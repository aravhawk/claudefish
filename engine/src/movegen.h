#ifndef CLAUDEFISH_MOVEGEN_H
#define CLAUDEFISH_MOVEGEN_H

#include "position.h"

typedef uint32_t Move;

enum {
    MOVEGEN_MAX_MOVES = 256,
    MOVE_PROMOTION_NONE = 0
};

typedef enum MoveFlags {
    MOVE_FLAG_NONE = 0,
    MOVE_FLAG_CAPTURE = 1 << 0,
    MOVE_FLAG_DOUBLE_PAWN_PUSH = 1 << 1,
    MOVE_FLAG_EN_PASSANT = 1 << 2,
    MOVE_FLAG_CASTLING = 1 << 3
} MoveFlags;

typedef struct MoveList {
    Move moves[MOVEGEN_MAX_MOVES];
    size_t count;
} MoveList;

extern Bitboard movegen_knight_attacks[BOARD_SQUARES];
extern Bitboard movegen_king_attacks[BOARD_SQUARES];
extern Bitboard movegen_pawn_attacks[2][BOARD_SQUARES];

static inline Move move_encode(
    int source,
    int target,
    uint8_t flags,
    Piece captured_piece,
    PieceType promotion_piece
) {
    return (Move) (
        ((uint32_t) (source & 0x3F)) |
        ((uint32_t) (target & 0x3F) << 6) |
        ((uint32_t) (flags & 0x0F) << 12) |
        ((uint32_t) (captured_piece & 0x0F) << 16) |
        ((uint32_t) (promotion_piece & 0x0F) << 20)
    );
}

static inline int move_source(Move move) {
    return (int) (move & 0x3FU);
}

static inline int move_target(Move move) {
    return (int) ((move >> 6) & 0x3FU);
}

static inline uint8_t move_flags(Move move) {
    return (uint8_t) ((move >> 12) & 0x0FU);
}

static inline Piece move_captured_piece(Move move) {
    return (Piece) ((move >> 16) & 0x0FU);
}

static inline PieceType move_promotion_piece(Move move) {
    return (PieceType) ((move >> 20) & 0x0FU);
}

void movegen_init(void);
Bitboard movegen_bishop_attacks(int square, Bitboard occupancy);
Bitboard movegen_rook_attacks(int square, Bitboard occupancy);
Bitboard movegen_queen_attacks(int square, Bitboard occupancy);
Bitboard movegen_pinned_pieces(const Position *pos, Color side);
bool movegen_is_square_attacked(const Position *pos, int square, Color attacker);
bool movegen_is_in_check(const Position *pos, Color side);
bool movegen_make_move(Position *pos, Move move);
bool movegen_unmake_move(Position *pos);
void movegen_generate_pseudo_legal(const Position *pos, MoveList *list);
void movegen_generate_legal(Position *pos, MoveList *list);
bool movegen_has_legal_moves(Position *pos);
bool movegen_is_checkmate(Position *pos);
bool movegen_is_stalemate(Position *pos);

#endif
