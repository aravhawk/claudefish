#include "search.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "evaluate.h"
#include "movorder.h"
#include "tt.h"

typedef struct SearchContext {
    uint64_t nodes;
    uint64_t qnodes;
    clock_t start_clock;
    int time_limit_ms;
    bool stop;
    Move killers[SEARCH_MAX_PLY][2];
    int history[2][BOARD_SQUARES][BOARD_SQUARES];
} SearchContext;

static const SearchOptions search_default_options = {
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
    true
};
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
    return ((double) (clock() - ctx->start_clock) * 1000.0) / (double) CLOCKS_PER_SEC;
}

static void search_check_time(SearchContext *ctx) {
    uint64_t visited;

    if (ctx == NULL || ctx->stop || ctx->time_limit_ms <= 0) {
        return;
    }

    visited = ctx->nodes + ctx->qnodes;
    if ((visited & 1023ULL) != 0) {
        return;
    }

    if (search_elapsed_ms(ctx) >= (double) ctx->time_limit_ms) {
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

    ctx->history[side][source][target] += bonus;
    if (ctx->history[side][source][target] > 2000000) {
        ctx->history[side][source][target] /= 2;
    }
}

static bool search_is_quiet_move(Move move) {
    return !movorder_is_capture(move) && move_promotion_piece(move) == MOVE_PROMOTION_NONE;
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

void search_init(void) {
    movegen_init();
    eval_init();
    tt_init();

    if (!search_initialized) {
        search_init_lmr_table();
        search_initialized = true;
    }
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

    in_check = movegen_is_in_check(pos, (Color) pos->side_to_move);
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
        static_eval = eval_evaluate(pos);
    }

    if (!in_check) {
        best_score = static_eval;
        if (best_score >= beta) {
            return best_score;
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

    movorder_score_moves(pos, &candidate_moves, tt_move, 0, 0, ctx->history, &ordered_moves);

    for (index = 0; index < ordered_moves.count; ++index) {
        Move move;
        int score;

        if (!movorder_pick_next(&ordered_moves, index, &move)) {
            continue;
        }

        if (!in_check &&
            movorder_is_capture(move) &&
            static_eval + movorder_estimate_gain(move) + SEARCH_DELTA_MARGIN < alpha) {
            continue;
        }

        if (!movegen_make_move(pos, move)) {
            continue;
        }

        score = -search_quiescence(pos, ctx, -beta, -alpha, ply + 1);
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
    bool allow_null_move
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
    if (tt_probe(pos->zobrist_hash, &entry)) {
        int tt_score = tt_score_from_entry(&entry, ply);

        tt_move = entry.best_move;
        static_eval = entry.static_eval;

        if (entry.depth >= depth) {
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
        static_eval = eval_evaluate(pos);
    }

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
        int reduction = depth >= 6 ? 3 : 2;
        int null_depth = depth - reduction - 1;

        if (null_depth > 0 && movegen_make_null_move(pos)) {
            int null_score = -search_negamax(pos, ctx, null_depth, -beta, -beta + 1, ply + 1, false);

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

    movegen_generate_legal(pos, &legal_moves);
    if (legal_moves.count == 0) {
        return in_check ? (-SEARCH_MATE_SCORE + ply) : 0;
    }

    movorder_score_moves(
        pos,
        &legal_moves,
        tt_move,
        ctx->killers[ply][0],
        ctx->killers[ply][1],
        ctx->history,
        &ordered_moves
    );

    for (index = 0; index < ordered_moves.count; ++index) {
        Move move;
        int score;
        int child_depth;
        int reduction = 0;
        int move_number = (int) index + 1;
        bool quiet_move;
        bool gives_check;
        Color side_to_move = (Color) pos->side_to_move;

        if (!movorder_pick_next(&ordered_moves, index, &move)) {
            continue;
        }

        quiet_move = search_is_quiet_move(move);
        if (!movegen_make_move(pos, move)) {
            continue;
        }

        gives_check = movegen_is_in_check(pos, (Color) pos->side_to_move);

        if (movegen_is_stalemate(pos)) {
            score = 0;
            if (static_eval >= SEARCH_STALEMATE_MARGIN) {
                score = -SEARCH_STALEMATE_BIAS;
            } else if (static_eval <= -SEARCH_STALEMATE_MARGIN) {
                score = SEARCH_STALEMATE_BIAS;
            }

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
                search_store_history(ctx, side_to_move, move, depth);
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
            static_eval + (150 * depth) <= alpha) {
            movegen_unmake_move(pos);
            continue;
        }

        child_depth = depth - 1 + (search_options.enable_check_extensions && gives_check ? 1 : 0);
        if (child_depth < 0) {
            child_depth = 0;
        }

        if (search_options.enable_lmr &&
            ply > 0 &&
            !pv_node &&
            !first_move &&
            !in_check &&
            depth >= 5 &&
            move_number > 3 &&
            quiet_move &&
            !gives_check &&
            child_depth > 1) {
            int depth_index = depth > SEARCH_MAX_DEPTH ? SEARCH_MAX_DEPTH : depth;
            int move_index = move_number > MOVEGEN_MAX_MOVES ? MOVEGEN_MAX_MOVES : move_number;

            reduction = search_lmr_table[depth_index][move_index];
            if (reduction > child_depth - 1) {
                reduction = child_depth - 1;
            }
        }

        if (first_move) {
            score = -search_negamax(pos, ctx, child_depth, -beta, -alpha, ply + 1, true);
            first_move = false;
        } else {
            int reduced_depth = child_depth - reduction;

            if (reduced_depth < 0) {
                reduced_depth = 0;
            }

            score = -search_negamax(pos, ctx, reduced_depth, -alpha - 1, -alpha, ply + 1, true);
            if (!ctx->stop && ((reduction > 0 && score > alpha - 64) || (reduction == 0 && score > alpha))) {
                score = -search_negamax(pos, ctx, child_depth, -beta, -alpha, ply + 1, true);
            }
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
            search_store_killer(ctx, ply, move);
            search_store_history(ctx, side_to_move, move, depth);
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

bool search_iterative_deepening(Position *pos, int max_depth, int time_limit_ms, SearchResult *out_result) {
    SearchContext ctx;
    SearchResult result;
    int previous_score = 0;
    bool have_previous_score = false;
    int depth;

    if (pos == NULL || max_depth <= 0) {
        return false;
    }

    search_init();
    memset(&ctx, 0, sizeof(ctx));
    memset(&result, 0, sizeof(result));

    if (max_depth > SEARCH_MAX_DEPTH) {
        max_depth = SEARCH_MAX_DEPTH;
    }

    ctx.start_clock = clock();
    ctx.time_limit_ms = time_limit_ms;
    tt_new_search();

    for (depth = 1; depth <= max_depth; ++depth) {
        Move pv[SEARCH_MAX_PLY];
        uint64_t previous_nodes = ctx.nodes + ctx.qnodes;
        int pv_length;
        int score;

        if (search_options.enable_aspiration_windows && have_previous_score && depth >= 5) {
            int alpha = previous_score - 50;
            int beta = previous_score + 50;

            score = search_negamax(pos, &ctx, depth, alpha, beta, 0, true);
            if (!ctx.stop && (score <= alpha || score >= beta)) {
                alpha = previous_score - 200;
                beta = previous_score + 200;
                score = search_negamax(pos, &ctx, depth, alpha, beta, 0, true);
                if (!ctx.stop) {
                    score = search_negamax(pos, &ctx, depth, -SEARCH_INF, SEARCH_INF, 0, true);
                }
            }
        } else {
            score = search_negamax(pos, &ctx, depth, -SEARCH_INF, SEARCH_INF, 0, true);
        }

        if (ctx.stop) {
            break;
        }

        pv_length = search_extract_pv(pos, depth, pv, SEARCH_MAX_PLY);

        result.score = score;
        result.completed_depth = depth;
        result.best_move = pv_length > 0 ? pv[0] : tt_probe_move(pos->zobrist_hash);
        result.nodes = ctx.nodes;
        result.qnodes = ctx.qnodes;
        result.elapsed_ms = search_elapsed_ms(&ctx);
        result.iteration_count = depth;
        search_copy_pv(result.pv, &result.pv_length, pv, pv_length);

        result.iterations[depth - 1].depth = depth;
        result.iterations[depth - 1].score = score;
        result.iterations[depth - 1].nodes = (ctx.nodes + ctx.qnodes) - previous_nodes;
        result.iterations[depth - 1].best_move = result.best_move;
        search_copy_pv(
            result.iterations[depth - 1].pv,
            &result.iterations[depth - 1].pv_length,
            pv,
            pv_length
        );

        previous_score = score;
        have_previous_score = true;
    }

    result.stopped = ctx.stop;
    result.elapsed_ms = search_elapsed_ms(&ctx);

    if (out_result != NULL) {
        *out_result = result;
    }

    return result.completed_depth > 0;
}
