#ifndef CLAUDEFISH_ZOBRIST_H
#define CLAUDEFISH_ZOBRIST_H

#include "types.h"

struct Position;

extern uint64_t zobrist_piece_keys[PIECE_BITBOARDS][BOARD_SQUARES];
extern uint64_t zobrist_pawn_keys[2][BOARD_SQUARES];
extern uint64_t zobrist_castling_keys[16];
extern uint64_t zobrist_en_passant_keys[8];
extern uint64_t zobrist_side_key;

void zobrist_init(void);
uint64_t zobrist_compute_hash(const struct Position *pos);
uint64_t zobrist_compute_pawn_hash(const struct Position *pos);

#endif
