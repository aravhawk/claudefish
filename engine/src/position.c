#include "position.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zobrist.h"

static bool append_char(char *buffer, size_t buffer_size, size_t *index, char value) {
    if (*index + 1 >= buffer_size) {
        return false;
    }

    buffer[*index] = value;
    ++(*index);
    buffer[*index] = '\0';
    return true;
}

static bool append_string(char *buffer, size_t buffer_size, size_t *index, const char *value) {
    size_t length = strlen(value);

    if (*index + length >= buffer_size) {
        return false;
    }

    memcpy(buffer + *index, value, length);
    *index += length;
    buffer[*index] = '\0';
    return true;
}

static char piece_to_char(Piece piece) {
    switch (piece) {
        case W_PAWN:   return 'P';
        case W_KNIGHT: return 'N';
        case W_BISHOP: return 'B';
        case W_ROOK:   return 'R';
        case W_QUEEN:  return 'Q';
        case W_KING:   return 'K';
        case B_PAWN:   return 'p';
        case B_KNIGHT: return 'n';
        case B_BISHOP: return 'b';
        case B_ROOK:   return 'r';
        case B_QUEEN:  return 'q';
        case B_KING:   return 'k';
        case NO_PIECE:
        default:
            return '\0';
    }
}

static bool piece_from_char(char symbol, Piece *piece) {
    if (piece == NULL) {
        return false;
    }

    switch (symbol) {
        case 'P': *piece = W_PAWN; return true;
        case 'N': *piece = W_KNIGHT; return true;
        case 'B': *piece = W_BISHOP; return true;
        case 'R': *piece = W_ROOK; return true;
        case 'Q': *piece = W_QUEEN; return true;
        case 'K': *piece = W_KING; return true;
        case 'p': *piece = B_PAWN; return true;
        case 'n': *piece = B_KNIGHT; return true;
        case 'b': *piece = B_BISHOP; return true;
        case 'r': *piece = B_ROOK; return true;
        case 'q': *piece = B_QUEEN; return true;
        case 'k': *piece = B_KING; return true;
        default:
            *piece = NO_PIECE;
            return false;
    }
}

static void xor_piece_hashes(Position *pos, Piece piece, int square) {
    int piece_index;
    int color;

    if (pos == NULL || !piece_is_valid(piece) || !square_is_valid(square)) {
        return;
    }

    piece_index = piece_bitboard_index(piece);
    color = piece_color(piece);

    pos->zobrist_hash ^= zobrist_piece_keys[piece_index][square];

    if (piece_type(piece) == PAWN) {
        pos->pawn_hash ^= zobrist_pawn_keys[color][square];
    }
}

static bool parse_uint16(const char *text, const char **end, uint16_t *value) {
    char *local_end = NULL;
    unsigned long parsed;

    if (text == NULL || value == NULL) {
        return false;
    }

    if (!isdigit((unsigned char) *text)) {
        return false;
    }

    parsed = strtoul(text, &local_end, 10);
    if (local_end == text || parsed > 65535UL) {
        return false;
    }

    *value = (uint16_t) parsed;

    if (end != NULL) {
        *end = local_end;
    }

    return true;
}

void position_clear(Position *pos) {
    if (pos == NULL) {
        return;
    }

    memset(pos, 0, sizeof(*pos));
    zobrist_init();
    pos->side_to_move = WHITE;
    pos->en_passant_sq = NO_SQUARE;
    pos->fullmove_number = 1;
    pos->zobrist_hash = zobrist_castling_keys[0];
}

bool position_clear_piece(Position *pos, int square) {
    Bitboard mask;
    Piece piece;
    int color;
    int piece_index;

    if (pos == NULL || !square_is_valid(square)) {
        return false;
    }

    piece = (Piece) pos->mailbox[square];
    if (!piece_is_valid(piece)) {
        return true;
    }

    zobrist_init();

    mask = BITBOARD_FROM_SQUARE(square);
    color = piece_color(piece);
    piece_index = piece_bitboard_index(piece);

    pos->piece_bitboards[piece_index] &= ~mask;
    pos->occupancy[color] &= ~mask;
    pos->occupancy[BOTH] &= ~mask;
    pos->mailbox[square] = NO_PIECE;
    xor_piece_hashes(pos, piece, square);

    return true;
}

bool position_set_piece(Position *pos, int square, Piece piece) {
    Bitboard mask;
    int color;
    int piece_index;

    if (pos == NULL || !square_is_valid(square)) {
        return false;
    }

    if (piece == NO_PIECE) {
        return position_clear_piece(pos, square);
    }

    if (!piece_is_valid(piece)) {
        return false;
    }

    if (!position_clear_piece(pos, square)) {
        return false;
    }

    zobrist_init();

    mask = BITBOARD_FROM_SQUARE(square);
    color = piece_color(piece);
    piece_index = piece_bitboard_index(piece);

    pos->piece_bitboards[piece_index] |= mask;
    pos->occupancy[color] |= mask;
    pos->occupancy[BOTH] |= mask;
    pos->mailbox[square] = (uint8_t) piece;
    xor_piece_hashes(pos, piece, square);

    return true;
}

Piece position_get_piece(const Position *pos, int square) {
    if (pos == NULL || !square_is_valid(square)) {
        return NO_PIECE;
    }

    return (Piece) pos->mailbox[square];
}

void position_refresh_hashes(Position *pos) {
    if (pos == NULL) {
        return;
    }

    zobrist_init();
    pos->zobrist_hash = zobrist_compute_hash(pos);
    pos->pawn_hash = zobrist_compute_pawn_hash(pos);
}

bool position_from_fen(Position *pos, const char *fen) {
    const char *cursor;
    int rank = 7;
    int file = 0;
    bool white_king_found = false;
    bool black_king_found = false;

    if (pos == NULL || fen == NULL) {
        return false;
    }

    position_clear(pos);
    cursor = fen;

    while (*cursor != '\0' && *cursor != ' ') {
        char symbol = *cursor++;

        if (symbol == '/') {
            if (file != 8 || rank == 0) {
                return false;
            }
            --rank;
            file = 0;
            continue;
        }

        if (isdigit((unsigned char) symbol)) {
            int empty_squares = symbol - '0';

            if (empty_squares < 1 || empty_squares > 8 || file + empty_squares > 8) {
                return false;
            }

            file += empty_squares;
            continue;
        }

        if (file >= 8) {
            return false;
        }

        {
            Piece piece = NO_PIECE;
            int square;

            if (!piece_from_char(symbol, &piece)) {
                return false;
            }

            square = bitboard_make_square(file, rank);
            if (!position_set_piece(pos, square, piece)) {
                return false;
            }

            if (piece == W_KING) {
                white_king_found = true;
            } else if (piece == B_KING) {
                black_king_found = true;
            }

            ++file;
        }
    }

    if (*cursor != ' ' || rank != 0 || file != 8 || !white_king_found || !black_king_found) {
        return false;
    }

    ++cursor;

    if (*cursor == 'w') {
        pos->side_to_move = WHITE;
    } else if (*cursor == 'b') {
        pos->side_to_move = BLACK;
    } else {
        return false;
    }

    ++cursor;
    if (*cursor != ' ') {
        return false;
    }
    ++cursor;

    pos->castling_rights = 0;
    if (*cursor == '-') {
        ++cursor;
    } else {
        while (*cursor != '\0' && *cursor != ' ') {
            switch (*cursor) {
                case 'K':
                    if ((pos->castling_rights & CASTLE_WHITE_KINGSIDE) != 0) {
                        return false;
                    }
                    pos->castling_rights |= CASTLE_WHITE_KINGSIDE;
                    break;
                case 'Q':
                    if ((pos->castling_rights & CASTLE_WHITE_QUEENSIDE) != 0) {
                        return false;
                    }
                    pos->castling_rights |= CASTLE_WHITE_QUEENSIDE;
                    break;
                case 'k':
                    if ((pos->castling_rights & CASTLE_BLACK_KINGSIDE) != 0) {
                        return false;
                    }
                    pos->castling_rights |= CASTLE_BLACK_KINGSIDE;
                    break;
                case 'q':
                    if ((pos->castling_rights & CASTLE_BLACK_QUEENSIDE) != 0) {
                        return false;
                    }
                    pos->castling_rights |= CASTLE_BLACK_QUEENSIDE;
                    break;
                default:
                    return false;
            }
            ++cursor;
        }
    }

    if (*cursor != ' ') {
        return false;
    }
    ++cursor;

    if (*cursor == '-') {
        pos->en_passant_sq = NO_SQUARE;
        ++cursor;
    } else {
        int en_passant_sq;
        int rank_of_square;
        char square_name[3];

        if (cursor[0] == '\0' || cursor[1] == '\0') {
            return false;
        }

        square_name[0] = cursor[0];
        square_name[1] = cursor[1];
        square_name[2] = '\0';
        en_passant_sq = bitboard_square_from_string(square_name);
        if (en_passant_sq == NO_SQUARE) {
            return false;
        }

        rank_of_square = bitboard_rank_of(en_passant_sq);
        if (rank_of_square != 2 && rank_of_square != 5) {
            return false;
        }
        if ((pos->side_to_move == WHITE && rank_of_square != 5) ||
            (pos->side_to_move == BLACK && rank_of_square != 2)) {
            return false;
        }

        pos->en_passant_sq = (int8_t) en_passant_sq;
        cursor += 2;
    }

    if (*cursor != ' ') {
        return false;
    }
    ++cursor;

    if (!parse_uint16(cursor, &cursor, &pos->halfmove_clock)) {
        return false;
    }
    if (*cursor != ' ') {
        return false;
    }
    ++cursor;

    if (!parse_uint16(cursor, &cursor, &pos->fullmove_number) || pos->fullmove_number == 0) {
        return false;
    }
    if (*cursor != '\0') {
        return false;
    }

    position_refresh_hashes(pos);
    return true;
}

bool position_to_fen(const Position *pos, char *buffer, size_t buffer_size) {
    int rank;
    size_t index = 0;

    if (pos == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    buffer[0] = '\0';

    for (rank = 7; rank >= 0; --rank) {
        int file;
        int empty_squares = 0;

        for (file = 0; file < 8; ++file) {
            int square = bitboard_make_square(file, rank);
            Piece piece = (Piece) pos->mailbox[square];

            if (piece == NO_PIECE) {
                ++empty_squares;
                continue;
            }

            if (empty_squares > 0) {
                if (!append_char(buffer, buffer_size, &index, (char) ('0' + empty_squares))) {
                    return false;
                }
                empty_squares = 0;
            }

            if (!append_char(buffer, buffer_size, &index, piece_to_char(piece))) {
                return false;
            }
        }

        if (empty_squares > 0) {
            if (!append_char(buffer, buffer_size, &index, (char) ('0' + empty_squares))) {
                return false;
            }
        }

        if (rank > 0 && !append_char(buffer, buffer_size, &index, '/')) {
            return false;
        }
    }

    if (!append_char(buffer, buffer_size, &index, ' ')) {
        return false;
    }
    if (!append_char(buffer, buffer_size, &index, pos->side_to_move == WHITE ? 'w' : 'b')) {
        return false;
    }
    if (!append_char(buffer, buffer_size, &index, ' ')) {
        return false;
    }

    if (pos->castling_rights == 0) {
        if (!append_char(buffer, buffer_size, &index, '-')) {
            return false;
        }
    } else {
        if ((pos->castling_rights & CASTLE_WHITE_KINGSIDE) != 0 &&
            !append_char(buffer, buffer_size, &index, 'K')) {
            return false;
        }
        if ((pos->castling_rights & CASTLE_WHITE_QUEENSIDE) != 0 &&
            !append_char(buffer, buffer_size, &index, 'Q')) {
            return false;
        }
        if ((pos->castling_rights & CASTLE_BLACK_KINGSIDE) != 0 &&
            !append_char(buffer, buffer_size, &index, 'k')) {
            return false;
        }
        if ((pos->castling_rights & CASTLE_BLACK_QUEENSIDE) != 0 &&
            !append_char(buffer, buffer_size, &index, 'q')) {
            return false;
        }
    }

    if (!append_char(buffer, buffer_size, &index, ' ')) {
        return false;
    }

    if (pos->en_passant_sq == NO_SQUARE) {
        if (!append_char(buffer, buffer_size, &index, '-')) {
            return false;
        }
    } else {
        char square_name[3];

        if (!bitboard_square_to_string(pos->en_passant_sq, square_name)) {
            return false;
        }
        if (!append_string(buffer, buffer_size, &index, square_name)) {
            return false;
        }
    }

    {
        char counters[32];
        int written = snprintf(
            counters,
            sizeof(counters),
            " %u %u",
            (unsigned int) pos->halfmove_clock,
            (unsigned int) pos->fullmove_number
        );

        if (written < 0 || (size_t) written >= sizeof(counters)) {
            return false;
        }

        if (!append_string(buffer, buffer_size, &index, counters)) {
            return false;
        }
    }

    return true;
}

void position_print_board(const Position *pos) {
    int rank;

    if (pos == NULL) {
        return;
    }

    for (rank = 7; rank >= 0; --rank) {
        int file;

        printf("%d ", rank + 1);
        for (file = 0; file < 8; ++file) {
            int square = bitboard_make_square(file, rank);
            Piece piece = (Piece) pos->mailbox[square];
            char symbol = piece_to_char(piece);

            if (symbol == '\0') {
                symbol = '.';
            }

            printf("%c ", symbol);
        }
        putchar('\n');
    }

    printf("  a b c d e f g h\n");
    printf("Side: %c\n", pos->side_to_move == WHITE ? 'w' : 'b');
}
