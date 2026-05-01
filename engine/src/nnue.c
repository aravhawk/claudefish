#include "nnue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "position.h"

/* ---- Embedded Network Data ----
   We embed a small HalfKP NNUE network compiled from public training data.
   The network weights are generated at build time and included as a binary header.
   For now we use a placeholder that provides the structure. The actual weights
   will be embedded via a generated header file. */

/* Placeholder: a minimal "identity-like" network that gives reasonable eval
   by falling back to classical eval concepts. Real weights will replace this. */

static NNUENet nnue_net;
static bool nnue_initialized = false;

/* ---- Feature Index Computation (HalfKP) ----
   For each perspective (white/black), compute the feature index for a piece.
   King square determines the "bucket" (which set of features), and each
   non-king piece maps to a unique feature within that bucket. */

static inline int nnue_halfkp_feature_index(
    int king_square,
    Piece piece,
    int piece_square,
    Color perspective
) {
    int p_color;
    int p_type;

    if (!piece_is_valid(piece) || !square_is_valid(piece_square)) {
        return -1;
    }

    /* King piece doesn't have a feature - it defines the bucket */
    if (piece_type(piece) == KING) {
        return -1;
    }

    p_color = piece_color(piece);
    p_type = piece_type(piece);

    /* Adjust king square for black perspective: mirror vertically */
    if (perspective == BLACK) {
        king_square = (king_square ^ 56); /* flip rank */
        piece_square = (piece_square ^ 56);
        p_color = p_color == WHITE ? BLACK : WHITE;
    }

    /* Index: king_bucket * 641 + (piece_color * 6 + piece_type) * 64 + piece_square */
    return king_square * NNUE_HALFKP_DIM_PER_KING +
           (p_color * PIECE_TYPE_NB + p_type) * BOARD_SQUARES +
           piece_square;
}

/* ---- ClippedReLU activation: clamp to [0, 127] ---- */
static inline int nnue_clipped_relu(int value) {
    if (value < 0) return 0;
    if (value > NNUE_ACTIVATION_RANGE) return NNUE_ACTIVATION_RANGE;
    return value;
}

/* ---- Feature Transformer: sparse input -> dense accumulator ----
   For each active feature, add the corresponding column from ft_weights.
   Incremental: on piece move, subtract old column and add new column. */

static void nnue_add_feature(
    int16_t *accumulator,
    int feature_index,
    const int16_t *ft_weights
) {
    int i;
    const int16_t *column;

    if (feature_index < 0 || feature_index >= NNUE_HALFKP_INPUT_DIM) {
        return;
    }

    column = ft_weights + feature_index * NNUE_ACCUMULATOR_DIM;
    for (i = 0; i < NNUE_ACCUMULATOR_DIM; ++i) {
        accumulator[i] += column[i];
    }
}

static void nnue_sub_feature(
    int16_t *accumulator,
    int feature_index,
    const int16_t *ft_weights
) {
    int i;
    const int16_t *column;

    if (feature_index < 0 || feature_index >= NNUE_HALFKP_INPUT_DIM) {
        return;
    }

    column = ft_weights + feature_index * NNUE_ACCUMULATOR_DIM;
    for (i = 0; i < NNUE_ACCUMULATOR_DIM; ++i) {
        accumulator[i] -= column[i];
    }
}

/* Full recomputation of one perspective's accumulator */
static void nnue_compute_accumulator(
    int16_t *accumulator,
    const int16_t *ft_biases,
    const int16_t *ft_weights,
    const Position *pos,
    Color perspective
) {
    int sq;
    int king_sq;
    Piece king_piece = make_piece(perspective, KING);

    /* Start with biases */
    memcpy(accumulator, ft_biases, NNUE_ACCUMULATOR_DIM * sizeof(int16_t));

    /* Find our king */
    king_sq = -1;
    for (sq = 0; sq < BOARD_SQUARES; ++sq) {
        if (pos->mailbox[sq] == (uint8_t) king_piece) {
            king_sq = sq;
            break;
        }
    }

    if (king_sq < 0) {
        return;
    }

    /* Add features for all non-king pieces */
    for (sq = 0; sq < BOARD_SQUARES; ++sq) {
        Piece piece = (Piece) pos->mailbox[sq];
        if (piece == NO_PIECE || piece_type(piece) == KING) {
            continue;
        }
        int idx = nnue_halfkp_feature_index(king_sq, piece, sq, perspective);
        if (idx >= 0) {
            nnue_add_feature(accumulator, idx, ft_weights);
        }
    }
}

/* ---- Forward pass through hidden layers ---- */
static int nnue_forward_pass(
    const int16_t *acc_white,
    const int16_t *acc_black,
    const NNUEWeights *w
) {
    int i, j;
    int8_t h1_input[NNUE_ACCUMULATOR_DIM * 2]; /* ClippedReLU of concatenated accumulators */
    int32_t h1[NNUE_HIDDEN1_DIM];
    int8_t h1_activated[NNUE_HIDDEN1_DIM];
    int32_t h2[NNUE_HIDDEN2_DIM];
    int8_t h2_activated[NNUE_HIDDEN2_DIM];
    int32_t output;

    /* Step 1: ClippedReLU on accumulator values, concat white+black */
    for (j = 0; j < NNUE_ACCUMULATOR_DIM; ++j) {
        h1_input[j] = (int8_t) nnue_clipped_relu(acc_white[j]);
        h1_input[NNUE_ACCUMULATOR_DIM + j] = (int8_t) nnue_clipped_relu(acc_black[j]);
    }

    /* Step 2: Hidden layer 1 - Linear(512 -> 32) + ClippedReLU */
    for (i = 0; i < NNUE_HIDDEN1_DIM; ++i) {
        h1[i] = w->h1_biases[i];
    }
    for (j = 0; j < NNUE_ACCUMULATOR_DIM * 2; ++j) {
        if (h1_input[j] == 0) continue;
        const int8_t *col = w->h1_weights + j * NNUE_HIDDEN1_DIM;
        for (i = 0; i < NNUE_HIDDEN1_DIM; ++i) {
            h1[i] += (int32_t) h1_input[j] * (int32_t) col[i];
        }
    }
    for (j = 0; j < NNUE_HIDDEN1_DIM; ++j) {
        h1_activated[j] = (int8_t) nnue_clipped_relu(h1[j] >> NNUE_WEIGHT_SCALE_BITS);
    }

    /* Step 3: Hidden layer 2 - Linear(32 -> 32) + ClippedReLU */
    for (i = 0; i < NNUE_HIDDEN2_DIM; ++i) {
        h2[i] = w->h2_biases[i];
    }
    for (j = 0; j < NNUE_HIDDEN1_DIM; ++j) {
        if (h1_activated[j] == 0) continue;
        const int8_t *col = w->h2_weights + j * NNUE_HIDDEN2_DIM;
        for (i = 0; i < NNUE_HIDDEN2_DIM; ++i) {
            h2[i] += (int32_t) h1_activated[j] * (int32_t) col[i];
        }
    }
    for (j = 0; j < NNUE_HIDDEN2_DIM; ++j) {
        h2_activated[j] = (int8_t) nnue_clipped_relu(h2[j] >> NNUE_WEIGHT_SCALE_BITS);
    }

    /* Step 4: Output layer - Linear(32 -> 1) */
    /* Output weights are stored after h2 weights */
    output = w->out_bias;
    {
        const int8_t *out_w = w->h2_weights + NNUE_HIDDEN1_DIM * NNUE_HIDDEN2_DIM;
        for (j = 0; j < NNUE_HIDDEN2_DIM; ++j) {
            output += (int32_t) h2_activated[j] * (int32_t) out_w[j];
        }
    }

    return (int) (output >> NNUE_WEIGHT_SCALE_BITS);
}

/* ---- Public API ---- */

void nnue_init(void) {
    if (nnue_initialized) return;

    memset(&nnue_net, 0, sizeof(nnue_net));
    nnue_initialized = true;
}

static void nnue_free_weights(NNUEWeights *w) {
    if (w == NULL) return;
    free(w->ft_weights); w->ft_weights = NULL;
    free(w->ft_biases);  w->ft_biases = NULL;
    free(w->h1_weights); w->h1_weights = NULL;
    free(w->h1_biases);  w->h1_biases = NULL;
    free(w->h2_weights); w->h2_weights = NULL;
    free(w->h2_biases);  w->h2_biases = NULL;
}

static bool nnue_allocate_weights(NNUEWeights *w) {
    if (w == NULL) return false;

    w->ft_weights = (int16_t *) malloc((size_t) NNUE_HALFKP_INPUT_DIM * NNUE_ACCUMULATOR_DIM * sizeof(int16_t));
    w->ft_biases  = (int16_t *) malloc((size_t) NNUE_ACCUMULATOR_DIM * sizeof(int16_t));
    w->h1_weights = (int8_t *)  malloc((size_t) (NNUE_ACCUMULATOR_DIM * 2) * NNUE_HIDDEN1_DIM * sizeof(int8_t));
    w->h1_biases  = (int32_t *) malloc((size_t) NNUE_HIDDEN1_DIM * sizeof(int32_t));
    w->h2_weights = (int8_t *)  malloc((size_t) (NNUE_HIDDEN1_DIM * NNUE_HIDDEN2_DIM + NNUE_HIDDEN2_DIM) * sizeof(int8_t));
    w->h2_biases  = (int32_t *) malloc((size_t) NNUE_HIDDEN2_DIM * sizeof(int32_t));

    if (w->ft_weights == NULL || w->ft_biases == NULL ||
        w->h1_weights == NULL || w->h1_biases == NULL ||
        w->h2_weights == NULL || w->h2_biases == NULL) {
        nnue_free_weights(w);
        return false;
    }

    return true;
}

/* ---- Load Network from Raw Binary Data ----
   Binary format:
     4 bytes: magic (0x4E4E5545)
     4 bytes: version
     256 bytes: description string
     FT biases: 256 * int16
     FT weights: 41024 * 256 * int16
     H1 biases: 32 * int32
     H1 weights: 512 * 32 * int8
     H2 biases: 32 * int32
     H2 weights: 32 * 32 * int8 (+ 32 int8 output weights appended)
     Out bias: 1 * int32 */

static bool nnue_load_from_data(const uint8_t *data, size_t data_size) {
    const uint8_t *ptr = data;
    const uint8_t *end = data + data_size;
    uint32_t magic, version;
    size_t expected_size;

    if (data == NULL || data_size < 8) return false;

    /* Read header */
    memcpy(&magic, ptr, 4); ptr += 4;
    memcpy(&version, ptr, 4); ptr += 4;

    if (magic != NNUE_MAGIC) return false;
    if (version != NNUE_VERSION) return false;

    /* Description */
    if (ptr + 256 > end) return false;
    memcpy(nnue_net.description, ptr, 256);
    nnue_net.description[255] = '\0';
    ptr += 256;

    /* Allocate weights */
    nnue_free_weights(&nnue_net.weights);
    if (!nnue_allocate_weights(&nnue_net.weights)) return false;

    /* Calculate expected size */
    expected_size = 8 + 256 + /* header */
        NNUE_ACCUMULATOR_DIM * sizeof(int16_t) + /* ft biases */
        (size_t) NNUE_HALFKP_INPUT_DIM * NNUE_ACCUMULATOR_DIM * sizeof(int16_t) + /* ft weights */
        NNUE_HIDDEN1_DIM * sizeof(int32_t) + /* h1 biases */
        (size_t) (NNUE_ACCUMULATOR_DIM * 2) * NNUE_HIDDEN1_DIM * sizeof(int8_t) + /* h1 weights */
        NNUE_HIDDEN2_DIM * sizeof(int32_t) + /* h2 biases */
        ((size_t) NNUE_HIDDEN1_DIM * NNUE_HIDDEN2_DIM + NNUE_HIDDEN2_DIM) * sizeof(int8_t) + /* h2+out weights */
        sizeof(int32_t); /* out bias */

    if (data_size < expected_size) {
        nnue_free_weights(&nnue_net.weights);
        return false;
    }

    /* FT biases */
    memcpy(nnue_net.weights.ft_biases, ptr, NNUE_ACCUMULATOR_DIM * sizeof(int16_t));
    ptr += NNUE_ACCUMULATOR_DIM * sizeof(int16_t);

    /* FT weights */
    {
        size_t ft_weight_bytes = (size_t) NNUE_HALFKP_INPUT_DIM * NNUE_ACCUMULATOR_DIM * sizeof(int16_t);
        memcpy(nnue_net.weights.ft_weights, ptr, ft_weight_bytes);
        ptr += ft_weight_bytes;
    }

    /* H1 biases */
    memcpy(nnue_net.weights.h1_biases, ptr, NNUE_HIDDEN1_DIM * sizeof(int32_t));
    ptr += NNUE_HIDDEN1_DIM * sizeof(int32_t);

    /* H1 weights */
    {
        size_t h1_weight_bytes = (size_t) (NNUE_ACCUMULATOR_DIM * 2) * NNUE_HIDDEN1_DIM * sizeof(int8_t);
        memcpy(nnue_net.weights.h1_weights, ptr, h1_weight_bytes);
        ptr += h1_weight_bytes;
    }

    /* H2 biases */
    memcpy(nnue_net.weights.h2_biases, ptr, NNUE_HIDDEN2_DIM * sizeof(int32_t));
    ptr += NNUE_HIDDEN2_DIM * sizeof(int32_t);

    /* H2 + output weights (stored contiguously) */
    {
        size_t h2_total = ((size_t) NNUE_HIDDEN1_DIM * NNUE_HIDDEN2_DIM + NNUE_HIDDEN2_DIM) * sizeof(int8_t);
        memcpy(nnue_net.weights.h2_weights, ptr, h2_total);
        ptr += h2_total;
    }

    /* Output bias */
    memcpy(&nnue_net.weights.out_bias, ptr, sizeof(int32_t));

    nnue_net.loaded = true;
    return true;
}

/* ---- Placeholder: generate a minimal weight set ----
   This creates a "zero network" that outputs 0 for all positions,
   allowing the engine to function while real weights are obtained.
   The classical eval will be used as fallback when NNUE eval is ~0. */

static void nnue_generate_zero_weights(NNUEWeights *w) {
    if (w == NULL) return;

    /* Zero everything */
    memset(w->ft_biases, 0, NNUE_ACCUMULATOR_DIM * sizeof(int16_t));
    memset(w->ft_weights, 0, (size_t) NNUE_HALFKP_INPUT_DIM * NNUE_ACCUMULATOR_DIM * sizeof(int16_t));
    memset(w->h1_biases, 0, NNUE_HIDDEN1_DIM * sizeof(int32_t));
    memset(w->h1_weights, 0, (size_t) (NNUE_ACCUMULATOR_DIM * 2) * NNUE_HIDDEN1_DIM * sizeof(int8_t));
    memset(w->h2_biases, 0, NNUE_HIDDEN2_DIM * sizeof(int32_t));
    memset(w->h2_weights, 0, ((size_t) NNUE_HIDDEN1_DIM * NNUE_HIDDEN2_DIM + NNUE_HIDDEN2_DIM) * sizeof(int8_t));
    w->out_bias = 0;

    /* Add a tiny signal so the network isn't identically zero:
       set some bias values to small numbers that give a basic material-like signal */
    {
        int i;
        /* FT biases: slight positive bias to make accumulator slightly positive
           with pieces on the board (the sum of features will add to this) */
        for (i = 0; i < NNUE_ACCUMULATOR_DIM; ++i) {
            w->ft_biases[i] = 10;
        }

        /* H1 biases: small positive to pass signal through */
        for (i = 0; i < NNUE_HIDDEN1_DIM; ++i) {
            w->h1_biases[i] = 100 * (1 << NNUE_WEIGHT_SCALE_BITS);
        }
    }
}

bool nnue_load_embedded(void) {
    nnue_init();

    /* For now, we don't load a placeholder zero-weight network.
       A real pre-trained NNUE network will be loaded when available.
       This means nnue_is_loaded() returns false and the search
       uses classical evaluation as fallback. */
    return false;
}

bool nnue_load_from_file(const char *path) {
    /* File loading not needed for WASM build.
       Native builds can load .nnue files from disk. */
#ifdef __EMSCRIPTEN__
    (void) path;
    return false;
#else
    FILE *f;
    uint8_t *data;
    size_t file_size;
    bool result;

    if (path == NULL) return false;

    f = fopen(path, "rb");
    if (f == NULL) return false;

    fseek(f, 0, SEEK_END);
    file_size = (size_t) ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (uint8_t *) malloc(file_size);
    if (data == NULL) {
        fclose(f);
        return false;
    }

    if (fread(data, 1, file_size, f) != file_size) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);

    result = nnue_load_from_data(data, file_size);
    free(data);
    return result;
#endif
}

bool nnue_is_loaded(void) {
    return nnue_net.loaded;
}

void nnue_state_init(NNUEState *state, const Position *pos) {
    if (state == NULL) return;

    memset(state, 0, sizeof(*state));
    state->current = 0;
    state->dirty_num = 0;
    state->initialized = true;

    /* Mark all accumulators as not computed */
    for (int i = 0; i < NNUE_MAX_PLY; ++i) {
        state->acc_stack[i].computed[WHITE] = false;
        state->acc_stack[i].computed[BLACK] = false;
    }

    /* Recompute from scratch */
    nnue_refresh_accumulators(state, pos);
}

void nnue_push(NNUEState *state) {
    if (state == NULL || !state->initialized) return;

    int next = state->current + 1;
    if (next >= NNUE_MAX_PLY) return;

    /* Copy current accumulator to next slot */
    memcpy(&state->acc_stack[next], &state->acc_stack[state->current],
           sizeof(NNUEAccumulator));
    state->current = next;
    state->dirty_num = 0;
}

void nnue_pop(NNUEState *state) {
    if (state == NULL || !state->initialized) return;
    if (state->current > 0) {
        state->current--;
    }
}

void nnue_update_accumulator_incremental(
    NNUEState *state,
    const Position *pos,
    Piece piece,
    int from,
    int to
) {
    int sq, king_sq;
    Color perspective;
    int16_t *accumulator;
    const int16_t *ft_weights;
    NNUEAccumulator *acc;

    if (state == NULL || !state->initialized || !nnue_net.loaded) return;

    acc = &state->acc_stack[state->current];
    ft_weights = nnue_net.weights.ft_weights;

    /* For each perspective, update the accumulator */
    for (perspective = WHITE; perspective <= BLACK; ++perspective) {
        Piece king_piece = make_piece(perspective, KING);

        /* Find king square */
        king_sq = -1;
        for (sq = 0; sq < BOARD_SQUARES; ++sq) {
            if (pos->mailbox[sq] == (uint8_t) king_piece) {
                king_sq = sq;
                break;
            }
        }

        if (king_sq < 0) {
            acc->computed[perspective] = false;
            continue;
        }

        if (!acc->computed[perspective]) {
            /* Can't do incremental update if parent isn't computed */
            continue;
        }

        accumulator = acc->values[perspective];

        /* Remove old feature (piece at 'from') */
        if (square_is_valid(from) && piece_is_valid(piece)) {
            int old_idx = nnue_halfkp_feature_index(king_sq, piece, from, perspective);
            if (old_idx >= 0) {
                nnue_sub_feature(accumulator, old_idx, ft_weights);
            }
        }

        /* Add new feature (piece at 'to') */
        if (square_is_valid(to) && piece_is_valid(piece)) {
            int new_idx = nnue_halfkp_feature_index(king_sq, piece, to, perspective);
            if (new_idx >= 0) {
                nnue_add_feature(accumulator, new_idx, ft_weights);
            }
        }

        acc->computed[perspective] = true;
    }
}

void nnue_refresh_accumulators(NNUEState *state, const Position *pos) {
    NNUEAccumulator *acc;

    if (state == NULL || !state->initialized || !nnue_net.loaded || pos == NULL) return;

    acc = &state->acc_stack[state->current];

    /* Compute both perspectives from scratch */
    nnue_compute_accumulator(
        acc->values[WHITE],
        nnue_net.weights.ft_biases,
        nnue_net.weights.ft_weights,
        pos,
        WHITE
    );
    acc->computed[WHITE] = true;

    nnue_compute_accumulator(
        acc->values[BLACK],
        nnue_net.weights.ft_biases,
        nnue_net.weights.ft_weights,
        pos,
        BLACK
    );
    acc->computed[BLACK] = true;
}

int nnue_evaluate(NNUEState *state, const Position *pos) {
    NNUEAccumulator *acc;
    int score;

    if (state == NULL || !state->initialized || !nnue_net.loaded || pos == NULL) {
        return 0;
    }

    acc = &state->acc_stack[state->current];

    /* If accumulators aren't computed, recompute */
    if (!acc->computed[WHITE] || !acc->computed[BLACK]) {
        nnue_refresh_accumulators(state, pos);
    }

    /* Forward pass: the output is from white's perspective.
       Negate for black's side to move. */
    score = nnue_forward_pass(acc->values[WHITE], acc->values[BLACK], &nnue_net.weights);

    /* Scale to centipawns and orient for side to move */
    score = (score * NNUE_EVAL_SCALE) / 100;
    if (pos->side_to_move == BLACK) {
        score = -score;
    }

    return score;
}

NNUEState *nnue_create_state(void) {
    NNUEState *state = (NNUEState *) calloc(1, sizeof(NNUEState));
    return state;
}

void nnue_destroy_state(NNUEState *state) {
    free(state);
}

void nnue_cleanup(void) {
    nnue_free_weights(&nnue_net.weights);
    nnue_net.loaded = false;
    nnue_initialized = false;
}
