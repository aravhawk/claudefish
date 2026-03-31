#ifndef CLAUDEFISH_TYPES_H
#define CLAUDEFISH_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t Bitboard;

enum {
    BOARD_SQUARES = 64,
    PIECE_BITBOARDS = 12
};

typedef enum Color {
    WHITE = 0,
    BLACK = 1,
    BOTH = 2
} Color;

typedef enum PieceType {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    PIECE_TYPE_NB = 6
} PieceType;

typedef enum Piece {
    NO_PIECE = 0,
    W_PAWN = 1,
    W_KNIGHT = 2,
    W_BISHOP = 3,
    W_ROOK = 4,
    W_QUEEN = 5,
    W_KING = 6,
    B_PAWN = 7,
    B_KNIGHT = 8,
    B_BISHOP = 9,
    B_ROOK = 10,
    B_QUEEN = 11,
    B_KING = 12
} Piece;

typedef enum Square {
    A1 = 0, B1, C1, D1, E1, F1, G1, H1,
    A2 = 8, B2, C2, D2, E2, F2, G2, H2,
    A3 = 16, B3, C3, D3, E3, F3, G3, H3,
    A4 = 24, B4, C4, D4, E4, F4, G4, H4,
    A5 = 32, B5, C5, D5, E5, F5, G5, H5,
    A6 = 40, B6, C6, D6, E6, F6, G6, H6,
    A7 = 48, B7, C7, D7, E7, F7, G7, H7,
    A8 = 56, B8, C8, D8, E8, F8, G8, H8
} Square;

#define NO_SQUARE (-1)

enum CastlingRights {
    CASTLE_WHITE_KINGSIDE = 1 << 0,
    CASTLE_WHITE_QUEENSIDE = 1 << 1,
    CASTLE_BLACK_KINGSIDE = 1 << 2,
    CASTLE_BLACK_QUEENSIDE = 1 << 3
};

static inline bool piece_is_valid(Piece piece) {
    return piece >= W_PAWN && piece <= B_KING;
}

static inline bool square_is_valid(int square) {
    return square >= 0 && square < BOARD_SQUARES;
}

static inline int piece_color(Piece piece) {
    if (!piece_is_valid(piece)) {
        return -1;
    }
    return (piece >= B_PAWN) ? BLACK : WHITE;
}

static inline int piece_type(Piece piece) {
    if (!piece_is_valid(piece)) {
        return -1;
    }
    return (piece - 1) % PIECE_TYPE_NB;
}

static inline int piece_bitboard_index(Piece piece) {
    return piece_is_valid(piece) ? (piece - 1) : -1;
}

static inline Piece make_piece(Color color, PieceType type) {
    return (Piece) (1 + (color * PIECE_TYPE_NB) + type);
}

#endif
