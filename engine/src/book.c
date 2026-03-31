#include "book.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "book_embedded.h"
#include "book_random.h"

static PolyglotEntry *book_entries = NULL;
static size_t book_count = 0;
static bool book_initialized = false;
static bool book_entries_owned = false;
static uint32_t book_rng_state = 1U;

static int book_piece_index(Piece piece) {
    switch (piece) {
        case B_PAWN:   return 0;
        case W_PAWN:   return 1;
        case B_KNIGHT: return 2;
        case W_KNIGHT: return 3;
        case B_BISHOP: return 4;
        case W_BISHOP: return 5;
        case B_ROOK:   return 6;
        case W_ROOK:   return 7;
        case B_QUEEN:  return 8;
        case W_QUEEN:  return 9;
        case B_KING:   return 10;
        case W_KING:   return 11;
        case NO_PIECE:
        default:
            return -1;
    }
}

static uint16_t book_read_be16(const unsigned char *bytes) {
    return (uint16_t) ((((uint16_t) bytes[0]) << 8) | ((uint16_t) bytes[1]));
}

static uint32_t book_read_be32(const unsigned char *bytes) {
    return (((uint32_t) bytes[0]) << 24) |
        (((uint32_t) bytes[1]) << 16) |
        (((uint32_t) bytes[2]) << 8) |
        ((uint32_t) bytes[3]);
}

static uint64_t book_read_be64(const unsigned char *bytes) {
    return (((uint64_t) book_read_be32(bytes)) << 32) | (uint64_t) book_read_be32(bytes + 4);
}

static int book_compare_entries(const void *left, const void *right) {
    const PolyglotEntry *lhs = (const PolyglotEntry *) left;
    const PolyglotEntry *rhs = (const PolyglotEntry *) right;

    if (lhs->key < rhs->key) {
        return -1;
    }
    if (lhs->key > rhs->key) {
        return 1;
    }
    if (lhs->move < rhs->move) {
        return -1;
    }
    if (lhs->move > rhs->move) {
        return 1;
    }
    return 0;
}

static size_t book_lower_bound(uint64_t key) {
    size_t low = 0;
    size_t high = book_count;

    while (low < high) {
        size_t mid = low + ((high - low) / 2);

        if (book_entries[mid].key < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return low;
}

static uint32_t book_random_u32(void) {
    book_rng_state = (book_rng_state * 1664525U) + 1013904223U;
    return book_rng_state;
}

static PieceType book_promotion_piece(uint16_t raw_move) {
    switch ((raw_move >> 12) & 0x7U) {
        case 1: return KNIGHT;
        case 2: return BISHOP;
        case 3: return ROOK;
        case 4: return QUEEN;
        default: return MOVE_PROMOTION_NONE;
    }
}

static bool book_targets_match(Move move, int polyglot_target) {
    int target = move_target(move);

    if (target == polyglot_target) {
        return true;
    }

    if ((move_flags(move) & MOVE_FLAG_CASTLING) == 0) {
        return false;
    }

    return (move_source(move) == E1 && target == G1 && polyglot_target == H1) ||
        (move_source(move) == E1 && target == C1 && polyglot_target == A1) ||
        (move_source(move) == E8 && target == G8 && polyglot_target == H8) ||
        (move_source(move) == E8 && target == C8 && polyglot_target == A8);
}

static bool book_decode_move(Position *pos, uint16_t raw_move, Move *out_move) {
    MoveList legal_moves;
    int source = (raw_move >> 6) & 0x3F;
    int target = raw_move & 0x3F;
    PieceType promotion_piece = book_promotion_piece(raw_move);
    size_t index;

    if (pos == NULL || out_move == NULL) {
        return false;
    }

    movegen_generate_legal(pos, &legal_moves);
    for (index = 0; index < legal_moves.count; ++index) {
        Move move = legal_moves.moves[index];

        if (move_source(move) != source) {
            continue;
        }
        if (!book_targets_match(move, target)) {
            continue;
        }
        if (move_promotion_piece(move) != promotion_piece) {
            continue;
        }

        *out_move = move;
        return true;
    }

    return false;
}

void book_clear(void) {
    if (book_entries_owned && book_entries != NULL) {
        free(book_entries);
    }

    book_entries = NULL;
    book_count = 0;
    book_entries_owned = false;
}

bool book_load_memory(const unsigned char *data, size_t size) {
    PolyglotEntry *entries;
    size_t count;
    size_t index;

    if (data == NULL || size == 0 || (size % 16) != 0) {
        return false;
    }

    count = size / 16;
    entries = (PolyglotEntry *) malloc(count * sizeof(*entries));
    if (entries == NULL) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        const unsigned char *entry = data + (index * 16);

        entries[index].key = book_read_be64(entry);
        entries[index].move = book_read_be16(entry + 8);
        entries[index].weight = book_read_be16(entry + 10);
        entries[index].learn = book_read_be32(entry + 12);
    }

    qsort(entries, count, sizeof(*entries), book_compare_entries);
    book_clear();
    book_entries = entries;
    book_count = count;
    book_entries_owned = true;
    return true;
}

bool book_load_file(const char *path) {
    FILE *file;
    unsigned char *buffer;
    long size;
    bool loaded;

    if (path == NULL) {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    size = ftell(file);
    if (size <= 0 || (size % 16L) != 0 || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    buffer = (unsigned char *) malloc((size_t) size);
    if (buffer == NULL) {
        fclose(file);
        return false;
    }

    loaded = fread(buffer, 1, (size_t) size, file) == (size_t) size && book_load_memory(buffer, (size_t) size);
    free(buffer);
    fclose(file);
    return loaded;
}

bool book_load_default(void) {
    static const char *paths[] = {
        "engine/book/rodent.bin",
        "./engine/book/rodent.bin",
        "book/rodent.bin"
    };
    size_t index;

    for (index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        if (book_load_file(paths[index])) {
            return true;
        }
    }

    return book_load_memory(book_embedded_data, book_embedded_size);
}

void book_init(void) {
    if (book_initialized) {
        return;
    }

    movegen_init();
    book_load_default();
    book_initialized = true;
}

void book_set_seed(uint32_t seed) {
    book_rng_state = seed == 0U ? 1U : seed;
}

bool book_is_loaded(void) {
    return book_count > 0;
}

size_t book_entry_count(void) {
    return book_count;
}

uint64_t book_polyglot_hash(const Position *pos) {
    uint64_t hash = 0;
    int square;

    if (pos == NULL) {
        return 0;
    }

    movegen_init();

    for (square = A1; square <= H8; ++square) {
        Piece piece = position_get_piece(pos, square);
        int piece_index = book_piece_index(piece);

        if (piece_index >= 0) {
            hash ^= book_polyglot_random[(piece_index * 64) + square];
        }
    }

    if ((pos->castling_rights & CASTLE_WHITE_KINGSIDE) != 0) {
        hash ^= book_polyglot_random[768];
    }
    if ((pos->castling_rights & CASTLE_WHITE_QUEENSIDE) != 0) {
        hash ^= book_polyglot_random[769];
    }
    if ((pos->castling_rights & CASTLE_BLACK_KINGSIDE) != 0) {
        hash ^= book_polyglot_random[770];
    }
    if ((pos->castling_rights & CASTLE_BLACK_QUEENSIDE) != 0) {
        hash ^= book_polyglot_random[771];
    }

    if (pos->en_passant_sq != NO_SQUARE) {
        Bitboard target = BITBOARD_FROM_SQUARE(pos->en_passant_sq);
        Bitboard pawns = pos->piece_bitboards[piece_bitboard_index(make_piece((Color) pos->side_to_move, PAWN))];
        Bitboard attackers = pos->side_to_move == WHITE
            ? movegen_pawn_attacks[BLACK][pos->en_passant_sq]
            : movegen_pawn_attacks[WHITE][pos->en_passant_sq];

        (void) target;
        if ((pawns & attackers) != 0) {
            hash ^= book_polyglot_random[772 + bitboard_file_of(pos->en_passant_sq)];
        }
    }

    if (pos->side_to_move == WHITE) {
        hash ^= book_polyglot_random[780];
    }

    return hash;
}

bool book_probe_move(Position *pos, Move *out_move) {
    uint64_t key;
    size_t start;
    size_t index;
    Move candidates[64];
    uint32_t weights[64];
    size_t candidate_count = 0;
    uint32_t total_weight = 0;

    if (out_move != NULL) {
        *out_move = 0;
    }

    book_init();
    if (pos == NULL || out_move == NULL || book_entries == NULL || book_count == 0) {
        return false;
    }

    key = book_polyglot_hash(pos);
    start = book_lower_bound(key);

    for (index = start; index < book_count && book_entries[index].key == key; ++index) {
        Move move;

        if (candidate_count >= sizeof(candidates) / sizeof(candidates[0])) {
            break;
        }

        if (!book_decode_move(pos, book_entries[index].move, &move)) {
            continue;
        }

        candidates[candidate_count] = move;
        weights[candidate_count] = book_entries[index].weight == 0 ? 1U : book_entries[index].weight;
        total_weight += weights[candidate_count];
        ++candidate_count;
    }

    if (candidate_count == 0 || total_weight == 0) {
        return false;
    }

    {
        uint32_t threshold = book_random_u32() % total_weight;
        uint32_t accumulated = 0;

        for (index = 0; index < candidate_count; ++index) {
            accumulated += weights[index];
            if (threshold < accumulated) {
                *out_move = candidates[index];
                return true;
            }
        }
    }

    *out_move = candidates[candidate_count - 1];
    return true;
}
