#include "correction.h"
#include "position.h"
#include "movegen.h"
#include "evaluate.h"
#include "search.h"
#include "zobrist.h"

#include <stdlib.h>
#include <string.h>

#define CORRHIST_CONT_INDEX(side, pp, pt, cp, ct) \
    ((side) * PIECE_TYPE_NB * BOARD_SQUARES * PIECE_TYPE_NB * BOARD_SQUARES + \
     (pp) * BOARD_SQUARES * PIECE_TYPE_NB * BOARD_SQUARES + \
     (pt) * PIECE_TYPE_NB * BOARD_SQUARES + \
     (cp) * BOARD_SQUARES + (ct))

static int corrhist_clamp(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int corrhist_min(int a, int b) { return a < b ? a : b; }

void corrhist_init(CorrectionHistory *ch) {
    if (ch == NULL) return;
    memset(ch, 0, sizeof(*ch));
    ch->continuation_corrhist = (int *)calloc(
        2 * PIECE_TYPE_NB * BOARD_SQUARES * PIECE_TYPE_NB * BOARD_SQUARES,
        sizeof(int));
}

void corrhist_cleanup(CorrectionHistory *ch) {
    if (ch == NULL) return;
    if (ch->continuation_corrhist != NULL) {
        free(ch->continuation_corrhist);
        ch->continuation_corrhist = NULL;
    }
}

void corrhist_clear(CorrectionHistory *ch) {
    if (ch == NULL) return;
    memset(ch->pawn_corrhist, 0, sizeof(ch->pawn_corrhist));
    memset(ch->material_corrhist, 0, sizeof(ch->material_corrhist));
    memset(ch->threat_corrhist, 0, sizeof(ch->threat_corrhist));
    memset(ch->nonpawn_corrhist, 0, sizeof(ch->nonpawn_corrhist));
    if (ch->continuation_corrhist != NULL) {
        size_t cont_size = 2 * PIECE_TYPE_NB * BOARD_SQUARES * PIECE_TYPE_NB * BOARD_SQUARES;
        memset(ch->continuation_corrhist, 0, cont_size * sizeof(int));
    }
}

/* Compute a hash of capturable pieces for threat correction indexing. */
static uint64_t corrhist_threat_hash(const Position *pos) {
    Color side = (Color)pos->side_to_move;
    Color opp = side == WHITE ? BLACK : WHITE;
    Bitboard our_pieces = pos->occupancy[side];
    Bitboard their_attacks = 0;
    int sq;
    uint64_t hash = 0;

    /* Find squares where our pieces are attacked by opponent */
    for (sq = 0; sq < BOARD_SQUARES; sq++) {
        if (our_pieces & (1ULL << sq)) {
            if (movegen_is_square_attacked(pos, sq, opp)) {
                hash ^= zobrist_piece_keys[piece_bitboard_index(position_get_piece(pos, sq))][sq];
            }
        }
    }
    return hash;
}

/* Compute a hash of non-pawn pieces for one side. */
static uint64_t corrhist_nonpawn_hash(const Position *pos, Color side) {
    Bitboard np = pos->piece_bitboards[make_piece(side, KNIGHT) - 1] |
                  pos->piece_bitboards[make_piece(side, BISHOP) - 1] |
                  pos->piece_bitboards[make_piece(side, ROOK) - 1] |
                  pos->piece_bitboards[make_piece(side, QUEEN) - 1];
    int king_sq = __builtin_ctzll(pos->piece_bitboards[make_piece(side, KING) - 1]);
    uint64_t hash = zobrist_piece_keys[make_piece(side, KING) - 1][king_sq];
    while (np) {
        int sq = __builtin_ctzll(np);
        np &= np - 1;
        hash ^= zobrist_piece_keys[piece_bitboard_index(position_get_piece(pos, sq))][sq];
    }
    return hash;
}

/* Compute material hash from position. */
static uint64_t corrhist_material_hash(const Position *pos) {
    uint64_t hash = 0;
    int sq;
    for (sq = 0; sq < BOARD_SQUARES; sq++) {
        Piece p = position_get_piece(pos, sq);
        if (piece_is_valid(p) && piece_type(p) != PAWN && piece_type(p) != KING) {
            hash ^= zobrist_piece_keys[piece_bitboard_index(p)][sq];
        }
    }
    return hash;
}

void corrhist_update(CorrectionHistory *ch, const Position *pos,
                     int depth, int static_eval, int search_score,
                     int last_piece_type, int last_target,
                     int best_piece_type, int best_target) {
    if (ch == NULL) return;

    int diff = search_score - static_eval;
    Color side = (Color)pos->side_to_move;

    /* History gravity formula: bonus = clamp(diff * depth / 8, -limit/4, limit/4) */
    int bonus = corrhist_clamp(diff * depth / 8,
                               -CORRHIST_LIMIT / 4, CORRHIST_LIMIT / 4);
    if (bonus == 0) return;

    int new_weight = corrhist_min(depth * depth + 2 * depth + 1, 128);

    /* Pawn correction */
    {
        int idx = (int)(pos->pawn_hash % CORRHIST_SIZE);
        int16_t *entry = &ch->pawn_corrhist[side][idx];
        *entry = (int16_t)corrhist_clamp(
            (*entry * (CORRHIST_WEIGHT_SCALE - new_weight) + bonus * CORRHIST_GRAIN * new_weight)
            / CORRHIST_WEIGHT_SCALE,
            -CORRHIST_MAX, CORRHIST_MAX);
    }

    /* Material correction */
    {
        uint64_t mhash = corrhist_material_hash(pos);
        int idx = (int)(mhash % CORRHIST_SIZE);
        int16_t *entry = &ch->material_corrhist[side][idx];
        *entry = (int16_t)corrhist_clamp(
            (*entry * (CORRHIST_WEIGHT_SCALE - new_weight) + bonus * CORRHIST_GRAIN * new_weight)
            / CORRHIST_WEIGHT_SCALE,
            -CORRHIST_MAX, CORRHIST_MAX);
    }

    /* Threat correction */
    {
        uint64_t thash = corrhist_threat_hash(pos);
        int idx = (int)(thash % CORRHIST_SIZE);
        int16_t *entry = &ch->threat_corrhist[side][idx];
        *entry = (int16_t)corrhist_clamp(
            (*entry * (CORRHIST_WEIGHT_SCALE - new_weight) + bonus * CORRHIST_GRAIN * new_weight)
            / CORRHIST_WEIGHT_SCALE,
            -CORRHIST_MAX, CORRHIST_MAX);
    }

    /* Non-pawn correction (per side, for both our and opponent pieces) */
    {
        int s;
        for (s = 0; s < 2; s++) {
            Color c = s == 0 ? side : (side == WHITE ? BLACK : WHITE);
            uint64_t nphash = corrhist_nonpawn_hash(pos, c);
            int idx = (int)(nphash % CORRHIST_NONPAWN_SIZE);
            int16_t *entry = &ch->nonpawn_corrhist[side][s][idx];
            *entry = (int16_t)corrhist_clamp(
                (*entry * (CORRHIST_WEIGHT_SCALE - new_weight) + bonus * CORRHIST_GRAIN * new_weight)
                / CORRHIST_WEIGHT_SCALE,
                -CORRHIST_MAX, CORRHIST_MAX);
        }
    }

    /* Continuation correction */
    if (ch->continuation_corrhist != NULL &&
        last_piece_type >= PAWN && last_piece_type < PIECE_TYPE_NB &&
        last_target >= 0 && last_target < BOARD_SQUARES &&
        best_piece_type >= PAWN && best_piece_type < PIECE_TYPE_NB &&
        best_target >= 0 && best_target < BOARD_SQUARES) {
        int idx = CORRHIST_CONT_INDEX(side, last_piece_type, last_target,
                                      best_piece_type, best_target);
        int *entry = &ch->continuation_corrhist[idx];
        *entry = corrhist_clamp(
            (*entry * (CORRHIST_WEIGHT_SCALE - new_weight) + bonus * new_weight)
            / CORRHIST_WEIGHT_SCALE,
            -CORRHIST_CONT_MAX, CORRHIST_CONT_MAX);
    }
}

int corrhist_correct_eval(const CorrectionHistory *ch, const Position *pos,
                          int raw_eval) {
    if (ch == NULL) return raw_eval;
    Color side = (Color)pos->side_to_move;
    int correction = 0;

    /* Only use cheap-to-compute correction tables during eval.
       Material, threat, non-pawn hashes are too expensive for per-node eval. */

    /* Pawn correction (uses pre-computed pawn_hash — cheap) */
    {
        int idx = (int)(pos->pawn_hash % CORRHIST_SIZE);
        correction += ch->pawn_corrhist[side][idx] / CORRHIST_GRAIN;
    }

    /* Apply with scaling factor 66/512 (from Stockfish) */
    return corrhist_clamp(raw_eval + 66 * correction / 512,
                          -SEARCH_MATE_SCORE + 1, SEARCH_MATE_SCORE - 1);
}

int corrhist_total_correction(const CorrectionHistory *ch, const Position *pos) {
    if (ch == NULL) return 0;
    Color side = (Color)pos->side_to_move;
    return ch->pawn_corrhist[side][pos->pawn_hash % CORRHIST_SIZE] / CORRHIST_GRAIN;
}
