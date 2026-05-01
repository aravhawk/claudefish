#include "movorder.h"

#include "see.h"

static const int move_order_piece_values[PIECE_TYPE_NB] = {
    100,
    320,
    330,
    500,
    950,
    0
};

static int movorder_piece_value(Piece piece) {
    int type = piece_type(piece);

    return type >= 0 ? move_order_piece_values[type] : 0;
}

bool movorder_is_capture(Move move) {
    return (move_flags(move) & MOVE_FLAG_CAPTURE) != 0;
}

int movorder_estimate_gain(Move move) {
    int gain = movorder_piece_value(move_captured_piece(move));
    PieceType promotion_piece = move_promotion_piece(move);

    if (promotion_piece != MOVE_PROMOTION_NONE) {
        gain += move_order_piece_values[promotion_piece] - move_order_piece_values[PAWN];
    }

    return gain;
}

static int movorder_score_move(
    const Position *pos,
    Move move,
    Move tt_move,
    Move killer_one,
    Move killer_two,
    Move countermove,
    const int history[2][BOARD_SQUARES][BOARD_SQUARES]
) {
    Piece moving_piece;
    int source;
    int target;

    if (move == tt_move) {
        return 2000000;
    }

    source = move_source(move);
    target = move_target(move);
    moving_piece = position_get_piece(pos, source);

    if (movorder_is_capture(move)) {
        int see_score = see_evaluate(pos, move);

        if (see_score >= 0) {
            return 1000000 + 50000 + see_score;
        }

        return -100000 + see_score;
    }

    if (move == killer_one) {
        return 900000;
    }

    if (move == killer_two) {
        return 800000;
    }

    if (move == countermove) {
        return 700000;
    }

    if (history != NULL) {
        return history[pos->side_to_move][source][target];
    }

    return 0;
}

void movorder_score_moves(
    const Position *pos,
    const MoveList *moves,
    Move tt_move,
    Move killer_one,
    Move killer_two,
    Move countermove,
    const int history[2][BOARD_SQUARES][BOARD_SQUARES],
    OrderedMoveList *ordered
) {
    size_t index;

    if (ordered == NULL) {
        return;
    }

    ordered->count = 0;

    if (pos == NULL || moves == NULL) {
        return;
    }

    ordered->count = moves->count;
    for (index = 0; index < moves->count; ++index) {
        ordered->entries[index].move = moves->moves[index];
        ordered->entries[index].score = movorder_score_move(pos, moves->moves[index], tt_move, killer_one, killer_two, countermove, history);
    }
}

bool movorder_pick_next(OrderedMoveList *ordered, size_t index, Move *out_move) {
    size_t best_index;
    size_t current_index;
    OrderedMove swap;

    if (ordered == NULL || out_move == NULL || index >= ordered->count) {
        return false;
    }

    best_index = index;
    for (current_index = index + 1; current_index < ordered->count; ++current_index) {
        if (ordered->entries[current_index].score > ordered->entries[best_index].score) {
            best_index = current_index;
        }
    }

    if (best_index != index) {
        swap = ordered->entries[index];
        ordered->entries[index] = ordered->entries[best_index];
        ordered->entries[best_index] = swap;
    }

    *out_move = ordered->entries[index].move;
    return true;
}
