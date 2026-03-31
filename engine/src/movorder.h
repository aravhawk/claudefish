#ifndef CLAUDEFISH_MOVORDER_H
#define CLAUDEFISH_MOVORDER_H

#include <stdbool.h>

#include "movegen.h"

typedef struct OrderedMove {
    Move move;
    int score;
} OrderedMove;

typedef struct OrderedMoveList {
    OrderedMove entries[MOVEGEN_MAX_MOVES];
    size_t count;
} OrderedMoveList;

void movorder_score_moves(
    const Position *pos,
    const MoveList *moves,
    Move tt_move,
    Move killer_one,
    Move killer_two,
    const int history[2][BOARD_SQUARES][BOARD_SQUARES],
    OrderedMoveList *ordered
);
bool movorder_pick_next(OrderedMoveList *ordered, size_t index, Move *out_move);
bool movorder_is_capture(Move move);
int movorder_estimate_gain(Move move);

#endif
