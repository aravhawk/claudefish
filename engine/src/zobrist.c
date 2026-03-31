#include "zobrist.h"

#include "bitboard.h"
#include "position.h"

uint64_t zobrist_piece_keys[PIECE_BITBOARDS][BOARD_SQUARES];
uint64_t zobrist_pawn_keys[2][BOARD_SQUARES];
uint64_t zobrist_castling_keys[16];
uint64_t zobrist_en_passant_keys[8];
uint64_t zobrist_side_key;

static bool zobrist_initialized = false;

static uint64_t splitmix64(uint64_t *state) {
    uint64_t value = (*state += 0x9E3779B97F4A7C15ULL);

    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

void zobrist_init(void) {
    uint64_t seed = 0xC1A0D00D5EED1234ULL;
    int piece;
    int square;
    int color;
    int rights;
    int file;

    if (zobrist_initialized) {
        return;
    }

    for (piece = 0; piece < PIECE_BITBOARDS; ++piece) {
        for (square = 0; square < BOARD_SQUARES; ++square) {
            zobrist_piece_keys[piece][square] = splitmix64(&seed);
        }
    }

    for (color = 0; color < 2; ++color) {
        for (square = 0; square < BOARD_SQUARES; ++square) {
            zobrist_pawn_keys[color][square] = splitmix64(&seed);
        }
    }

    for (rights = 0; rights < 16; ++rights) {
        zobrist_castling_keys[rights] = splitmix64(&seed);
    }

    for (file = 0; file < 8; ++file) {
        zobrist_en_passant_keys[file] = splitmix64(&seed);
    }

    zobrist_side_key = splitmix64(&seed);
    zobrist_initialized = true;
}

uint64_t zobrist_compute_hash(const struct Position *pos) {
    uint64_t hash = 0;
    int square;

    zobrist_init();

    if (pos == NULL) {
        return 0;
    }

    for (square = 0; square < BOARD_SQUARES; ++square) {
        Piece piece = (Piece) pos->mailbox[square];
        int piece_index = piece_bitboard_index(piece);

        if (piece_index >= 0) {
            hash ^= zobrist_piece_keys[piece_index][square];
        }
    }

    hash ^= zobrist_castling_keys[pos->castling_rights & 0x0F];

    if (pos->en_passant_sq != NO_SQUARE) {
        hash ^= zobrist_en_passant_keys[bitboard_file_of(pos->en_passant_sq)];
    }

    if (pos->side_to_move == BLACK) {
        hash ^= zobrist_side_key;
    }

    return hash;
}

uint64_t zobrist_compute_pawn_hash(const struct Position *pos) {
    uint64_t hash = 0;
    int square;

    zobrist_init();

    if (pos == NULL) {
        return 0;
    }

    for (square = 0; square < BOARD_SQUARES; ++square) {
        Piece piece = (Piece) pos->mailbox[square];

        if (piece == W_PAWN) {
            hash ^= zobrist_pawn_keys[WHITE][square];
        } else if (piece == B_PAWN) {
            hash ^= zobrist_pawn_keys[BLACK][square];
        }
    }

    return hash;
}
