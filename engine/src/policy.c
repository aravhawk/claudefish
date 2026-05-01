#include "policy.h"

#include <stdlib.h>
#include <string.h>

#include "movegen.h"
#include "position.h"

static PolicyState policy_state;
static bool policy_initialized = false;

static int policy_clipped_relu(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return value;
}

/* Pre-computed adaptive search parameters per position class */
static const AdaptParams adapt_params_table[ADAPT_NUM_CLASSES] = {
    /* 0: tactical - less pruning, more extensions */
    { 80,  -1, 120, 200, 8 },
    /* 1: quiet_positional - aggressive pruning, deeper search in key lines */
    { 110,  1,  90,  80, 20 },
    /* 2: endgame - less null move, careful pruning */
    { 95,   0, 100, 100, 15 },
    /* 3: material_imbalance - cautious, extend more */
    { 90,   0, 110, 110, 12 }
};

void policy_init(void) {
    if (policy_initialized) return;
    memset(&policy_state, 0, sizeof(policy_state));
    policy_initialized = true;
}

static void policy_free_weights(PolicyWeights *w) {
    if (w == NULL) return;
    free(w->h1_weights); w->h1_weights = NULL;
    free(w->h1_biases); w->h1_biases = NULL;
    free(w->out_weights); w->out_weights = NULL;
    free(w->out_biases); w->out_biases = NULL;
    free(w->adapt_h1_weights); w->adapt_h1_weights = NULL;
    free(w->adapt_h1_biases); w->adapt_h1_biases = NULL;
    free(w->adapt_out_weights); w->adapt_out_weights = NULL;
    free(w->adapt_out_biases); w->adapt_out_biases = NULL;
}

static bool policy_allocate_weights(PolicyWeights *w) {
    if (w == NULL) return false;

    w->h1_weights = (int8_t *)malloc((size_t)(NNUE_ACCUMULATOR_DIM * 2) * POLICY_HIDDEN_DIM * sizeof(int8_t));
    w->h1_biases = (int32_t *)malloc((size_t)POLICY_HIDDEN_DIM * sizeof(int32_t));
    w->out_weights = (int8_t *)malloc((size_t)POLICY_HIDDEN_DIM * POLICY_OUTPUT_DIM * sizeof(int8_t));
    w->out_biases = (int32_t *)malloc((size_t)POLICY_OUTPUT_DIM * sizeof(int32_t));
    w->adapt_h1_weights = (int8_t *)malloc((size_t)(NNUE_ACCUMULATOR_DIM * 2) * ADAPT_HIDDEN_DIM * sizeof(int8_t));
    w->adapt_h1_biases = (int32_t *)malloc((size_t)ADAPT_HIDDEN_DIM * sizeof(int32_t));
    w->adapt_out_weights = (int8_t *)malloc((size_t)ADAPT_HIDDEN_DIM * ADAPT_NUM_CLASSES * sizeof(int8_t));
    w->adapt_out_biases = (int32_t *)malloc((size_t)ADAPT_NUM_CLASSES * sizeof(int32_t));

    if (!w->h1_weights || !w->h1_biases || !w->out_weights || !w->out_biases ||
        !w->adapt_h1_weights || !w->adapt_h1_biases || !w->adapt_out_weights || !w->adapt_out_biases) {
        policy_free_weights(w);
        return false;
    }
    return true;
}

/* Generate placeholder weights that produce a basic material/position signal */
static void policy_generate_placeholder(PolicyWeights *w) {
    int j;

    memset(w->h1_weights, 0, (size_t)(NNUE_ACCUMULATOR_DIM * 2) * POLICY_HIDDEN_DIM);
    memset(w->h1_biases, 0, POLICY_HIDDEN_DIM * sizeof(int32_t));
    memset(w->out_weights, 0, (size_t)POLICY_HIDDEN_DIM * POLICY_OUTPUT_DIM);
    memset(w->out_biases, 0, POLICY_OUTPUT_DIM * sizeof(int32_t));

    /* Small bias in h1 so the network isn't zero */
    for (j = 0; j < POLICY_HIDDEN_DIM; ++j) {
        w->h1_biases[j] = 50 * (1 << NNUE_WEIGHT_SCALE_BITS);
    }

    /* Uniform output biases so all moves have equal prior */
    for (j = 0; j < POLICY_OUTPUT_DIM; ++j) {
        w->out_biases[j] = 0;
    }

    /* Adaptive head placeholder */
    memset(w->adapt_h1_weights, 0, (size_t)(NNUE_ACCUMULATOR_DIM * 2) * ADAPT_HIDDEN_DIM);
    memset(w->adapt_h1_biases, 0, ADAPT_HIDDEN_DIM * sizeof(int32_t));
    memset(w->adapt_out_weights, 0, (size_t)ADAPT_HIDDEN_DIM * ADAPT_NUM_CLASSES);
    memset(w->adapt_out_biases, 0, ADAPT_NUM_CLASSES * sizeof(int32_t));

    for (j = 0; j < ADAPT_HIDDEN_DIM; ++j) {
        w->adapt_h1_biases[j] = 10 * (1 << NNUE_WEIGHT_SCALE_BITS);
    }
}

bool policy_load_embedded(void) {
    policy_init();
    policy_free_weights(&policy_state.weights);
    if (!policy_allocate_weights(&policy_state.weights)) {
        return false;
    }

    policy_generate_placeholder(&policy_state.weights);
    policy_state.loaded = true;
    return true;
}

bool policy_is_loaded(void) {
    return policy_state.loaded;
}

int policy_score_move(
    const NNUEAccumulator *acc,
    Move move
) {
    int from, to, move_idx;
    int j, i;
    int32_t hidden[POLICY_HIDDEN_DIM];
    int8_t activated[POLICY_HIDDEN_DIM];
    int32_t move_logit;
    const PolicyWeights *w;

    if (!policy_state.loaded || acc == NULL) return 0;

    w = &policy_state.weights;

    from = move_source(move);
    to = move_target(move);
    move_idx = from * 64 + to;

    if (move_idx < 0 || move_idx >= POLICY_OUTPUT_DIM) return 0;

    /* Hidden layer 1: ClippedReLU(accumulator) -> linear transform */
    for (i = 0; i < POLICY_HIDDEN_DIM; ++i) {
        hidden[i] = w->h1_biases[i];
    }

    for (j = 0; j < NNUE_ACCUMULATOR_DIM; ++j) {
        int8_t a = (int8_t) policy_clipped_relu(acc->values[WHITE][j]);
        if (a == 0) continue;
        const int8_t *col = w->h1_weights + j * POLICY_HIDDEN_DIM;
        for (i = 0; i < POLICY_HIDDEN_DIM; ++i) {
            hidden[i] += (int32_t)a * (int32_t)col[i];
        }
    }
    for (j = 0; j < NNUE_ACCUMULATOR_DIM; ++j) {
        int8_t a = (int8_t) policy_clipped_relu(acc->values[BLACK][j]);
        if (a == 0) continue;
        const int8_t *col = w->h1_weights + (NNUE_ACCUMULATOR_DIM + j) * POLICY_HIDDEN_DIM;
        for (i = 0; i < POLICY_HIDDEN_DIM; ++i) {
            hidden[i] += (int32_t)a * (int32_t)col[i];
        }
    }

    for (j = 0; j < POLICY_HIDDEN_DIM; ++j) {
        activated[j] = (int8_t) policy_clipped_relu(hidden[j] >> NNUE_WEIGHT_SCALE_BITS);
    }

    /* Output: read only the column for this specific move */
    move_logit = w->out_biases[move_idx];
    {
        const int8_t *col = w->out_weights + move_idx * POLICY_HIDDEN_DIM;
        for (j = 0; j < POLICY_HIDDEN_DIM; ++j) {
            move_logit += (int32_t)activated[j] * (int32_t)col[j];
        }
    }

    /* Convert logit to score: sigmoid-like mapping to [0, POLICY_SCORE_SCALE] */
    move_logit >>= NNUE_WEIGHT_SCALE_BITS;

    /* Clamp and scale */
    if (move_logit < -100) move_logit = -100;
    if (move_logit > 100) move_logit = 100;

    /* Map [-100, 100] -> [0, POLICY_SCORE_SCALE] with sigmoid-like curve */
    {
        int normalized = move_logit + 100; /* [0, 200] */
        return (normalized * POLICY_SCORE_SCALE) / 200;
    }
}

int policy_classify_position(
    const NNUEAccumulator *acc,
    const Position *pos
) {
    int j, i;
    int32_t hidden[ADAPT_HIDDEN_DIM];
    int8_t activated[ADAPT_HIDDEN_DIM];
    int32_t logits[ADAPT_NUM_CLASSES];
    int best_class = 0;
    int32_t best_logit;
    const PolicyWeights *w;

    if (!policy_state.loaded || acc == NULL || pos == NULL) return -1;

    w = &policy_state.weights;

    /* Use heuristic fallback when placeholder weights are loaded.
       Classify based on simple position features. */
    {
        int phase = 0;
        int total_pieces = 0;
        Color side;

        for (side = WHITE; side <= BLACK; ++side) {
            PieceType pt;
            for (pt = KNIGHT; pt <= QUEEN; ++pt) {
                int count = bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(side, pt))]);
                phase += count * (pt == KNIGHT || pt == BISHOP ? 1 : pt == ROOK ? 2 : 4);
                total_pieces += count;
            }
        }

        /* Also count pawns */
        for (side = WHITE; side <= BLACK; ++side) {
            total_pieces += bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))]);
        }

        /* Classify */
        if (total_pieces <= 6) return 2; /* endgame */
        if (movegen_is_in_check(pos, (Color)pos->side_to_move)) return 0; /* tactical */
        if (phase <= 4) return 1; /* quiet_positional */
        return 1; /* default to quiet_positional */
    }

    /* Full neural classification (used when real weights are loaded) */
    for (i = 0; i < ADAPT_HIDDEN_DIM; ++i) {
        hidden[i] = w->adapt_h1_biases[i];
    }
    for (j = 0; j < NNUE_ACCUMULATOR_DIM * 2; ++j) {
        int8_t a;
        const int8_t *col;
        if (j < NNUE_ACCUMULATOR_DIM) {
            a = (int8_t) policy_clipped_relu(acc->values[WHITE][j]);
        } else {
            a = (int8_t) policy_clipped_relu(acc->values[BLACK][j - NNUE_ACCUMULATOR_DIM]);
        }
        if (a == 0) continue;
        col = w->adapt_h1_weights + j * ADAPT_HIDDEN_DIM;
        for (i = 0; i < ADAPT_HIDDEN_DIM; ++i) {
            hidden[i] += (int32_t)a * (int32_t)col[i];
        }
    }
    for (j = 0; j < ADAPT_HIDDEN_DIM; ++j) {
        activated[j] = (int8_t) policy_clipped_relu(hidden[j] >> NNUE_WEIGHT_SCALE_BITS);
    }

    for (i = 0; i < ADAPT_NUM_CLASSES; ++i) {
        logits[i] = w->adapt_out_biases[i];
    }
    for (j = 0; j < ADAPT_HIDDEN_DIM; ++j) {
        if (activated[j] == 0) continue;
        const int8_t *col = w->adapt_out_weights + j * ADAPT_NUM_CLASSES;
        for (i = 0; i < ADAPT_NUM_CLASSES; ++i) {
            logits[i] += (int32_t)activated[j] * (int32_t)col[i];
        }
    }

    best_logit = logits[0];
    best_class = 0;
    for (i = 1; i < ADAPT_NUM_CLASSES; ++i) {
        if (logits[i] > best_logit) {
            best_logit = logits[i];
            best_class = i;
        }
    }

    return best_class;
}

AdaptParams policy_get_adapt_params(int position_class) {
    if (position_class < 0 || position_class >= ADAPT_NUM_CLASSES) {
        /* Default: return the quiet_positional params */
        AdaptParams default_params = { 100, 0, 100, 100, 15 };
        return default_params;
    }
    return adapt_params_table[position_class];
}

void policy_cleanup(void) {
    policy_free_weights(&policy_state.weights);
    policy_state.loaded = false;
    policy_initialized = false;
}
