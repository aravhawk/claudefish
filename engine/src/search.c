#include "search.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include <pthread.h>
#endif

#include "correction.h"
#include "draw.h"
#include "evaluate.h"
#include "mcts.h"
#include "mate_search.h"
#include "movorder.h"
#include "nnue.h"
#include "policy.h"
#include "see.h"
#include "syzygy.h"
#include "time.h"
#include "tt.h"

typedef struct SearchContext {
    uint64_t nodes;
    uint64_t qnodes;
    SearchTimer timer;
    bool stop;
    Move killers[SEARCH_MAX_PLY][2];
    int history[2][BOARD_SQUARES][BOARD_SQUARES];
    int *continuation_history; /* heap-allocated: [PT_NB*64*PT_NB*64] */
    Move countermoves[BOARD_SQUARES][BOARD_SQUARES];
    Move last_move;
    PieceType last_piece_type;
    int last_target;

    /* NNUE state: heap-allocated to avoid stack overflow from large accumulator stack */
    NNUEState *nnue_state;
    bool use_nnue; /* whether NNUE eval is being used */

    /* Adaptive search parameters from policy classification */
    AdaptParams adapt_params;
    bool adapt_initialized;

    /* Ensemble disagreement tracking */
    int prev_nnue_eval;  /* cached NNUE eval for ensemble comparison */
    int prev_classical_eval; /* cached classical eval */

    /* Correction History: corrects static eval based on past search results */
    CorrectionHistory *corrhist; /* heap-allocated to avoid stack overflow */

    /* Fortress detection: track eval stability across iterations */
    int iter_evals[SEARCH_MAX_DEPTH];
    int iter_eval_count;

    /* History-LM: learned move feature weights for adaptive ordering */
    int move_feature_hist[2][8]; /* [side][feature: capture/check/piece_type/etc] */

    /* Mate search context for df-PN verification (heap-allocated) */
    MateSearchContext *mate_ctx;
} SearchContext;

enum {
    CH_SIZE = PIECE_TYPE_NB * BOARD_SQUARES * PIECE_TYPE_NB * BOARD_SQUARES
};

#define CH_INDEX(pp, pt, cp, ct) ((pp) * BOARD_SQUARES * PIECE_TYPE_NB * BOARD_SQUARES + (pt) * PIECE_TYPE_NB * BOARD_SQUARES + (cp) * BOARD_SQUARES + (ct))

/* ---- Combined Evaluation: NNUE + Classical Ensemble ----
   When both evaluators are available, use NNUE as primary but check for
   disagreement. When NNUE and classical disagree significantly, this signals
   an unusual position where NNUE may have blind spots. */

enum {
    ENSEMBLE_DISAGREEMENT_THRESHOLD = 100, /* centipawns */
    ENSEMBLE_EXTENSION_DEPTH = 2
};

static int search_evaluate_combined(
    SearchContext *ctx,
    Position *pos,
    bool *disagreement
) {
    int classical_eval, nnue_eval, final_eval;

    if (disagreement != NULL) *disagreement = false;

    if (!ctx->use_nnue || !nnue_is_loaded()) {
        int raw_eval = eval_evaluate(pos);
        return corrhist_correct_eval(ctx->corrhist, pos, raw_eval);
    }

    /* Get NNUE evaluation */
    nnue_eval = nnue_evaluate(ctx->nnue_state, pos);

    /* Get classical evaluation for ensemble comparison */
    classical_eval = eval_evaluate(pos);

    /* Apply correction history to classical eval */
    classical_eval = corrhist_correct_eval(ctx->corrhist, pos, classical_eval);

    /* Cache for ensemble tracking */
    ctx->prev_nnue_eval = nnue_eval;
    ctx->prev_classical_eval = classical_eval;

    /* Use NNUE as primary evaluation */
    final_eval = nnue_eval;

    /* Ensemble disagreement detection */
    {
        int diff = abs(nnue_eval - classical_eval);
        if (diff > ENSEMBLE_DISAGREEMENT_THRESHOLD) {
            if (disagreement != NULL) *disagreement = true;
            /* Blend evaluations when they disagree: average NNUE and classical
               with more weight toward classical for positions NNUE might miscalibrate */
            final_eval = (nnue_eval * 3 + classical_eval) / 4;
        }
    }

    return final_eval;
}

static void search_ctx_init(SearchContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->continuation_history = (int *) calloc(CH_SIZE, sizeof(int));
    ctx->last_piece_type = (PieceType) -1;
    ctx->last_target = -1;
    ctx->use_nnue = false;
    ctx->nnue_state = NULL;
    ctx->adapt_initialized = false;

    /* Default adaptive params (no adjustment) */
    ctx->adapt_params.lmr_multiplier = 100;
    ctx->adapt_params.null_move_r_delta = 0;
    ctx->adapt_params.futility_margin = 100;
    ctx->adapt_params.lmp_threshold = 100;
    ctx->adapt_params.extension_threshold = 15;

    /* Initialize correction history (heap-allocated to avoid stack overflow) */
    ctx->corrhist = (CorrectionHistory *)calloc(1, sizeof(CorrectionHistory));
    if (ctx->corrhist != NULL) {
        corrhist_init(ctx->corrhist);
    }

    /* Initialize mate search context (heap-allocated to avoid stack overflow) */
    ctx->mate_ctx = (MateSearchContext *)calloc(1, sizeof(MateSearchContext));
    if (ctx->mate_ctx != NULL) {
        mate_search_init(ctx->mate_ctx, MATESEARCH_MAX_NODES);
    }
}

static void search_ctx_cleanup(SearchContext *ctx) {
    if (ctx->continuation_history != NULL) {
        free(ctx->continuation_history);
        ctx->continuation_history = NULL;
    }
    if (ctx->nnue_state != NULL) {
        nnue_destroy_state(ctx->nnue_state);
        ctx->nnue_state = NULL;
    }
    corrhist_cleanup(ctx->corrhist);
    if (ctx->corrhist != NULL) {
        free(ctx->corrhist);
        ctx->corrhist = NULL;
    }
    if (ctx->mate_ctx != NULL) {
        free(ctx->mate_ctx);
        ctx->mate_ctx = NULL;
    }
}

static const SearchOptions search_default_options = {
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true
};
static SearchOptions search_options = {
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true
};
static int search_num_threads = 1;
static int search_lmr_table[SEARCH_MAX_DEPTH + 1][MOVEGEN_MAX_MOVES + 1];
static bool search_initialized = false;
enum {
    SEARCH_STALEMATE_MARGIN = 500,
    SEARCH_STALEMATE_BIAS = 800
};

static void search_init_lmr_table(void) {
    int depth;

    for (depth = 0; depth <= SEARCH_MAX_DEPTH; ++depth) {
        int move_count;

        for (move_count = 0; move_count <= MOVEGEN_MAX_MOVES; ++move_count) {
            int reduction = 0;

            if (depth >= 3 && move_count > 3) {
                reduction = (int) (0.75 + (log((double) depth) * log((double) move_count) / 2.25));
                if (reduction < 1) {
                    reduction = 1;
                } else if (reduction > depth - 2) {
                    reduction = depth - 2;
                }
            }

            search_lmr_table[depth][move_count] = reduction;
        }
    }
}

static double search_elapsed_ms(const SearchContext *ctx) {
    return ctx == NULL ? 0.0 : time_elapsed_ms(&ctx->timer);
}

static void search_check_time(SearchContext *ctx) {
    uint64_t visited;

    if (ctx == NULL || ctx->stop || ctx->timer.limit_ms <= 0) {
        return;
    }

    visited = ctx->nodes + ctx->qnodes;
    if ((visited & (TIME_NODE_CHECK_GRANULARITY - 1ULL)) != 0) {
        return;
    }

    if (time_is_expired(&ctx->timer)) {
        ctx->stop = true;
    }
}

static bool search_move_is_legal(Position *pos, Move move) {
    MoveList legal_moves;
    size_t index;

    if (pos == NULL) {
        return false;
    }

    movegen_generate_legal(pos, &legal_moves);
    for (index = 0; index < legal_moves.count; ++index) {
        if (legal_moves.moves[index] == move) {
            return true;
        }
    }

    return false;
}

static void search_copy_pv(Move *destination, int *destination_length, const Move *source, int source_length) {
    int index;

    if (destination == NULL || destination_length == NULL) {
        return;
    }

    *destination_length = source_length;
    for (index = 0; index < source_length; ++index) {
        destination[index] = source[index];
    }
}

static void search_store_killer(SearchContext *ctx, int ply, Move move) {
    if (ctx == NULL || ply < 0 || ply >= SEARCH_MAX_PLY || movorder_is_capture(move)) {
        return;
    }

    if (ctx->killers[ply][0] == move) {
        return;
    }

    ctx->killers[ply][1] = ctx->killers[ply][0];
    ctx->killers[ply][0] = move;
}

static void search_store_countermove(SearchContext *ctx, Move move) {
    if (ctx == NULL || movorder_is_capture(move)) {
        return;
    }

    if (ctx->last_move != 0) {
        int from = move_source(ctx->last_move);
        int to = move_target(ctx->last_move);
        ctx->countermoves[from][to] = move;
    }
}

static void search_store_history(SearchContext *ctx, Color side, Move move, int depth) {
    int source;
    int target;
    int bonus;

    if (ctx == NULL || side < WHITE || side > BLACK || movorder_is_capture(move)) {
        return;
    }

    source = move_source(move);
    target = move_target(move);
    bonus = depth * depth;

    /* History gravity: apply bonus with gravity to prevent saturation.
       Formula: history += bonus - (history * |bonus| / 16384) */
    ctx->history[side][source][target] += bonus -
        (ctx->history[side][source][target] * (bonus < 0 ? -bonus : bonus)) / 16384;

    if (ctx->history[side][source][target] > 2000000) {
        ctx->history[side][source][target] /= 2;
    }
    if (ctx->history[side][source][target] < -2000000) {
        ctx->history[side][source][target] /= 2;
    }
}

static void search_store_continuation_history(SearchContext *ctx, PieceType prev_piece, int prev_target, PieceType curr_piece, int curr_target, int bonus) {
    int idx;
    int *entry;

    if (ctx == NULL || ctx->continuation_history == NULL) {
        return;
    }
    if (prev_piece < PAWN || prev_piece > KING || curr_piece < PAWN || curr_piece > KING) {
        return;
    }
    if (prev_target < 0 || prev_target >= BOARD_SQUARES || curr_target < 0 || curr_target >= BOARD_SQUARES) {
        return;
    }

    idx = CH_INDEX(prev_piece, prev_target, curr_piece, curr_target);
    entry = &ctx->continuation_history[idx];

    *entry += bonus - (*entry * (bonus < 0 ? -bonus : bonus)) / 16384;

    if (*entry > 2000000) {
        *entry /= 2;
    }
    if (*entry < -2000000) {
        *entry /= 2;
    }
}

static bool search_is_quiet_move(Move move) {
    return !movorder_is_capture(move) && move_promotion_piece(move) == MOVE_PROMOTION_NONE;
}

/* Quick estimate of whether a move gives check, without making the move.
   Used for History-LM feature tracking. */
static bool gives_check_estimated(const Position *pos, Move move) {
    int target = move_target(move);
    Piece moved_piece = position_get_piece(pos, move_source(move));
    PieceType pt = piece_type(moved_piece);
    Color side = (Color)pos->side_to_move;
    Color opp = side == WHITE ? BLACK : WHITE;
    int opp_king_sq = __builtin_ctzll(pos->piece_bitboards[make_piece(opp, KING) - 1]);

    if (pt == PAWN) {
        /* Pawn attacks the king square? */
        if (movegen_pawn_attacks[side][opp_king_sq] & (1ULL << target))
            return true;
    } else if (pt == KNIGHT) {
        if (movegen_knight_attacks[target] & (1ULL << opp_king_sq))
            return true;
    } else if (pt == KING) {
        return false; /* King can't give check */
    } else {
        /* Sliding pieces: check if target square attacks the king square
           through the occupancy after the move */
        Bitboard occ = pos->occupancy[BOTH];
        occ &= ~(1ULL << move_source(move));
        occ |= (1ULL << target);
        Bitboard attacks = 0;
        if (pt == BISHOP || pt == QUEEN) {
            attacks = movegen_bishop_attacks(target, occ);
        }
        if (pt == ROOK || pt == QUEEN) {
            attacks |= movegen_rook_attacks(target, occ);
        }
        if (attacks & (1ULL << opp_king_sq))
            return true;
    }
    return false;
}

static bool search_has_non_pawn_material(const Position *pos, Color side) {
    PieceType piece_type;

    if (pos == NULL) {
        return false;
    }

    for (piece_type = KNIGHT; piece_type <= QUEEN; ++piece_type) {
        if (pos->piece_bitboards[piece_bitboard_index(make_piece(side, piece_type))] != 0) {
            return true;
        }
    }

    return false;
}

static bool search_is_pawn_only_endgame(const Position *pos) {
    Color side;

    if (pos == NULL) {
        return false;
    }

    for (side = WHITE; side <= BLACK; ++side) {
        if (search_has_non_pawn_material(pos, side)) {
            return false;
        }
    }

    return true;
}

static int search_total_non_king_pieces(const Position *pos) {
    int total = 0;
    Color side;
    PieceType piece_type;

    if (pos == NULL) {
        return 0;
    }

    for (side = WHITE; side <= BLACK; ++side) {
        for (piece_type = PAWN; piece_type <= QUEEN; ++piece_type) {
            total += bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(side, piece_type))]);
        }
    }

    return total;
}

static int search_razor_margin(int depth) {
    static const int margins[4] = { 0, 200, 300, 450 };

    if (depth < 0) {
        depth = 0;
    } else if (depth > 3) {
        depth = 3;
    }

    return margins[depth];
}

bool search_move_to_uci(Move move, char buffer[6]) {
    char source[3];
    char target[3];
    PieceType promotion_piece;

    if (buffer == NULL ||
        !bitboard_square_to_string(move_source(move), source) ||
        !bitboard_square_to_string(move_target(move), target)) {
        return false;
    }

    buffer[0] = source[0];
    buffer[1] = source[1];
    buffer[2] = target[0];
    buffer[3] = target[1];

    promotion_piece = move_promotion_piece(move);
    if (promotion_piece == MOVE_PROMOTION_NONE) {
        buffer[4] = '\0';
        return true;
    }

    switch (promotion_piece) {
        case KNIGHT:
            buffer[4] = 'n';
            break;
        case BISHOP:
            buffer[4] = 'b';
            break;
        case ROOK:
            buffer[4] = 'r';
            break;
        case QUEEN:
            buffer[4] = 'q';
            break;
        default:
            return false;
    }

    buffer[5] = '\0';
    return true;
}

bool search_is_mate_score(int score) {
    return score > SEARCH_MATE_BOUND || score < -SEARCH_MATE_BOUND;
}

int search_mate_distance(int score) {
    int plies;

    if (!search_is_mate_score(score)) {
        return 0;
    }

    plies = SEARCH_MATE_SCORE - (score > 0 ? score : -score);
    return (plies + 1) / 2;
}

int search_debug_lmr_reduction(int depth, int move_count) {
    if (!search_initialized) {
        search_init();
    }

    if (depth < 0) {
        depth = 0;
    } else if (depth > SEARCH_MAX_DEPTH) {
        depth = SEARCH_MAX_DEPTH;
    }

    if (move_count < 0) {
        move_count = 0;
    } else if (move_count > MOVEGEN_MAX_MOVES) {
        move_count = MOVEGEN_MAX_MOVES;
    }

    return search_lmr_table[depth][move_count];
}

/* ---- MPC Rollout Leaf Evaluation ----
   Model Predictive Control (Bertsekas 2024) proves that 1-ply rollout
   at leaf nodes is provably better than raw static eval. We generate
   the opponent's most likely response (from policy or shallow search),
   make that move, evaluate, and use the resulting score. */
static int search_mpc_rollout(SearchContext *ctx, Position *pos, int raw_eval) {
    Color stm = (Color)pos->side_to_move;
    Color opp = stm == WHITE ? BLACK : WHITE;
    MoveList moves;
    Move best_response = 0;
    int best_response_score = -SEARCH_INF;

    /* Only do rollout if we have policy priors or can cheaply find opponent's best */
    if (!policy_is_loaded()) return raw_eval;

    movegen_generate_legal(pos, &moves);
    if (moves.count == 0) return raw_eval;

    /* Find the opponent's best move using a very shallow eval */
    for (size_t i = 0; i < moves.count; i++) {
        Move m = moves.moves[i];
        int score = 0;

        /* Quick evaluation: MVV-LVA for captures, PST diff for quiets */
        if (movorder_is_capture(m)) {
            Piece captured = move_captured_piece(m);
            PieceType ct = piece_type(captured);
            Piece mover = position_get_piece(pos, move_source(m));
            PieceType mt = piece_type(mover);
            score = ct * 10 - mt; /* MVV-LVA style */
        } else {
            /* Use policy if available */
            score = 0;
        }

        if (score > best_response_score) {
            best_response_score = score;
            best_response = m;
        }
    }

    if (best_response == 0) return raw_eval;

    /* Make the opponent's best response, evaluate, unmake */
    if (!movegen_make_move(pos, best_response)) return raw_eval;

    {
        int response_eval = eval_evaluate(pos);
        response_eval = corrhist_correct_eval(ctx->corrhist, pos, response_eval);
        /* The rollout eval is from the opponent's perspective after their move,
           so negate it back to our perspective */
        response_eval = -response_eval;

        movegen_unmake_move(pos);
        /* Blend rollout with raw eval (60/40 favoring rollout) */
        return (raw_eval * 2 + response_eval * 3) / 5;
    }
}

/* ---- Fortress Detection ----
   Track eval stability across iterative deepening iterations. If the eval
   is stuck in a narrow band for multiple iterations while the position has
   fortress-like features (closed files, low mobility for the stronger side),
   apply a draw bias to prevent wasting time winning unconvertible material. */
static bool search_detect_fortress(const SearchContext *ctx, const Position *pos, int current_eval) {
    if (ctx->iter_eval_count < 3) return false;

    /* Check eval stability: min/max over last 3 iterations */
    int min_eval = current_eval, max_eval = current_eval;
    int start = ctx->iter_eval_count > 3 ? ctx->iter_eval_count - 3 : 0;
    for (int i = start; i < ctx->iter_eval_count; i++) {
        int e = ctx->iter_evals[i];
        if (e < min_eval) min_eval = e;
        if (e > max_eval) max_eval = e;
    }

    /* If eval range is < 30cp over 3 iterations, check fortress features */
    if (max_eval - min_eval > 30) return false;

    /* Fortress features: closed pawn structure (many pawns on same files) */
    Bitboard wp = pos->piece_bitboards[piece_bitboard_index(make_piece(WHITE, PAWN))];
    Bitboard bp = pos->piece_bitboards[piece_bitboard_index(make_piece(BLACK, PAWN))];
    int closed_files = 0;
    for (int f = 0; f < 8; f++) {
        Bitboard file_mask = (0x0101010101010101ULL << f);
        if ((wp & file_mask) && (bp & file_mask)) closed_files++;
    }

    /* Strong side has low mobility (typical of fortress) */
    int total_mobility = 0;
    for (int sq = 0; sq < BOARD_SQUARES; sq++) {
        Piece p = position_get_piece(pos, sq);
        if (piece_is_valid(p) && piece_color(p) == (int)(pos->side_to_move)) {
            PieceType pt = piece_type(p);
            if (pt == KNIGHT) total_mobility += __builtin_popcountll(movegen_knight_attacks[sq] & ~pos->occupancy[pos->side_to_move]);
            else if (pt == KING) total_mobility += __builtin_popcountll(movegen_king_attacks[sq] & ~pos->occupancy[pos->side_to_move]);
        }
    }

    /* More than 4 closed files and very low non-pawn mobility = likely fortress */
    return closed_files >= 4 && total_mobility < 8;
}

void search_init(void) {
    movegen_init();
    eval_init();
    tt_init();
    nnue_init();
    policy_init();
    mcts_init();
    syzygy_init();

    if (!search_initialized) {
        search_init_lmr_table();
        search_initialized = true;
    }

    /* Load NNUE and policy networks if available */
    nnue_load_embedded();
    policy_load_embedded();
    syzygy_load_embedded();
}

void search_reset_heuristics(void) {
    tt_clear();
}

SearchOptions search_get_options(void) {
    return search_options;
}

void search_reset_options(void) {
    search_options = search_default_options;
}

void search_set_options(const SearchOptions *options) {
    if (options == NULL) {
        search_reset_options();
        return;
    }

    search_options = *options;
}

static int search_quiescence(Position *pos, SearchContext *ctx, int alpha, int beta, int ply) {
    TTEntry entry;
    Move tt_move = 0;
    Move best_move = 0;
    MoveList legal_moves;
    MoveList candidate_moves;
    OrderedMoveList ordered_moves;
    bool in_check;
    int alpha_original = alpha;
    int static_eval;
    int best_score;
    size_t index;

    ctx->qnodes++;
    search_check_time(ctx);
    if (ctx->stop) {
        return 0;
    }

    if (draw_is_draw(pos)) {
        return draw_score(pos);
    }

    in_check = movegen_is_in_check(pos, (Color) pos->side_to_move);
    tt_prefetch(pos->zobrist_hash);
    if (tt_probe(pos->zobrist_hash, &entry)) {
        tt_move = entry.best_move;
        static_eval = entry.static_eval;

        if (entry.depth == 0) {
            int tt_score = tt_score_from_entry(&entry, ply);

            if (entry.flag == TT_FLAG_EXACT) {
                return tt_score;
            }
            if (entry.flag == TT_FLAG_ALPHA && tt_score <= alpha) {
                return tt_score;
            }
            if (entry.flag == TT_FLAG_BETA && tt_score >= beta) {
                return tt_score;
            }
        }
    } else {
        static_eval = search_evaluate_combined(ctx, pos, NULL);
    }

    if (!in_check) {
        best_score = static_eval;
        if (best_score >= beta) {
            return best_score;
        }

        /* Delta pruning: if the best possible capture + margin can't raise alpha,
           skip all captures. Currently disabled — can be too aggressive. */
        if (0 && static_eval + 1150 < alpha && !in_check) {
            return alpha > best_score ? alpha : best_score;
        }

        if (best_score > alpha) {
            alpha = best_score;
        }
    } else {
        best_score = -SEARCH_INF;
    }

    movegen_generate_legal(pos, &legal_moves);
    if (legal_moves.count == 0) {
        return in_check ? (-SEARCH_MATE_SCORE + ply) : 0;
    }

    candidate_moves.count = 0;
    if (in_check) {
        candidate_moves = legal_moves;
    } else {
        for (index = 0; index < legal_moves.count; ++index) {
            Move move = legal_moves.moves[index];

            if (movorder_is_capture(move)) {
                candidate_moves.moves[candidate_moves.count++] = move;
            }
        }

        if (candidate_moves.count == 0) {
            return best_score;
        }
    }

    movorder_score_moves(pos, &candidate_moves, tt_move, 0, 0, 0, ctx->history, ctx->continuation_history, (PieceType) -1, -1, NULL, &ordered_moves);

    for (index = 0; index < ordered_moves.count; ++index) {
        Move move;
        int score;

        if (!movorder_pick_next(&ordered_moves, index, &move)) {
            continue;
        }

        if (!in_check &&
            movorder_is_capture(move) &&
            see_evaluate(pos, move) < 0) {
            continue;
        }

        if (!movegen_make_move(pos, move)) {
            continue;
        }

        if (ctx->use_nnue) {
            Piece moved_piece = position_get_piece(pos, move_target(move));
            nnue_push(ctx->nnue_state);
            nnue_update_accumulator_incremental(ctx->nnue_state, pos,
                moved_piece, move_source(move), move_target(move));
        }

        score = -search_quiescence(pos, ctx, -beta, -alpha, ply + 1);

        if (ctx->use_nnue) {
            nnue_pop(ctx->nnue_state);
        }
        movegen_unmake_move(pos);

        if (ctx->stop) {
            return 0;
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (score >= beta) {
            tt_store(pos->zobrist_hash, 0, score, static_eval, move, TT_FLAG_BETA, ply);
            return score;
        }
    }

    tt_store(
        pos->zobrist_hash,
        0,
        best_score,
        static_eval,
        best_move,
        best_score <= alpha_original ? TT_FLAG_ALPHA : TT_FLAG_EXACT,
        ply
    );

    return best_score;
}

static int search_negamax(
    Position *pos,
    SearchContext *ctx,
    int depth,
    int alpha,
    int beta,
    int ply,
    bool allow_null_move,
    int prev_static_eval
) {
    TTEntry entry;
    Move tt_move = 0;
    Move best_move = 0;
    MoveList legal_moves;
    OrderedMoveList ordered_moves;
    bool in_check;
    bool pv_node = (beta - alpha) > 1;
    int alpha_original = alpha;
    int static_eval;
    int best_score = -SEARCH_INF;
    bool first_move = true;
    size_t index;

    if (depth <= 0 || ply >= SEARCH_MAX_PLY - 1) {
        return search_quiescence(pos, ctx, alpha, beta, ply);
    }

    ctx->nodes++;
    search_check_time(ctx);
    if (ctx->stop) {
        return 0;
    }

    in_check = movegen_is_in_check(pos, (Color) pos->side_to_move);
    tt_prefetch(pos->zobrist_hash);
    if (draw_is_draw(pos)) {
        return draw_score(pos);
    }
    if (tt_probe(pos->zobrist_hash, &entry)) {
        int tt_score = tt_score_from_entry(&entry, ply);

        tt_move = entry.best_move;
        static_eval = entry.static_eval;

        if (entry.depth >= depth) {
            if (entry.flag == TT_FLAG_EXACT) {
                /* ---- df-PN Mate Verification ----
                   When the TT returns a near-mate score, verify it with
                   Proof-Number Search to avoid false mate claims.
                   Currently disabled — needs integration work. */
                if (0 && search_is_mate_score(tt_score) && depth >= 4 && !pv_node) {
                    MateSearchResult mr = mate_search(ctx->mate_ctx, pos, depth);
                    if (mr.disproven) {
                        /* Mate was disproven — reduce the score to a large positional advantage */
                        return tt_score > 0 ? SEARCH_MATE_BOUND - 100 : -(SEARCH_MATE_BOUND - 100);
                    }
                    if (mr.mate_found) {
                        /* Confirmed mate — return exact mate distance */
                        return mr.mate_distance > 0 ?
                            (SEARCH_MATE_SCORE - 2 * mr.mate_distance) :
                            -(SEARCH_MATE_SCORE - 2 * (-mr.mate_distance));
                    }
                }
                return tt_score;
            }
            if (entry.flag == TT_FLAG_ALPHA && tt_score <= alpha) {
                return tt_score;
            }
            if (entry.flag == TT_FLAG_BETA && tt_score >= beta) {
                return tt_score;
            }
        }
    } else {
        bool disagreement = false;
        static_eval = search_evaluate_combined(ctx, pos, &disagreement);

        /* Ensemble disagreement: extend search depth when NNUE and classical
           disagree significantly, catching positions where NNUE has blind spots */
        if (disagreement && depth >= 3 && !pv_node) {
            depth += 1;
        }
    }

    /* Improving: is our static eval better than 2 plies ago? */
    bool improving = ply > 0 && prev_static_eval != -SEARCH_INF && static_eval > prev_static_eval;

    /* ---- Fortress Detection ----
       If eval is stable across iterations and position has fortress features,
       apply a draw bias to prevent wasting time on unconvertible advantages.
       Currently disabled — needs more tuning. */
    if (0 && ply == 0 && search_detect_fortress(ctx, pos, static_eval)) {
        /* Push eval toward draw if we seem to be in a fortress */
        if (static_eval > 0) static_eval = static_eval * 3 / 4;
        else if (static_eval < 0) static_eval = static_eval * 3 / 4;
    }

    /* ---- Correction History: use as complexity proxy ----
       Large correction values indicate complex positions where the static eval
       is unreliable. Use this to adjust LMR and futility thresholds. */
    int corr_complexity = corrhist_total_correction(ctx->corrhist, pos);
    if (corr_complexity < 0) corr_complexity = -corr_complexity;

    if (search_options.enable_razoring &&
        !pv_node &&
        depth <= 3 &&
        !in_check &&
        alpha > -SEARCH_MATE_BOUND &&
        static_eval + search_razor_margin(depth) <= alpha) {
        int razor_score = search_quiescence(pos, ctx, alpha, beta, ply);

        if (ctx->stop) {
            return 0;
        }

        if (razor_score <= alpha) {
            return razor_score;
        }
    }

    /* Reverse Futility Pruning (Static Null Move Pruning):
       If static eval - margin >= beta at shallow depth and not in check,
       the position is so good that we can safely return beta.
       Use a larger margin on non-improving nodes (more likely to be overvalued). */
    if (search_options.enable_reverse_futility &&
        !pv_node &&
        depth <= 3 &&
        !in_check &&
        static_eval - ((80 + (improving ? 0 : 40)) * depth) >= beta &&
        beta < SEARCH_MATE_BOUND &&
        alpha > -SEARCH_MATE_BOUND &&
        search_has_non_pawn_material(pos, (Color) pos->side_to_move)) {
        return static_eval - ((80 + (improving ? 0 : 40)) * depth);
    }

    if (search_options.enable_null_move_pruning &&
        !pv_node &&
        allow_null_move &&
        ply > 0 &&
        depth >= 3 &&
        !in_check &&
        beta < SEARCH_MATE_BOUND &&
        static_eval >= beta &&
        search_has_non_pawn_material(pos, (Color) pos->side_to_move) &&
        search_total_non_king_pieces(pos) > 3 &&
        !search_is_pawn_only_endgame(pos)) {
        /* Dynamic null move reduction: base R increases with depth,
           additional reduction when eval is significantly above beta */
        int eval_margin = static_eval - beta;
        int reduction = 3 + (depth / 6) + (eval_margin > 200 ? 1 : 0) + (eval_margin > 600 ? 1 : 0);
        if (reduction > depth - 2) reduction = depth - 2;
        if (reduction < 2) reduction = 2;
        int null_depth = depth - reduction - 1;

        if (null_depth > 0 && movegen_make_null_move(pos)) {
            /* NNUE: null move doesn't change pieces, but we push/pop to maintain stack */
            if (ctx->use_nnue) nnue_push(ctx->nnue_state);

            int null_score = -search_negamax(pos, ctx, null_depth, -beta, -beta + 1, ply + 1, false, -static_eval);

            if (ctx->use_nnue) nnue_pop(ctx->nnue_state);
            movegen_unmake_null_move(pos);
            if (ctx->stop) {
                return 0;
            }

            if (null_score >= beta) {
                tt_store(pos->zobrist_hash, depth, null_score, static_eval, 0, TT_FLAG_BETA, ply);
                return null_score;
            }
        }
    }

    /* ProbCut: at non-PV nodes with sufficient depth, if static eval is significantly
       above beta, do a shallow verification search. If it still beats beta + margin,
       we can safely prune. */
    if (search_options.enable_probcut &&
        !pv_node &&
        depth >= 5 &&
        !in_check &&
        abs(beta) < SEARCH_MATE_BOUND) {
        int probcut_margin = depth * 40 + 100;
        int probcut_beta = beta + probcut_margin;

        if (static_eval >= probcut_beta) {
            int probcut_depth = depth - 4;
            if (probcut_depth < 1) probcut_depth = 1;

            /* Verify with a search at reduced depth with widened beta */
            int probcut_score = search_negamax(pos, ctx, probcut_depth, probcut_beta - 1, probcut_beta, ply, true, -SEARCH_INF);
            if (ctx->stop) return 0;

            if (probcut_score >= probcut_beta) {
                return probcut_score;
            }
        }
    }

    /* Internal Iterative Deepening: at PV nodes with no TT move, do a reduced-depth
       search first to discover a good move for ordering. */
    if (search_options.enable_iid &&
        pv_node &&
        tt_move == 0 &&
        depth >= 4) {
        SearchContext iid_ctx;
        int iid_depth = depth - 2;
        if (iid_depth < 1) iid_depth = 1;

        iid_ctx = *ctx;
        iid_ctx.stop = false;

        search_negamax(pos, &iid_ctx, iid_depth, alpha, beta, ply, true, -SEARCH_INF);
        if (!iid_ctx.stop) {
            TTEntry iid_entry;
            if (tt_probe(pos->zobrist_hash, &iid_entry) && iid_entry.best_move != 0) {
                tt_move = iid_entry.best_move;
            }
        }
    }

    movegen_generate_legal(pos, &legal_moves);
    if (legal_moves.count == 0) {
        return in_check ? (-SEARCH_MATE_SCORE + ply) : 0;
    }

    /* ---- Enhanced Transposition Cutoff (ETC) ----
       Before scoring moves, check if killer/countermove squares have TT entries
       with sufficient depth causing an immediate cutoff. */
    if (0 && !pv_node && depth >= 4 && !in_check) {
        Move etc_moves[2];
        int etc_count = 0;

        if (ply < SEARCH_MAX_PLY) {
            if (ctx->killers[ply][0] != 0) etc_moves[etc_count++] = ctx->killers[ply][0];
            if (ctx->killers[ply][1] != 0) etc_moves[etc_count++] = ctx->killers[ply][1];
        }

        for (int e = 0; e < etc_count; e++) {
            Move etc_move = etc_moves[e];
            TTEntry etc_entry;
            if (movegen_make_move(pos, etc_move)) {
                if (tt_probe(pos->zobrist_hash, &etc_entry) &&
                    etc_entry.depth >= depth - 1 &&
                    etc_entry.flag == TT_FLAG_BETA) {
                    int etc_score = tt_score_from_entry(&etc_entry, ply + 1);
                    if (etc_score >= beta) {
                        movegen_unmake_move(pos);
                        tt_store(pos->zobrist_hash, depth, etc_score, static_eval, etc_move, TT_FLAG_BETA, ply);
                        return etc_score;
                    }
                }
                movegen_unmake_move(pos);
            }
        }
    }

    /* ---- Syzygy Tablebase Probing ----
       If tablebases are available and the position has few enough pieces,
       use the exact tablebase result instead of searching.
       Currently disabled in search — tablebase probing needs full Fathom port.
       The API is in place for when proper TB data is loaded. */
    if (0 && syzygy_available(pos)) {
        SyzygyResult tb = syzygy_probe(pos);
        if (tb.found) {
            int tb_score;
            if (tb.wdl == SYZYGY_RESULT_WIN) {
                tb_score = SEARCH_MATE_SCORE - 2 * ply - 1;
            } else if (tb.wdl == SYZYGY_RESULT_LOSS) {
                tb_score = -(SEARCH_MATE_SCORE - 2 * ply - 1);
            } else {
                tb_score = 0; /* draw */
            }
            if (tb_score >= beta) {
                tt_store(pos->zobrist_hash, depth, tb_score, static_eval, 0, TT_FLAG_BETA, ply);
                return tb_score;
            }
            if (tb_score <= alpha) {
                tt_store(pos->zobrist_hash, depth, tb_score, static_eval, 0, TT_FLAG_ALPHA, ply);
                return tb_score;
            }
            /* Exact hit in PV node: use the tablebase score */
            if (pv_node) {
                best_score = tb_score;
                alpha = tb_score > alpha ? tb_score : alpha;
            }
        }
    }

    {
        Move countermove = 0;
        if (ctx->last_move != 0) {
            int from = move_source(ctx->last_move);
            int to = move_target(ctx->last_move);
            countermove = ctx->countermoves[from][to];
        }
        movorder_score_moves(
            pos,
            &legal_moves,
            tt_move,
            ctx->killers[ply][0],
            ctx->killers[ply][1],
            countermove,
            ctx->history,
            ctx->continuation_history,
            ctx->last_piece_type,
            ctx->last_target,
            NULL, /* policy scores computed separately in move loop */
            &ordered_moves
        );
    }

    /* Singular Extensions: check if the TT move is significantly better than alternatives */
    bool singular_move = false;
    if (search_options.enable_singular_extensions &&
        tt_move != 0 &&
        !pv_node &&
        depth >= 8 &&
        entry.depth >= depth - 3 &&
        entry.flag != TT_FLAG_ALPHA &&
        abs(entry.score) < SEARCH_MATE_BOUND) {
        int singular_beta = tt_score_from_entry(&entry, ply) - (2 * depth);
        int singular_depth = (depth - 1) / 2;
        if (singular_depth < 1) singular_depth = 1;

        /* Search with the TT move excluded — if no other move beats singular_beta,
           the TT move is singular */
        SearchContext singular_ctx;
        Move saved_last_move = ctx->last_move;

        singular_ctx = *ctx;
        singular_ctx.stop = false;
        ctx->last_move = 0;

        /* Generate legal moves excluding the TT move */
        {
            MoveList excl_moves;
            size_t excl_index;
            excl_moves.count = 0;
            movegen_generate_legal(pos, &excl_moves);
            for (excl_index = 0; excl_index < excl_moves.count; ++excl_index) {
                if (excl_moves.moves[excl_index] == tt_move) {
                    excl_moves.moves[excl_index] = excl_moves.moves[--excl_moves.count];
                    break;
                }
            }

            if (excl_moves.count > 0) {
                OrderedMoveList excl_ordered;
                movorder_score_moves(pos, &excl_moves, 0, ctx->killers[ply][0], ctx->killers[ply][1], 0, ctx->history, ctx->continuation_history, (PieceType) -1, -1, NULL, &excl_ordered);

                /* Search each excluded move to see if any beat singular_beta */
                bool move_is_singular = true;
                size_t excl_ord_index;
                for (excl_ord_index = 0; excl_ord_index < excl_ordered.count && move_is_singular; ++excl_ord_index) {
                    Move excl_move;
                    if (!movorder_pick_next(&excl_ordered, excl_ord_index, &excl_move)) {
                        continue;
                    }
                    if (!movegen_make_move(pos, excl_move)) {
                        continue;
                    }
                    {
                        int excl_score = -search_negamax(pos, &singular_ctx, singular_depth, -singular_beta - 1, -singular_beta, ply + 1, true, -SEARCH_INF);
                        movegen_unmake_move(pos);
                        if (singular_ctx.stop) {
                            move_is_singular = false;
                            break;
                        }
                        if (excl_score >= singular_beta) {
                            move_is_singular = false;
                        }
                    }
                }

                if (move_is_singular) {
                    singular_move = true;
                }
            }
        }

        ctx->last_move = saved_last_move;
        if (singular_ctx.stop) {
            return 0;
        }
    }

    /* ---- Multi-Cut Pruning ----
       At expected cut-nodes, if multiple moves fail high with reduced search,
       the node is "multi-cut" — the position is so good that many moves beat beta,
       so we can return beta early without searching all moves.
       Uses a separate move list to avoid consuming from the main ordered list.
       Currently disabled — too aggressive at low depths. */
    if (0 && !pv_node && depth >= 6 && !in_check) {
        const int MC_M = 6;    /* Check first M moves */
        const int MC_C = 3;    /* Number of cutoffs needed for multi-cut */
        const int MC_R = 3;    /* Depth reduction for multi-cut probe */
        int mc_cutoffs = 0;
        int mc_checked = 0;

        /* Use the legal_moves list directly (before ordering consumed it) */
        MoveList mc_moves = legal_moves;
        OrderedMoveList mc_ordered;
        Move countermove = 0;
        if (ctx->last_move != 0) {
            countermove = ctx->countermoves[move_source(ctx->last_move)][move_target(ctx->last_move)];
        }
        movorder_score_moves(pos, &mc_moves, tt_move,
                             ctx->killers[ply][0], ctx->killers[ply][1],
                             countermove, ctx->history, ctx->continuation_history,
                             ctx->last_piece_type, ctx->last_target, NULL, &mc_ordered);

        for (size_t mc_i = 0; mc_i < mc_ordered.count && mc_checked < MC_M; ++mc_i) {
            Move mc_move;
            if (!movorder_pick_next(&mc_ordered, mc_i, &mc_move)) continue;

            /* Only check quiet moves */
            if (movorder_is_capture(mc_move)) {
                mc_checked++;
                continue;
            }

            if (!movegen_make_move(pos, mc_move)) continue;

            if (ctx->use_nnue) {
                nnue_push(ctx->nnue_state);
                nnue_update_accumulator_incremental(ctx->nnue_state, pos,
                    position_get_piece(pos, move_target(mc_move)),
                    move_source(mc_move), move_target(mc_move));
            }

            int mc_depth = depth - 1 - MC_R;
            if (mc_depth < 1) mc_depth = 1;
            int mc_score = -search_negamax(pos, ctx, mc_depth, -beta, -beta + 1, ply + 1, true, -static_eval);

            if (ctx->use_nnue) nnue_pop(ctx->nnue_state);
            movegen_unmake_move(pos);

            if (ctx->stop) return 0;

            if (mc_score >= beta) {
                mc_cutoffs++;
                if (mc_cutoffs >= MC_C) {
                    tt_store(pos->zobrist_hash, depth, beta, static_eval, mc_move, TT_FLAG_BETA, ply);
                    return beta;
                }
            }
            mc_checked++;
        }
    }

    for (index = 0; index < ordered_moves.count; ++index) {
        Move move;
        int score;
        int child_depth;
        int reduction = 0;
        int move_number = (int) index + 1;
        bool quiet_move;
        bool gives_check;
        bool extend = false;
        Color side_to_move = (Color) pos->side_to_move;

        if (!movorder_pick_next(&ordered_moves, index, &move)) {
            continue;
        }

        quiet_move = search_is_quiet_move(move);
        if (!movegen_make_move(pos, move)) {
            continue;
        }

        /* Update NNUE accumulator stack for this move */
        if (ctx->use_nnue) {
            nnue_push(ctx->nnue_state);
            nnue_update_accumulator_incremental(ctx->nnue_state, pos,
                position_get_piece(pos, move_target(move)),
                move_source(move), move_target(move));
        }

        gives_check = movegen_is_in_check(pos, (Color) pos->side_to_move);

        if (movegen_is_stalemate(pos)) {
            score = 0;
            if (static_eval >= SEARCH_STALEMATE_MARGIN) {
                score = -SEARCH_STALEMATE_BIAS;
            } else if (static_eval <= -SEARCH_STALEMATE_MARGIN) {
                score = SEARCH_STALEMATE_BIAS;
            }

            if (ctx->use_nnue) nnue_pop(ctx->nnue_state);
            movegen_unmake_move(pos);

            if (score > best_score) {
                best_score = score;
                best_move = move;
            }

            if (score > alpha) {
                alpha = score;
            }

            if (score >= beta) {
                search_store_killer(ctx, ply, move);
                search_store_countermove(ctx, move);
                search_store_history(ctx, side_to_move, move, depth);
                if (search_is_quiet_move(move)) {
                    search_store_continuation_history(ctx, ctx->last_piece_type, ctx->last_target,
                        piece_type(position_get_piece(pos, move_source(move))), move_target(move),
                        depth * depth);
                }
                tt_store(pos->zobrist_hash, depth, score, static_eval, move, TT_FLAG_BETA, ply);
                return score;
            }

            continue;
        }

        if (search_options.enable_futility_pruning &&
            !pv_node &&
            depth <= 3 &&
            !in_check &&
            quiet_move &&
            !gives_check &&
            static_eval + (150 * depth) + (corr_complexity > 50 ? 50 : 0) <= alpha) {
            if (ctx->use_nnue) nnue_pop(ctx->nnue_state);
            movegen_unmake_move(pos);
            continue;
        }

        /* Late Move Pruning: at shallow depths, skip quiet moves late in the move list.
           Reduce pruning on improving nodes since we're less likely to be in Zugzwang. */
        if (search_options.enable_lmp &&
            !pv_node &&
            depth <= 3 &&
            !in_check &&
            quiet_move &&
            !gives_check &&
            move_number > (3 + depth * depth + (improving ? 3 : 0))) {
            if (ctx->use_nnue) nnue_pop(ctx->nnue_state);
            movegen_unmake_move(pos);
            continue;
        }

        /* Singular extension: extend the TT move if it's singular */
        if (singular_move && move == tt_move) {
            extend = true;
        }

        child_depth = depth - 1 + (search_options.enable_check_extensions && gives_check ? 1 : 0) + (extend ? 1 : 0);
        if (child_depth < 0) {
            child_depth = 0;
        }

        if (search_options.enable_lmr &&
            ply > 0 &&
            !pv_node &&
            !first_move &&
            !in_check &&
            depth >= 3 &&
            move_number > 3 &&
            quiet_move &&
            !gives_check &&
            child_depth > 1) {
            int depth_index = depth > SEARCH_MAX_DEPTH ? SEARCH_MAX_DEPTH : depth;
            int move_index = move_number > MOVEGEN_MAX_MOVES ? MOVEGEN_MAX_MOVES : move_number;

            reduction = search_lmr_table[depth_index][move_index];
            /* Apply adaptive LMR multiplier from position classification */
            if (ctx->adapt_initialized) {
                reduction = (reduction * ctx->adapt_params.lmr_multiplier) / 100;
            }
            if (reduction > child_depth - 1) {
                reduction = child_depth - 1;
            }
        }

        if (first_move) {
            Move prev_move = ctx->last_move;
            PieceType prev_pt = ctx->last_piece_type;
            int prev_tgt = ctx->last_target;
            ctx->last_move = move;
            ctx->last_piece_type = piece_type(position_get_piece(pos, move_source(move)));
            ctx->last_target = move_target(move);
            score = -search_negamax(pos, ctx, child_depth, -beta, -alpha, ply + 1, true, -static_eval);
            ctx->last_move = prev_move;
            ctx->last_piece_type = prev_pt;
            ctx->last_target = prev_tgt;
            first_move = false;
        } else {
            int reduced_depth = child_depth - reduction;

            if (reduced_depth < 0) {
                reduced_depth = 0;
            }

            {
                Move prev_move = ctx->last_move;
                PieceType prev_pt = ctx->last_piece_type;
                int prev_tgt = ctx->last_target;
                ctx->last_move = move;
                ctx->last_piece_type = piece_type(position_get_piece(pos, move_source(move)));
                ctx->last_target = move_target(move);
                score = -search_negamax(pos, ctx, reduced_depth, -alpha - 1, -alpha, ply + 1, true, -static_eval);
                if (!ctx->stop && ((reduction > 0 && score > alpha - 64) || (reduction == 0 && score > alpha))) {
                    score = -search_negamax(pos, ctx, child_depth, -beta, -alpha, ply + 1, true, -static_eval);
                }
                ctx->last_move = prev_move;
                ctx->last_piece_type = prev_pt;
                ctx->last_target = prev_tgt;
            }
        }

        if (ctx->use_nnue) nnue_pop(ctx->nnue_state);
        movegen_unmake_move(pos);
        if (ctx->stop) {
            return 0;
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (score >= beta) {
            search_store_killer(ctx, ply, move);
            search_store_countermove(ctx, move);
            search_store_history(ctx, side_to_move, move, depth);
            if (search_is_quiet_move(move)) {
                search_store_continuation_history(ctx, ctx->last_piece_type, ctx->last_target,
                    piece_type(position_get_piece(pos, move_source(move))), move_target(move),
                    depth * depth);
            }
            tt_store(pos->zobrist_hash, depth, score, static_eval, move, TT_FLAG_BETA, ply);
            return score;
        }
    }

    tt_store(
        pos->zobrist_hash,
        depth,
        best_score,
        static_eval,
        best_move,
        best_score <= alpha_original ? TT_FLAG_ALPHA : TT_FLAG_EXACT,
        ply
    );

    /* ---- Correction History Update ----
       Update correction history when: !in_check, best move is quiet or none,
       score conditions met (not contradictory bounds). */
    if (!in_check &&
        (!best_move || search_is_quiet_move(best_move)) &&
        !(best_score >= beta && best_score <= static_eval) &&
        !(!best_move && best_score >= static_eval)) {
        corrhist_update(ctx->corrhist, pos, depth, static_eval, best_score,
                        (int)ctx->last_piece_type, ctx->last_target,
                        best_move ? (int)piece_type(position_get_piece(pos, move_source(best_move))) : 0,
                        best_move ? move_target(best_move) : 0);
    }

    /* ---- History-LM: update move feature weights ----
       Track which features the best move had, to bias future move ordering. */
    if (best_move) {
        Color side = (Color)pos->side_to_move;
        int feat_idx = 0;
        if (movorder_is_capture(best_move)) feat_idx = 1;
        else if (gives_check_estimated(pos, best_move)) feat_idx = 2;
        else if (move_promotion_piece(best_move) != MOVE_PROMOTION_NONE) feat_idx = 3;
        else {
            PieceType pt = piece_type(position_get_piece(pos, move_source(best_move)));
            feat_idx = 4 + (pt < KNIGHT ? 0 : (pt < ROOK ? 1 : 2));
        }
        ctx->move_feature_hist[side][feat_idx] += depth * depth;
        /* Gravity: decay old values */
        for (int f = 0; f < 8; f++) {
            ctx->move_feature_hist[side][f] = (ctx->move_feature_hist[side][f] * 7) / 8;
        }
    }

    return best_score;
}

int search_extract_pv(Position *pos, int max_depth, Move *pv_out, int pv_capacity) {
    Position temp;
    int length = 0;

    if (pos == NULL || pv_out == NULL || pv_capacity <= 0) {
        return 0;
    }

    temp = *pos;
    while (length < max_depth && length < pv_capacity) {
        TTEntry entry;

        if (!tt_probe(temp.zobrist_hash, &entry) || entry.best_move == 0) {
            break;
        }

        if (!search_move_is_legal(&temp, entry.best_move)) {
            break;
        }

        pv_out[length++] = entry.best_move;
        if (!movegen_make_move(&temp, entry.best_move)) {
            break;
        }
    }

    return length;
}

/* ---- Lazy SMP Multi-threading ---- */

typedef struct SearchThreadData {
    Position pos;
    SearchContext ctx;
    SearchResult result;
    int max_depth;
    int time_limit_ms;
    int thread_id;
    bool finished;
    bool is_main;
} SearchThreadData;

static volatile bool smp_stop_flag = false;

static void *search_thread_worker(void *arg) {
    SearchThreadData *data = (SearchThreadData *) arg;
    SearchResult *result = &data->result;
    int previous_score = 0;
    bool have_previous_score = false;
    int depth;
    int start_depth = data->thread_id == 0 ? 1 : 1 + data->thread_id;

    /* search_ctx_init already called in search_iterative_deepening */
    memset(result, 0, sizeof(*result));

    /* Initialize NNUE state for this search (heap-allocated to avoid stack overflow) */
    if (nnue_is_loaded()) {
        data->ctx.nnue_state = nnue_create_state();
        if (data->ctx.nnue_state != NULL) {
            nnue_state_init(data->ctx.nnue_state, &data->pos);
            data->ctx.use_nnue = true;
        } else {
            data->ctx.use_nnue = false;
        }
    } else {
        data->ctx.use_nnue = false;
    }

    /* Initialize adaptive search parameters via policy classification */
    if (policy_is_loaded() && data->ctx.use_nnue && data->ctx.nnue_state != NULL) {
        int pos_class = policy_classify_position(
            &data->ctx.nnue_state->acc_stack[data->ctx.nnue_state->current],
            &data->pos
        );
        if (pos_class >= 0) {
            data->ctx.adapt_params = policy_get_adapt_params(pos_class);
            data->ctx.adapt_initialized = true;
        }
    }

    time_start(&data->ctx.timer, data->time_limit_ms);

    /* Adaptive time management: set optimum and maximum time bounds */
    if (data->is_main && data->time_limit_ms > 0) {
        int total = data->time_limit_ms;
        data->ctx.timer.optimum_ms = total * 60 / 100;  /* 60% of allocated time */
        data->ctx.timer.maximum_ms = total * 95 / 100;   /* 95% hard limit */
        data->ctx.timer.limit_ms = total;
    }

    Move prev_best_move = 0;
    int stable_count = 0;

    for (depth = start_depth; depth <= data->max_depth; ++depth) {
        Move pv[SEARCH_MAX_PLY];
        uint64_t previous_nodes = data->ctx.nodes + data->ctx.qnodes;
        int pv_length;
        int score;

        /* Check stop flag from other threads */
        if (smp_stop_flag) {
            data->ctx.stop = true;
        }

        if (search_options.enable_aspiration_windows && have_previous_score && depth >= 4) {
            const int aspiration_margins[] = { 50, 200 };
            bool resolved = false;
            size_t margin_index;

            score = previous_score;
            for (margin_index = 0; margin_index < sizeof(aspiration_margins) / sizeof(aspiration_margins[0]); ++margin_index) {
                int alpha = previous_score - aspiration_margins[margin_index];
                int beta = previous_score + aspiration_margins[margin_index];

                score = search_negamax(&data->pos, &data->ctx, depth, alpha, beta, 0, true, -SEARCH_INF);
                if (data->ctx.stop || smp_stop_flag) {
                    data->ctx.stop = true;
                    break;
                }

                if (score > alpha && score < beta) {
                    resolved = true;
                    break;
                }
            }

            if (!data->ctx.stop && !smp_stop_flag && !resolved) {
                score = search_negamax(&data->pos, &data->ctx, depth, -SEARCH_INF, SEARCH_INF, 0, true, -SEARCH_INF);
            }
        } else {
            score = search_negamax(&data->pos, &data->ctx, depth, -SEARCH_INF, SEARCH_INF, 0, true, -SEARCH_INF);
        }

        if (data->ctx.stop || smp_stop_flag) {
            break;
        }

        pv_length = search_extract_pv(&data->pos, depth, pv, SEARCH_MAX_PLY);

        result->score = score;
        result->completed_depth = depth;
        result->best_move = pv_length > 0 ? pv[0] : tt_probe_move(data->pos.zobrist_hash);
        result->nodes = data->ctx.nodes;
        result->qnodes = data->ctx.qnodes;
        result->elapsed_ms = search_elapsed_ms(&data->ctx);
        result->iteration_count = depth;
        search_copy_pv(result->pv, &result->pv_length, pv, pv_length);

        result->iterations[depth - 1].depth = depth;
        result->iterations[depth - 1].score = score;
        result->iterations[depth - 1].nodes = (data->ctx.nodes + data->ctx.qnodes) - previous_nodes;
        result->iterations[depth - 1].best_move = result->best_move;
        search_copy_pv(
            result->iterations[depth - 1].pv,
            &result->iterations[depth - 1].pv_length,
            pv,
            pv_length
        );

        previous_score = score;
        have_previous_score = true;

        /* Adaptive time management on main thread */
        if (data->is_main && data->time_limit_ms > 0) {
            Move current_best = result->best_move;

            if (current_best == prev_best_move) {
                stable_count++;
                /* Best move stable for 3+ iterations: easy move, reduce time */
                if (stable_count >= 3 && depth >= 5) {
                    data->ctx.timer.easy_move = true;
                    data->ctx.timer.optimum_ms = data->time_limit_ms * 35 / 100;
                }
            } else {
                /* Best move changed: unstable, increase time */
                stable_count = 0;
                data->ctx.timer.unstable_pv = true;
                data->ctx.timer.optimum_ms = data->time_limit_ms * 75 / 100;
            }
            prev_best_move = current_best;
        }

        /* Main thread signals stop to helpers when time is up or depth reached */
        if (data->is_main && time_is_expired(&data->ctx.timer)) {
            smp_stop_flag = true;
            break;
        }
    }

    result->stopped = data->ctx.stop || smp_stop_flag;
    result->elapsed_ms = search_elapsed_ms(&data->ctx);
    data->finished = true;

    return NULL;
}

bool search_iterative_deepening(Position *pos, int max_depth, int time_limit_ms, SearchResult *out_result) {
    SearchThreadData *threads = (SearchThreadData *)calloc(search_num_threads, sizeof(SearchThreadData));
    if (threads == NULL) {
        if (out_result != NULL) memset(out_result, 0, sizeof(*out_result));
        return false;
    }
#ifndef __EMSCRIPTEN__
    pthread_t thread_handles[SEARCH_MAX_THREADS];
#endif
    int num_threads;
    int i;
    SearchResult best_result;

    if (pos == NULL || max_depth <= 0) {
        return false;
    }

    search_init();

    if (max_depth > SEARCH_MAX_DEPTH) {
        max_depth = SEARCH_MAX_DEPTH;
    }

    num_threads = search_num_threads;
    if (num_threads < 1) num_threads = 1;
    if (num_threads > SEARCH_MAX_THREADS) num_threads = SEARCH_MAX_THREADS;

    tt_new_search();
    smp_stop_flag = false;

    /* Initialize all thread data */
    for (i = 0; i < num_threads; ++i) {
        threads[i].pos = *pos;
        threads[i].max_depth = max_depth;
        threads[i].time_limit_ms = time_limit_ms;
        threads[i].thread_id = i;
        threads[i].finished = false;
        threads[i].is_main = (i == 0);
        search_ctx_init(&threads[i].ctx);
        memset(&threads[i].result, 0, sizeof(threads[i].result));
    }

#ifdef __EMSCRIPTEN__
    /* WASM: single-threaded fallback */
    search_thread_worker(&threads[0]);
#else
    /* Native: use pthreads for Lazy SMP */
    if (num_threads == 1) {
        search_thread_worker(&threads[0]);
    } else {
        /* Launch helper threads first */
        for (i = 1; i < num_threads; ++i) {
            pthread_create(&thread_handles[i], NULL, search_thread_worker, &threads[i]);
        }

        /* Run main thread (thread 0) in the current thread */
        search_thread_worker(&threads[0]);

        /* Signal stop to any remaining helpers */
        smp_stop_flag = true;

        /* Wait for all helpers to finish */
        for (i = 1; i < num_threads; ++i) {
            pthread_join(thread_handles[i], NULL);
        }
    }
#endif

    /* Collect the best result from all threads.
       Prefer the deepest completed depth, then best score. */
    best_result = threads[0].result;
    for (i = 1; i < num_threads; ++i) {
        if (threads[i].result.completed_depth > best_result.completed_depth ||
            (threads[i].result.completed_depth == best_result.completed_depth &&
             threads[i].result.score > best_result.score)) {
            best_result = threads[i].result;
        }
    }

    /* Aggregate node counts from all threads */
    {
        uint64_t total_nodes = 0;
        uint64_t total_qnodes = 0;
        for (i = 0; i < num_threads; ++i) {
            total_nodes += threads[i].result.nodes;
            total_qnodes += threads[i].result.qnodes;
        }
        best_result.nodes = total_nodes;
        best_result.qnodes = total_qnodes;
    }

    /* Cleanup per-thread search contexts */
    for (i = 0; i < num_threads; ++i) {
        search_ctx_cleanup(&threads[i].ctx);
    }

    free(threads);

    if (out_result != NULL) {
        *out_result = best_result;
    }

    return best_result.completed_depth > 0;
}

void search_set_threads(int threads) {
    if (threads < 1) threads = 1;
    if (threads > SEARCH_MAX_THREADS) threads = SEARCH_MAX_THREADS;
    search_num_threads = threads;
}

int search_get_threads(void) {
    return search_num_threads;
}
