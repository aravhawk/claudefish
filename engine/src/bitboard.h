#ifndef CLAUDEFISH_BITBOARD_H
#define CLAUDEFISH_BITBOARD_H

#include "types.h"

#define BITBOARD_FROM_SQUARE(square) ((Bitboard) 1ULL << (square))

static inline int bitboard_rank_of(int square) {
    return square >> 3;
}

static inline int bitboard_file_of(int square) {
    return square & 7;
}

static inline int bitboard_make_square(int file, int rank) {
    return (rank << 3) | file;
}

int bitboard_popcount(Bitboard bitboard);
bool bitboard_square_to_string(int square, char output[3]);
int bitboard_square_from_string(const char *square_name);

#endif
