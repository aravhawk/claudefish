#ifndef CLAUDEFISH_EVALUATE_H
#define CLAUDEFISH_EVALUATE_H

#include "movegen.h"

enum {
    EVAL_MATE_SCORE = 30000,
    EVAL_TEMPO_BONUS = 10
};

typedef struct EvalPawnHashStats {
    uint64_t probes;
    uint64_t hits;
    uint64_t stores;
} EvalPawnHashStats;

void eval_init(void);
void eval_update_piece_square_state(Position *pos, Piece piece, int square, int delta);
void eval_refresh_position_state(Position *pos);
int eval_calculate_phase(const Position *pos);
int eval_evaluate(Position *pos);
void eval_reset_pawn_hash_table(void);
void eval_reset_pawn_hash_stats(void);
EvalPawnHashStats eval_get_pawn_hash_stats(void);

#endif
