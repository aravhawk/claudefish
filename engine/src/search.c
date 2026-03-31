#include "search.h"

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
}

void search_reset_heuristics(void) {
    tt_clear();
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

static int search_negamax(Position *pos, SearchContext *ctx, int depth, int alpha, int beta, int ply) {
    TTEntry entry;
    Move tt_move = 0;
    Move best_move = 0;
    MoveList legal_moves;
    OrderedMoveList ordered_moves;
    bool in_check;
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
        Color side_to_move = (Color) pos->side_to_move;

        if (!movorder_pick_next(&ordered_moves, index, &move)) {
            continue;
        }

        if (!movegen_make_move(pos, move)) {
            continue;
        }

        if (first_move) {
            score = -search_negamax(pos, ctx, depth - 1, -beta, -alpha, ply + 1);
            first_move = false;
        } else {
            score = -search_negamax(pos, ctx, depth - 1, -alpha - 1, -alpha, ply + 1);
            if (!ctx->stop && score > alpha && score < beta) {
                score = -search_negamax(pos, ctx, depth - 1, -beta, -alpha, ply + 1);
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
        int score = search_negamax(pos, &ctx, depth, -SEARCH_INF, SEARCH_INF, 0);

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
    }

    result.stopped = ctx.stop;
    result.elapsed_ms = search_elapsed_ms(&ctx);

    if (out_result != NULL) {
        *out_result = result;
    }

    return result.completed_depth > 0;
}
