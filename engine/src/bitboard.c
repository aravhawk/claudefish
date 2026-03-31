#include "bitboard.h"

#include <ctype.h>

int bitboard_popcount(Bitboard bitboard) {
    return __builtin_popcountll((unsigned long long) bitboard);
}

int bitboard_lsb(Bitboard bitboard) {
    if (bitboard == 0) {
        return NO_SQUARE;
    }

    return __builtin_ctzll((unsigned long long) bitboard);
}

int bitboard_pop_lsb(Bitboard *bitboard) {
    int square;

    if (bitboard == NULL || *bitboard == 0) {
        return NO_SQUARE;
    }

    square = bitboard_lsb(*bitboard);
    *bitboard &= *bitboard - 1;
    return square;
}

bool bitboard_square_to_string(int square, char output[3]) {
    if (!square_is_valid(square) || output == NULL) {
        return false;
    }

    output[0] = (char) ('a' + bitboard_file_of(square));
    output[1] = (char) ('1' + bitboard_rank_of(square));
    output[2] = '\0';

    return true;
}

int bitboard_square_from_string(const char *square_name) {
    char file;
    char rank;

    if (square_name == NULL || square_name[0] == '\0' || square_name[1] == '\0' || square_name[2] != '\0') {
        return NO_SQUARE;
    }

    file = (char) tolower((unsigned char) square_name[0]);
    rank = square_name[1];

    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return NO_SQUARE;
    }

    return bitboard_make_square(file - 'a', rank - '1');
}
