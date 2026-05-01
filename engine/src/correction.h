#ifndef CLAUDEFISH_CORRECTION_H
#define CLAUDEFISH_CORRECTION_H

#include <stdint.h>
#include "types.h"

struct Position;

enum {
    CORRHIST_SIZE = 16384,
    CORRHIST_GRAIN = 16,
    CORRHIST_MAX = 16384,
    CORRHIST_WEIGHT_SCALE = 512,
    CORRHIST_LIMIT = 800,

    /* Non-pawn correction uses king + non-pawn piece positions */
    CORRHIST_NONPAWN_SIZE = 8192,

    /* Continuation correction: [prev_piece_type][prev_target][piece_type][target] */
    CORRHIST_CONT_SIZE = PIECE_TYPE_NB * BOARD_SQUARES * PIECE_TYPE_NB * BOARD_SQUARES,
    CORRHIST_CONT_MAX = 8192
};

typedef struct CorrectionHistory {
    /* Pawn structure correction: indexed by pawn hash */
    int16_t pawn_corrhist[2][CORRHIST_SIZE];

    /* Material configuration correction: indexed by material hash */
    int16_t material_corrhist[2][CORRHIST_SIZE];

    /* Threat correction: indexed by hash of capturable pieces */
    int16_t threat_corrhist[2][CORRHIST_SIZE];

    /* Non-pawn correction (per side): indexed by non-pawn piece positions */
    int16_t nonpawn_corrhist[2][2][CORRHIST_NONPAWN_SIZE];

    /* Continuation correction: [side][prev_piece_type][prev_target][piece_type][target] */
    int *continuation_corrhist; /* heap-allocated: [2 * PT_NB * 64 * PT_NB * 64] */
} CorrectionHistory;

void corrhist_init(CorrectionHistory *ch);
void corrhist_cleanup(CorrectionHistory *ch);
void corrhist_clear(CorrectionHistory *ch);

/* Update correction history after search returns a score different from static eval.
   Called when: !in_check, best move is quiet or none, score conditions met. */
void corrhist_update(CorrectionHistory *ch, const struct Position *pos,
                     int depth, int static_eval, int search_score,
                     int last_piece_type, int last_target,
                     int best_piece_type, int best_target);

/* Apply correction to a raw static evaluation, returning corrected eval. */
int corrhist_correct_eval(const CorrectionHistory *ch, const struct Position *pos,
                          int raw_eval);

/* Get the total correction value (for complexity-based search parameter adjustment). */
int corrhist_total_correction(const CorrectionHistory *ch, const struct Position *pos);

#endif
