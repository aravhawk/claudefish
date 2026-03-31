#ifndef CLAUDEFISH_POSITION_H
#define CLAUDEFISH_POSITION_H

#include <stddef.h>

#include "bitboard.h"

enum {
    POSITION_STATE_STACK_CAPACITY = 2048
};

typedef struct PositionState {
    uint8_t castling_rights;
    int8_t en_passant_sq;
    uint16_t halfmove_clock;
    uint16_t fullmove_number;
    uint64_t zobrist_hash;
    uint64_t pawn_hash;
    uint32_t move;
    uint8_t captured_piece;
} PositionState;

typedef struct Position {
    Bitboard piece_bitboards[PIECE_BITBOARDS];
    Bitboard occupancy[3];
    uint8_t mailbox[BOARD_SQUARES];
    int16_t pst_mg[2];
    int16_t pst_eg[2];
    uint8_t side_to_move;
    uint8_t castling_rights;
    int8_t en_passant_sq;
    uint16_t halfmove_clock;
    uint16_t fullmove_number;
    uint64_t zobrist_hash;
    uint64_t pawn_hash;
    PositionState state_stack[POSITION_STATE_STACK_CAPACITY];
    size_t state_count;
} Position;

void position_clear(Position *pos);
bool position_from_fen(Position *pos, const char *fen);
bool position_to_fen(const Position *pos, char *buffer, size_t buffer_size);
bool position_set_piece(Position *pos, int square, Piece piece);
bool position_clear_piece(Position *pos, int square);
Piece position_get_piece(const Position *pos, int square);
void position_refresh_hashes(Position *pos);
void position_print_board(const Position *pos);

#endif
