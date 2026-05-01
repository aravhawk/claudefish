#include "evaluate.h"

#include <string.h>

#include "draw.h"

enum {
    EVAL_PAWN_HASH_SIZE = 262144,
    EVAL_OPENING_PHASE_TOTAL = 24
};

typedef struct EvalPawnHashEntry {
    uint64_t key;
    int16_t mg_score;
    int16_t eg_score;
    uint8_t valid;
} EvalPawnHashEntry;

static const int material_mg[PIECE_TYPE_NB] = { 100, 320, 330, 500, 950, 0 };
static const int material_eg[PIECE_TYPE_NB] = { 120, 310, 320, 530, 1000, 0 };
static const int phase_weights[PIECE_TYPE_NB] = { 0, 1, 1, 2, 4, 0 };

static const int passed_pawn_bonus_mg[8] = { 0, 0, 20, 35, 55, 80, 120, 0 };
static const int passed_pawn_bonus_eg[8] = { 0, 0, 30, 50, 75, 100, 120, 0 };

static const int pawn_mobility_mg[5] = { 0, 4, 8, 12, 16 };
static const int pawn_mobility_eg[5] = { 0, 5, 10, 15, 20 };
static const int knight_mobility_mg[9] = { -18, -10, -4, 2, 8, 14, 20, 24, 28 };
static const int knight_mobility_eg[9] = { -12, -6, 0, 4, 8, 12, 16, 18, 20 };
static const int bishop_mobility_mg[14] = { -12, -8, -4, 0, 4, 8, 12, 16, 20, 24, 27, 30, 32, 34 };
static const int bishop_mobility_eg[14] = { -8, -5, -2, 1, 4, 7, 10, 13, 16, 19, 22, 24, 26, 28 };
static const int rook_mobility_mg[15] = { -10, -6, -2, 2, 6, 10, 14, 18, 21, 24, 27, 29, 31, 33, 35 };
static const int rook_mobility_eg[15] = { -6, -3, 0, 3, 6, 9, 12, 15, 18, 21, 24, 26, 28, 30, 32 };
static const int queen_mobility_mg[28] = {
    -10, -8, -6, -4, -2, 0, 2, 4, 6, 8, 10, 12, 14, 16,
    18, 20, 22, 24, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35
};
static const int queen_mobility_eg[28] = {
    -6, -4, -2, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20,
    22, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36
};
static const int king_mobility_mg[9] = { -8, -5, -2, 0, 2, 3, 4, 5, 6 };
static const int king_mobility_eg[9] = { -4, -1, 2, 5, 8, 11, 14, 17, 20 };
static const int attacker_count_bonus[9] = { 0, 0, 12, 24, 36, 48, 60, 72, 84 };
static const int attack_weights[PIECE_TYPE_NB] = { 0, 1, 1, 2, 4, 0 };
static const int attack_weight_bonus[17] = {
    0, 0, 0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200, 220, 240, 260, 280
};

static bool eval_initialized = false;
static int16_t pst_mg[PIECE_TYPE_NB][BOARD_SQUARES];
static int16_t pst_eg[PIECE_TYPE_NB][BOARD_SQUARES];
static Bitboard file_masks[8];
static EvalPawnHashEntry pawn_hash_table[EVAL_PAWN_HASH_SIZE];
static EvalPawnHashStats pawn_hash_stats;

static int eval_abs(int value) {
    return value < 0 ? -value : value;
}

static int eval_clamp(int value, int minimum, int maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int eval_lookup_bonus(const int *table, int count, int max_index) {
    int index = count;

    if (index < 0) {
        index = 0;
    } else if (index > max_index) {
        index = max_index;
    }

    return table[index];
}

static int eval_mirror_square(int square) {
    return square ^ 56;
}

static int eval_relative_square(Color color, int square) {
    return color == WHITE ? square : eval_mirror_square(square);
}

static int eval_center_score(int file, int rank) {
    int file_score = 7 - eval_abs((file * 2) - 7);
    int rank_score = 7 - eval_abs((rank * 2) - 7);

    return file_score + rank_score;
}

static int eval_long_diagonal_score(int file, int rank) {
    int main_diagonal = 7 - eval_abs(file - rank);
    int anti_diagonal = 7 - eval_abs((7 - file) - rank);

    return main_diagonal > anti_diagonal ? main_diagonal : anti_diagonal;
}

static bool eval_square_attacked_by_pawns(Color attacker, Bitboard pawns, int square) {
    return (movegen_pawn_attacks[attacker == WHITE ? BLACK : WHITE][square] & pawns) != 0;
}

static bool eval_side_has_castled_king(Color side, int king_square) {
    int file = bitboard_file_of(king_square);
    int relative_rank = side == WHITE ? bitboard_rank_of(king_square) : 7 - bitboard_rank_of(king_square);

    return relative_rank <= 1 && (file <= 2 || file >= 5);
}

static bool eval_file_has_any_pawn(Bitboard white_pawns, Bitboard black_pawns, int file) {
    return ((white_pawns | black_pawns) & file_masks[file]) != 0;
}

static bool eval_file_has_friendly_pawn(Bitboard pawns, int file) {
    return (pawns & file_masks[file]) != 0;
}

static bool eval_is_isolated_pawn(Bitboard own_pawns, int square) {
    int file = bitboard_file_of(square);

    if (file > 0 && (own_pawns & file_masks[file - 1]) != 0) {
        return false;
    }
    if (file < 7 && (own_pawns & file_masks[file + 1]) != 0) {
        return false;
    }

    return true;
}

static bool eval_is_connected_pawn(const Position *pos, Color side, int square) {
    int file = bitboard_file_of(square);
    int rank = bitboard_rank_of(square);
    int file_offset;

    for (file_offset = -1; file_offset <= 1; file_offset += 2) {
        int adjacent_file = file + file_offset;
        int rank_offset;

        if (adjacent_file < 0 || adjacent_file > 7) {
            continue;
        }

        for (rank_offset = -1; rank_offset <= 1; ++rank_offset) {
            int adjacent_rank = rank + rank_offset;
            Piece expected_pawn;

            if (adjacent_rank < 0 || adjacent_rank > 7) {
                continue;
            }

            expected_pawn = make_piece(side, PAWN);
            if (position_get_piece(pos, bitboard_make_square(adjacent_file, adjacent_rank)) == expected_pawn) {
                return true;
            }
        }
    }

    return false;
}

static bool eval_is_passed_pawn(Bitboard enemy_pawns, Color side, int square) {
    int file = bitboard_file_of(square);
    int rank = bitboard_rank_of(square);
    int direction = side == WHITE ? 1 : -1;
    int next_rank;

    for (next_rank = rank + direction; next_rank >= 0 && next_rank < 8; next_rank += direction) {
        int file_offset;

        for (file_offset = -1; file_offset <= 1; ++file_offset) {
            int current_file = file + file_offset;

            if (current_file < 0 || current_file > 7) {
                continue;
            }

            if ((enemy_pawns & BITBOARD_FROM_SQUARE(bitboard_make_square(current_file, next_rank))) != 0) {
                return false;
            }
        }
    }

    return true;
}

static bool eval_is_backward_pawn(
    const Position *pos,
    Color side,
    Bitboard own_pawns,
    Bitboard enemy_pawns,
    int square
) {
    int file = bitboard_file_of(square);
    int rank = bitboard_rank_of(square);
    int relative_rank = side == WHITE ? rank : 7 - rank;
    int forward_square = square + (side == WHITE ? 8 : -8);

    if (relative_rank <= 1 || !square_is_valid(forward_square)) {
        return false;
    }

    if (eval_square_attacked_by_pawns(side, own_pawns, square)) {
        return false;
    }

    if (position_get_piece(pos, forward_square) != NO_PIECE) {
        return true;
    }

    if (eval_square_attacked_by_pawns(side == WHITE ? BLACK : WHITE, enemy_pawns, forward_square)) {
        return true;
    }

    if (file > 0 && (own_pawns & file_masks[file - 1]) != 0) {
        return false;
    }
    if (file < 7 && (own_pawns & file_masks[file + 1]) != 0) {
        return false;
    }

    return true;
}

static int eval_pawn_mobility(const Position *pos, Color side, int square) {
    int mobility = 0;
    int direction = side == WHITE ? 8 : -8;
    int start_rank = side == WHITE ? 1 : 6;
    int single_push = square + direction;
    int double_push = square + (direction * 2);

    if (square_is_valid(single_push) && position_get_piece(pos, single_push) == NO_PIECE) {
        ++mobility;

        if (bitboard_rank_of(square) == start_rank &&
            square_is_valid(double_push) &&
            position_get_piece(pos, double_push) == NO_PIECE) {
            ++mobility;
        }
    }

    mobility += bitboard_popcount(
        movegen_pawn_attacks[side][square] & pos->occupancy[side == WHITE ? BLACK : WHITE]
    );

    if (pos->en_passant_sq != NO_SQUARE &&
        (movegen_pawn_attacks[side][square] & BITBOARD_FROM_SQUARE(pos->en_passant_sq)) != 0) {
        ++mobility;
    }

    return mobility;
}

static bool eval_is_knight_outpost(Bitboard own_pawns, Bitboard enemy_pawns, Color side, int square) {
    int file = bitboard_file_of(square);
    int rank = bitboard_rank_of(square);
    int relative_rank = side == WHITE ? rank : 7 - rank;
    int enemy_rank;

    if (relative_rank < 3 || relative_rank > 5) {
        return false;
    }

    if (!eval_square_attacked_by_pawns(side, own_pawns, square)) {
        return false;
    }

    if (side == WHITE) {
        for (enemy_rank = rank + 1; enemy_rank < 8; ++enemy_rank) {
            if ((file > 0 && (enemy_pawns & BITBOARD_FROM_SQUARE(bitboard_make_square(file - 1, enemy_rank))) != 0) ||
                (file < 7 && (enemy_pawns & BITBOARD_FROM_SQUARE(bitboard_make_square(file + 1, enemy_rank))) != 0)) {
                return false;
            }
        }
    } else {
        for (enemy_rank = rank - 1; enemy_rank >= 0; --enemy_rank) {
            if ((file > 0 && (enemy_pawns & BITBOARD_FROM_SQUARE(bitboard_make_square(file - 1, enemy_rank))) != 0) ||
                (file < 7 && (enemy_pawns & BITBOARD_FROM_SQUARE(bitboard_make_square(file + 1, enemy_rank))) != 0)) {
                return false;
            }
        }
    }

    return true;
}

static void eval_init_pst_tables(void) {
    int square;

    for (square = A1; square <= H8; ++square) {
        int file = bitboard_file_of(square);
        int rank = bitboard_rank_of(square);
        int center = eval_center_score(file, rank);
        int long_diagonal = eval_long_diagonal_score(file, rank);
        int pawn_center_bonus = (file == 3 || file == 4) ? 6 : (file == 2 || file == 5 ? 3 : 0);
        int king_castle_bonus = (file == 2 || file == 6) ? 24 : ((file == 1 || file == 5) ? 12 : 0);

        pst_mg[PAWN][square] = (int16_t) (-8 + (rank * 10) + pawn_center_bonus + (center / 3));
        pst_eg[PAWN][square] = (int16_t) (-4 + (rank * 14) + pawn_center_bonus + (center / 4));

        pst_mg[KNIGHT][square] = (int16_t) eval_clamp(-32 + (center * 4) + rank, -40, 36);
        pst_eg[KNIGHT][square] = (int16_t) eval_clamp(-22 + (center * 3), -28, 24);

        pst_mg[BISHOP][square] = (int16_t) eval_clamp(-10 + (center * 2) + (long_diagonal * 2), -18, 28);
        pst_eg[BISHOP][square] = (int16_t) eval_clamp(-6 + center + (long_diagonal * 2), -12, 22);

        pst_mg[ROOK][square] = (int16_t) eval_clamp(-4 + (center / 2) + (rank * 2) + (rank == 6 ? 8 : 0), -10, 20);
        pst_eg[ROOK][square] = (int16_t) eval_clamp(-2 + center + (rank * 2), -8, 22);

        pst_mg[QUEEN][square] = (int16_t) eval_clamp(-8 + center + (rank / 2), -14, 18);
        pst_eg[QUEEN][square] = (int16_t) eval_clamp(-4 + center + rank, -8, 22);

        pst_mg[KING][square] = (int16_t) eval_clamp(
            18 + king_castle_bonus - (center * 3) - (rank * 8),
            -50,
            38
        );
        pst_eg[KING][square] = (int16_t) eval_clamp(-18 + (center * 4), -24, 32);
    }
}

static void eval_init_file_masks(void) {
    int file;
    int rank;

    for (file = 0; file < 8; ++file) {
        file_masks[file] = 0;
        for (rank = 0; rank < 8; ++rank) {
            file_masks[file] |= BITBOARD_FROM_SQUARE(bitboard_make_square(file, rank));
        }
    }
}

static void eval_add_material_terms(const Position *pos, int *mg_score, int *eg_score) {
    int piece_type;

    for (piece_type = PAWN; piece_type <= QUEEN; ++piece_type) {
        int white_count = bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(WHITE, (PieceType) piece_type))]);
        int black_count = bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(BLACK, (PieceType) piece_type))]);
        int difference = white_count - black_count;

        *mg_score += difference * material_mg[piece_type];
        *eg_score += difference * material_eg[piece_type];
    }
}

static void eval_compute_pawn_structure_side(
    const Position *pos,
    Color side,
    Bitboard own_pawns,
    Bitboard enemy_pawns,
    int *mg_score,
    int *eg_score
) {
    int file;
    Bitboard pawns = own_pawns;

    for (file = 0; file < 8; ++file) {
        int pawns_on_file = bitboard_popcount(own_pawns & file_masks[file]);

        if (pawns_on_file > 1) {
            *mg_score -= 20 * (pawns_on_file - 1);
            *eg_score -= 20 * (pawns_on_file - 1);
        }
    }

    while (pawns != 0) {
        int square = bitboard_pop_lsb(&pawns);
        int relative_rank = side == WHITE ? bitboard_rank_of(square) : 7 - bitboard_rank_of(square);

        if (eval_is_isolated_pawn(own_pawns, square)) {
            *mg_score -= 15;
            *eg_score -= 15;
        }

        if (eval_is_connected_pawn(pos, side, square)) {
            *mg_score += 10;
            *eg_score += 10;
        }

        if (eval_is_passed_pawn(enemy_pawns, side, square)) {
            *mg_score += passed_pawn_bonus_mg[relative_rank];
            *eg_score += passed_pawn_bonus_eg[relative_rank];
        } else if (eval_is_backward_pawn(pos, side, own_pawns, enemy_pawns, square)) {
            *mg_score -= 10;
            *eg_score -= 10;
        }
    }
}

static void eval_probe_pawn_structure(const Position *pos, int *mg_score, int *eg_score) {
    Bitboard white_pawns;
    Bitboard black_pawns;
    EvalPawnHashEntry *entry;
    int white_mg = 0;
    int white_eg = 0;
    int black_mg = 0;
    int black_eg = 0;

    pawn_hash_stats.probes++;

    entry = &pawn_hash_table[pos->pawn_hash & (EVAL_PAWN_HASH_SIZE - 1)];
    if (entry->valid != 0 && entry->key == pos->pawn_hash) {
        pawn_hash_stats.hits++;
        *mg_score += entry->mg_score;
        *eg_score += entry->eg_score;
        return;
    }

    white_pawns = pos->piece_bitboards[piece_bitboard_index(W_PAWN)];
    black_pawns = pos->piece_bitboards[piece_bitboard_index(B_PAWN)];

    eval_compute_pawn_structure_side(pos, WHITE, white_pawns, black_pawns, &white_mg, &white_eg);
    eval_compute_pawn_structure_side(pos, BLACK, black_pawns, white_pawns, &black_mg, &black_eg);

    white_mg -= black_mg;
    white_eg -= black_eg;

    *mg_score += white_mg;
    *eg_score += white_eg;

    entry->valid = 1;
    entry->key = pos->pawn_hash;
    entry->mg_score = (int16_t) white_mg;
    entry->eg_score = (int16_t) white_eg;
    pawn_hash_stats.stores++;
}

static void eval_add_piece_activity_side(const Position *pos, Color side, int *mg_score, int *eg_score) {
    Bitboard own_occupancy = pos->occupancy[side];
    Bitboard all_occupancy = pos->occupancy[BOTH];
    Bitboard own_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))];
    Bitboard enemy_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, PAWN))];
    Bitboard pieces;
    int sign = side == WHITE ? 1 : -1;
    int bishop_count = bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(side, BISHOP))]);

    if (bishop_count >= 2) {
        *mg_score += sign * 35;
        *eg_score += sign * 35;
    }

    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))];
    while (pieces != 0) {
        int square = bitboard_pop_lsb(&pieces);
        int mobility = eval_pawn_mobility(pos, side, square);

        *mg_score += sign * eval_lookup_bonus(pawn_mobility_mg, mobility, 4);
        *eg_score += sign * eval_lookup_bonus(pawn_mobility_eg, mobility, 4);
    }

    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, KNIGHT))];
    while (pieces != 0) {
        int square = bitboard_pop_lsb(&pieces);
        /* Mobility excluding squares attacked by enemy pawns */
        Bitboard enemy_pawn_attacks = 0;
        int sq2;
        Bitboard ep = enemy_pawns;
        while (ep != 0) {
            sq2 = bitboard_pop_lsb(&ep);
            enemy_pawn_attacks |= movegen_pawn_attacks[side == WHITE ? BLACK : WHITE][sq2];
        }
        int mobility = bitboard_popcount(movegen_knight_attacks[square] & ~own_occupancy & ~enemy_pawn_attacks);

        *mg_score += sign * eval_lookup_bonus(knight_mobility_mg, mobility, 8);
        *eg_score += sign * eval_lookup_bonus(knight_mobility_eg, mobility, 8);

        if (eval_is_knight_outpost(own_pawns, enemy_pawns, side, square)) {
            *mg_score += sign * 25;
            *eg_score += sign * 12;
        }
    }

    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, BISHOP))];
    while (pieces != 0) {
        int square = bitboard_pop_lsb(&pieces);
        /* Mobility excluding squares attacked by enemy pawns */
        Bitboard enemy_pawn_attacks = 0;
        int sq2;
        Bitboard ep = enemy_pawns;
        while (ep != 0) {
            sq2 = bitboard_pop_lsb(&ep);
            enemy_pawn_attacks |= movegen_pawn_attacks[side == WHITE ? BLACK : WHITE][sq2];
        }
        int mobility = bitboard_popcount(movegen_bishop_attacks(square, all_occupancy) & ~own_occupancy & ~enemy_pawn_attacks);

        *mg_score += sign * eval_lookup_bonus(bishop_mobility_mg, mobility, 13);
        *eg_score += sign * eval_lookup_bonus(bishop_mobility_eg, mobility, 13);
    }

    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, ROOK))];
    while (pieces != 0) {
        int square = bitboard_pop_lsb(&pieces);
        int file = bitboard_file_of(square);
        int relative_rank = side == WHITE ? bitboard_rank_of(square) : 7 - bitboard_rank_of(square);
        int mobility = bitboard_popcount(movegen_rook_attacks(square, all_occupancy) & ~own_occupancy);
        bool friendly_pawn_on_file = eval_file_has_friendly_pawn(own_pawns, file);
        bool enemy_pawn_on_file = eval_file_has_friendly_pawn(enemy_pawns, file);

        *mg_score += sign * eval_lookup_bonus(rook_mobility_mg, mobility, 14);
        *eg_score += sign * eval_lookup_bonus(rook_mobility_eg, mobility, 14);

        if (!friendly_pawn_on_file && !enemy_pawn_on_file) {
            *mg_score += sign * 20;
            *eg_score += sign * 15;
        } else if (!friendly_pawn_on_file) {
            *mg_score += sign * 10;
            *eg_score += sign * 8;
        }

        if (relative_rank == 6) {
            *mg_score += sign * 20;
            *eg_score += sign * 20;
        }
    }

    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, QUEEN))];
    while (pieces != 0) {
        int square = bitboard_pop_lsb(&pieces);
        int mobility = bitboard_popcount(movegen_queen_attacks(square, all_occupancy) & ~own_occupancy);

        *mg_score += sign * eval_lookup_bonus(queen_mobility_mg, mobility, 27);
        *eg_score += sign * eval_lookup_bonus(queen_mobility_eg, mobility, 27);
    }

    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, KING))];
    while (pieces != 0) {
        int square = bitboard_pop_lsb(&pieces);
        int mobility = bitboard_popcount(movegen_king_attacks[square] & ~own_occupancy);

        *mg_score += sign * eval_lookup_bonus(king_mobility_mg, mobility, 8);
        *eg_score += sign * eval_lookup_bonus(king_mobility_eg, mobility, 8);
    }
}

static void eval_add_trapped_pieces_side(const Position *pos, Color side, int *mg_score, int *eg_score) {
    int sign = side == WHITE ? 1 : -1;
    Bitboard own_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))];
    Bitboard pieces;
    int king_sq = bitboard_lsb(pos->piece_bitboards[piece_bitboard_index(make_piece(side, KING))]);

    /* Trapped bishop: bishop on a7/h7 with own pawn blocking retreat (a6/h6 with pawn on b7/g7) */
    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, BISHOP))];
    while (pieces != 0) {
        int sq = bitboard_pop_lsb(&pieces);
        int file = bitboard_file_of(sq);
        int rank = bitboard_rank_of(sq);
        int relative_rank = side == WHITE ? rank : 7 - rank;

        /* Bishop trapped on a7/a2 by pawn on b6/b3 */
        if (file == 0 && relative_rank == 6) {
            int blocking_sq = bitboard_make_square(1, side == WHITE ? 5 : 2);
            if (position_get_piece(pos, blocking_sq) == make_piece(side, PAWN)) {
                *mg_score -= sign * 80;
                *eg_score -= sign * 40;
            }
        }
        /* Bishop trapped on h7/h2 by pawn on g6/g3 */
        if (file == 7 && relative_rank == 6) {
            int blocking_sq = bitboard_make_square(6, side == WHITE ? 5 : 2);
            if (position_get_piece(pos, blocking_sq) == make_piece(side, PAWN)) {
                *mg_score -= sign * 80;
                *eg_score -= sign * 40;
            }
        }

        /* Bad bishop: bishop on same color as many own pawns */
        {
            int bishop_color = (file + rank) % 2; /* 0=light, 1=dark */
            int pawns_on_color = 0;
            Bitboard pawns = own_pawns;
            while (pawns != 0) {
                int psq = bitboard_pop_lsb(&pawns);
                if ((bitboard_file_of(psq) + bitboard_rank_of(psq)) % 2 == bishop_color) {
                    pawns_on_color++;
                }
            }
            if (pawns_on_color >= 4) {
                *mg_score -= sign * (pawns_on_color * 5);
            }
        }
    }

    /* Trapped rook: rook on a1/h1 with uncastled king on b1/g1 blocking it */
    pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side, ROOK))];
    while (pieces != 0) {
        int sq = bitboard_pop_lsb(&pieces);
        int file = bitboard_file_of(sq);
        int relative_rank = side == WHITE ? bitboard_rank_of(sq) : 7 - bitboard_rank_of(sq);
        int king_file = bitboard_file_of(king_sq);

        /* Rook trapped on back rank by uncastled king */
        if (relative_rank == 0 && (file == 0 || file == 7)) {
            if ((king_file == 1 && file == 0) || (king_file == 6 && file == 7)) {
                /* Rook blocked by king - only penalize if no castling rights */
                if (!(pos->castling_rights & (side == WHITE ? (CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE)
                    : (CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE)))) {
                    *mg_score -= sign * 30;
                    *eg_score -= sign * 10;
                }
            }
        }
    }
}

static void eval_add_king_safety_side(const Position *pos, Color side, int *mg_score, int *eg_score) {
    Bitboard own_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))];
    Bitboard enemy_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, PAWN))];
    Bitboard enemy_knights = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, KNIGHT))];
    Bitboard enemy_bishops = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, BISHOP))];
    Bitboard enemy_rooks = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, ROOK))];
    Bitboard enemy_queens = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, QUEEN))];
    Bitboard enemy_pieces;
    Bitboard king_zone;
    int king_square = bitboard_lsb(pos->piece_bitboards[piece_bitboard_index(make_piece(side, KING))]);
    int king_file;
    int king_rank;
    int weighted_attack = 0;
    int sign = side == WHITE ? 1 : -1;
    int file;

    if (king_square == NO_SQUARE) {
        return;
    }

    king_file = bitboard_file_of(king_square);
    king_rank = bitboard_rank_of(king_square);

    /* King zone: king attacks + king square + extended zone */
    king_zone = movegen_king_attacks[king_square] | BITBOARD_FROM_SQUARE(king_square);

    /* Pawn shield bonus for castled king */
    if (eval_side_has_castled_king(side, king_square)) {
        int forward = side == WHITE ? 1 : -1;
        int base_rank = bitboard_rank_of(king_square);

        for (file = king_file - 1; file <= king_file + 1; ++file) {
            int first_rank;
            int second_rank;

            if (file < 0 || file > 7) {
                continue;
            }

            first_rank = base_rank + forward;
            second_rank = base_rank + (2 * forward);

            if (first_rank >= 0 && first_rank < 8 &&
                position_get_piece(pos, bitboard_make_square(file, first_rank)) == make_piece(side, PAWN)) {
                *mg_score += sign * 14;
            }

            if (second_rank >= 0 && second_rank < 8 &&
                position_get_piece(pos, bitboard_make_square(file, second_rank)) == make_piece(side, PAWN)) {
                *mg_score += sign * 7;
            }
        }
    }

    /* Open/semi-open files near king */
    for (file = king_file - 1; file <= king_file + 1; ++file) {
        bool has_friendly_pawn;

        if (file < 0 || file > 7) {
            continue;
        }

        has_friendly_pawn = eval_file_has_friendly_pawn(own_pawns, file);
        if (!eval_file_has_any_pawn(own_pawns, enemy_pawns, file)) {
            *mg_score -= sign * 30;
        } else if (!has_friendly_pawn) {
            *mg_score -= sign * 10;
        }
    }

    /* Pawn storm detection: enemy pawns advancing toward our king */
    {
        int direction = side == WHITE ? 1 : -1;
        int king_rel_rank = side == WHITE ? king_rank : 7 - king_rank;
        Bitboard storm_pawns = 0;

        for (file = king_file - 1; file <= king_file + 1; ++file) {
            if (file < 0 || file > 7) continue;

            storm_pawns = enemy_pawns & file_masks[file];
            while (storm_pawns != 0) {
                int sq = bitboard_pop_lsb(&storm_pawns);
                int rel_rank = side == WHITE ? bitboard_rank_of(sq) : 7 - bitboard_rank_of(sq);
                int distance = king_rel_rank - rel_rank;

                if (distance > 0 && distance <= 3) {
                    *mg_score -= sign * (20 - distance * 5);
                }
            }
        }
    }

    /* Weighted attack counting using attack_weights table */
    enemy_pieces = enemy_knights;
    while (enemy_pieces != 0) {
        int square = bitboard_pop_lsb(&enemy_pieces);
        if ((movegen_knight_attacks[square] & king_zone) != 0) {
            weighted_attack += attack_weights[KNIGHT] * 20;
        }
    }

    enemy_pieces = enemy_bishops;
    while (enemy_pieces != 0) {
        int square = bitboard_pop_lsb(&enemy_pieces);
        if ((movegen_bishop_attacks(square, pos->occupancy[BOTH]) & king_zone) != 0) {
            weighted_attack += attack_weights[BISHOP] * 20;
        }
    }

    enemy_pieces = enemy_rooks;
    while (enemy_pieces != 0) {
        int square = bitboard_pop_lsb(&enemy_pieces);
        if ((movegen_rook_attacks(square, pos->occupancy[BOTH]) & king_zone) != 0) {
            weighted_attack += attack_weights[ROOK] * 20;
        }
    }

    enemy_pieces = enemy_queens;
    while (enemy_pieces != 0) {
        int square = bitboard_pop_lsb(&enemy_pieces);
        if ((movegen_queen_attacks(square, pos->occupancy[BOTH]) & king_zone) != 0) {
            weighted_attack += attack_weights[QUEEN] * 20;
        }
    }

    /* Apply weighted attack penalty */
    if (weighted_attack > 0) {
        int weight_index = weighted_attack / 20;
        if (weight_index > 16) weight_index = 16;

        *mg_score -= sign * attack_weight_bonus[weight_index];
        *eg_score -= sign * (attack_weight_bonus[weight_index] / 3);
    }
}

void eval_init(void) {
    if (eval_initialized) {
        return;
    }

    movegen_init();
    eval_init_file_masks();
    eval_init_pst_tables();
    eval_initialized = true;
}

void eval_update_piece_square_state(Position *pos, Piece piece, int square, int delta) {
    Color color;
    PieceType type;
    int relative_square;

    if (pos == NULL || !piece_is_valid(piece) || !square_is_valid(square) || delta == 0) {
        return;
    }

    eval_init();
    color = (Color) piece_color(piece);
    type = (PieceType) piece_type(piece);
    relative_square = eval_relative_square(color, square);

    pos->pst_mg[color] = (int16_t) (pos->pst_mg[color] + (pst_mg[type][relative_square] * delta));
    pos->pst_eg[color] = (int16_t) (pos->pst_eg[color] + (pst_eg[type][relative_square] * delta));
}

void eval_refresh_position_state(Position *pos) {
    int square;

    if (pos == NULL) {
        return;
    }

    eval_init();
    pos->pst_mg[WHITE] = 0;
    pos->pst_mg[BLACK] = 0;
    pos->pst_eg[WHITE] = 0;
    pos->pst_eg[BLACK] = 0;

    for (square = A1; square <= H8; ++square) {
        Piece piece = position_get_piece(pos, square);

        if (piece_is_valid(piece)) {
            eval_update_piece_square_state(pos, piece, square, 1);
        }
    }
}

static void eval_add_space_threat_terms(const Position *pos, int *mg_score, int *eg_score) {
    Color side;

    for (side = WHITE; side <= BLACK; ++side) {
        Color enemy = side == WHITE ? BLACK : WHITE;
        Bitboard own_pieces;
        Bitboard enemy_attacks;
        int sign = side == WHITE ? 1 : -1;
        int space_bonus = 0;
        int threat_penalty = 0;

        /* Compute all squares attacked by the enemy */
        enemy_attacks = 0;
        {
            Bitboard pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(enemy, PAWN))];
            while (pawns != 0) {
                int sq = bitboard_pop_lsb(&pawns);
                enemy_attacks |= movegen_pawn_attacks[enemy][sq];
            }
        }
        {
            Bitboard knights = pos->piece_bitboards[piece_bitboard_index(make_piece(enemy, KNIGHT))];
            while (knights != 0) {
                int sq = bitboard_pop_lsb(&knights);
                enemy_attacks |= movegen_knight_attacks[sq];
            }
        }
        {
            Bitboard bishops = pos->piece_bitboards[piece_bitboard_index(make_piece(enemy, BISHOP))];
            while (bishops != 0) {
                int sq = bitboard_pop_lsb(&bishops);
                enemy_attacks |= movegen_bishop_attacks(sq, pos->occupancy[BOTH]);
            }
        }
        {
            Bitboard rooks = pos->piece_bitboards[piece_bitboard_index(make_piece(enemy, ROOK))];
            while (rooks != 0) {
                int sq = bitboard_pop_lsb(&rooks);
                enemy_attacks |= movegen_rook_attacks(sq, pos->occupancy[BOTH]);
            }
        }
        {
            Bitboard queens = pos->piece_bitboards[piece_bitboard_index(make_piece(enemy, QUEEN))];
            while (queens != 0) {
                int sq = bitboard_pop_lsb(&queens);
                enemy_attacks |= movegen_queen_attacks(sq, pos->occupancy[BOTH]);
            }
        }
        {
            int king_sq = bitboard_lsb(pos->piece_bitboards[piece_bitboard_index(make_piece(enemy, KING))]);
            if (king_sq != NO_SQUARE) {
                enemy_attacks |= movegen_king_attacks[king_sq];
            }
        }

        /* Space: count own pieces controlling squares in the opponent's half */
        {
            Bitboard own_reach = 0;
            Bitboard own_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))];

            /* Pawn-controlled squares in opponent's half */
            while (own_pawns != 0) {
                int sq = bitboard_pop_lsb(&own_pawns);
                own_reach |= movegen_pawn_attacks[side][sq];
            }
            {
                Bitboard knights = pos->piece_bitboards[piece_bitboard_index(make_piece(side, KNIGHT))];
                while (knights != 0) {
                    int sq = bitboard_pop_lsb(&knights);
                    own_reach |= movegen_knight_attacks[sq];
                }
            }
            {
                Bitboard bishops = pos->piece_bitboards[piece_bitboard_index(make_piece(side, BISHOP))];
                while (bishops != 0) {
                    int sq = bitboard_pop_lsb(&bishops);
                    own_reach |= movegen_bishop_attacks(sq, pos->occupancy[BOTH]);
                }
            }

            /* Count space on opponent's half */
            if (side == WHITE) {
                Bitboard opponent_half = 0xFFFFFFFF00000000ULL; /* ranks 4-7 */
                space_bonus = bitboard_popcount(own_reach & opponent_half & ~enemy_attacks);
            } else {
                Bitboard opponent_half = 0x00000000FFFFFFFFULL; /* ranks 0-3 */
                space_bonus = bitboard_popcount(own_reach & opponent_half & ~enemy_attacks);
            }
        }

        /* Threats: pieces attacked by less valuable enemy pieces */
        own_pieces = pos->occupancy[side] & ~pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))] &
                     ~pos->piece_bitboards[piece_bitboard_index(make_piece(side, KING))];
        while (own_pieces != 0) {
            int sq = bitboard_pop_lsb(&own_pieces);
            Piece piece = position_get_piece(pos, sq);
            PieceType pt = piece_type(piece);

            /* Check if attacked by a pawn (cheap attacker) */
            if ((enemy_attacks & BITBOARD_FROM_SQUARE(sq)) != 0) {
                Bitboard enemy_pawn_att = movegen_pawn_attacks[side][sq] &
                    pos->piece_bitboards[piece_bitboard_index(make_piece(enemy, PAWN))];
                if (enemy_pawn_att != 0 && pt > PAWN) {
                    threat_penalty += (pt == KNIGHT || pt == BISHOP) ? 30 : (pt == ROOK ? 40 : 60);
                }
            }
        }

        *mg_score += sign * space_bonus * 3;
        *eg_score += sign * space_bonus;
        *mg_score -= sign * threat_penalty;
        *eg_score -= sign * (threat_penalty / 2);
    }
}

static void eval_add_endgame_terms(const Position *pos, int *mg_score, int *eg_score) {
    Color side;
    Bitboard white_pawns = pos->piece_bitboards[piece_bitboard_index(W_PAWN)];
    Bitboard black_pawns = pos->piece_bitboards[piece_bitboard_index(B_PAWN)];

    for (side = WHITE; side <= BLACK; ++side) {
        Bitboard own_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side, PAWN))];
        Bitboard enemy_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, PAWN))];
        Bitboard own_rooks = pos->piece_bitboards[piece_bitboard_index(make_piece(side, ROOK))];
        Bitboard enemy_king_bb = pos->piece_bitboards[piece_bitboard_index(make_piece(side == WHITE ? BLACK : WHITE, KING))];
        Bitboard own_king_bb = pos->piece_bitboards[piece_bitboard_index(make_piece(side, KING))];
        int own_king_sq = bitboard_lsb(own_king_bb);
        int enemy_king_sq = bitboard_lsb(enemy_king_bb);
        int sign = side == WHITE ? 1 : -1;
        Bitboard pawns;

        /* Passed pawn vs enemy king proximity in endgame */
        pawns = own_pawns;
        while (pawns != 0) {
            int sq = bitboard_pop_lsb(&pawns);
            int file = bitboard_file_of(sq);
            int rank = bitboard_rank_of(sq);

            if (eval_is_passed_pawn(enemy_pawns, side, sq)) {
                int relative_rank = side == WHITE ? rank : 7 - rank;
                int promo_dist = 7 - relative_rank;

                /* Bonus if the enemy king is far from the promotion square */
                int promo_sq = bitboard_make_square(file, side == WHITE ? 7 : 0);
                int king_dist = eval_abs(bitboard_file_of(enemy_king_sq) - bitboard_file_of(promo_sq)) +
                                eval_abs(bitboard_rank_of(enemy_king_sq) - bitboard_rank_of(promo_sq));
                int own_king_dist = eval_abs(bitboard_file_of(own_king_sq) - bitboard_file_of(sq)) +
                                    eval_abs(bitboard_rank_of(own_king_sq) - bitboard_rank_of(sq));

                /* Closer enemy king = harder to queen, farther = easier */
                if (king_dist > promo_dist + 1) {
                    *eg_score += sign * (20 + (relative_rank >= 5 ? 30 : 0));
                }

                /* Own king close to passed pawn helps support it */
                if (own_king_dist <= 2) {
                    *eg_score += sign * (15 + (relative_rank >= 5 ? 20 : 0));
                }
            }
        }

        /* Rook behind passed pawns */
        pawns = own_pawns;
        while (pawns != 0) {
            int sq = bitboard_pop_lsb(&pawns);
            int file = bitboard_file_of(sq);
            int rank = bitboard_rank_of(sq);

            if (eval_is_passed_pawn(enemy_pawns, side, sq)) {
                /* Check if own rook is behind this pawn (on the same file, behind = lower relative rank) */
                Bitboard file_mask = file_masks[file];
                Bitboard rooks_on_file = own_rooks & file_mask;

                while (rooks_on_file != 0) {
                    int rook_sq = bitboard_pop_lsb(&rooks_on_file);
                    int rook_rel_rank = side == WHITE ? bitboard_rank_of(rook_sq) : 7 - bitboard_rank_of(rook_sq);
                    int pawn_rel_rank = side == WHITE ? rank : 7 - rank;

                    if (rook_rel_rank < pawn_rel_rank) {
                        *eg_score += sign * 20;
                        break;
                    }
                }
            }
        }

        /* King centralization in endgame */
        {
            int king_file = bitboard_file_of(own_king_sq);
            int king_rank = bitboard_rank_of(own_king_sq);
            int center_dist = eval_abs(king_file - 3) + eval_abs(king_rank - 3);
            if (center_dist > 4) center_dist = eval_abs(king_file - 4) + eval_abs(king_rank - 4);
            if (center_dist > 4) center_dist = 4;

            *eg_score += sign * (4 - center_dist) * 8;
        }
    }
}

int eval_calculate_phase(const Position *pos) {
    int piece_type;
    int phase = 0;

    if (pos == NULL) {
        return 0;
    }

    eval_init();

    for (piece_type = KNIGHT; piece_type <= QUEEN; ++piece_type) {
        int white_count = bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(WHITE, (PieceType) piece_type))]);
        int black_count = bitboard_popcount(pos->piece_bitboards[piece_bitboard_index(make_piece(BLACK, (PieceType) piece_type))]);

        phase += (white_count + black_count) * phase_weights[piece_type];
    }

    phase = (phase * 256) / EVAL_OPENING_PHASE_TOTAL;
    return eval_clamp(phase, 0, 256);
}

/* ---- Threat-Aware Evaluation ----
   Evaluate which pieces are threatened by opponent attacks.
   Heavily inspired by Stockfish 18's "Threat Input" features for SFNNv10.
   A threatened piece is worth significantly less than an unthreatened one,
   especially high-value pieces under attack by low-value attackers. */
static void eval_add_threat_terms(const Position *pos, int *mg, int *eg) {
    static const int threat_penalty_mg[PIECE_TYPE_NB] = { 0, 30, 30, 45, 80, 0 };
    static const int threat_penalty_eg[PIECE_TYPE_NB] = { 0, 25, 25, 40, 70, 0 };
    static const int threat_by_pawn_mg[PIECE_TYPE_NB] = { 0, 50, 50, 70, 120, 0 };
    static const int threat_by_pawn_eg[PIECE_TYPE_NB] = { 0, 40, 40, 60, 100, 0 };

    Color side;
    for (side = WHITE; side <= BLACK; ++side) {
        Color opp = side == WHITE ? BLACK : WHITE;
        Bitboard our_pieces = pos->occupancy[side];
        Bitboard their_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(opp, PAWN))];
        Bitboard attacked_by_opp = 0;
        int sign = side == WHITE ? 1 : -1;
        int sq;

        /* Compute all squares attacked by opponent */
        for (sq = 0; sq < BOARD_SQUARES; sq++) {
            if (movegen_is_square_attacked(pos, sq, opp)) {
                attacked_by_opp |= (1ULL << sq);
            }
        }

        /* Check each of our pieces: is it threatened? */
        Bitboard our_threatened = our_pieces & attacked_by_opp;

        while (our_threatened) {
            sq = __builtin_ctzll(our_threatened);
            our_threatened &= our_threatened - 1;

            Piece p = position_get_piece(pos, sq);
            if (!piece_is_valid(p) || piece_type(p) == KING || piece_type(p) == PAWN) continue;
            PieceType pt = piece_type(p);

            /* Is this piece specifically threatened by an enemy pawn? Much worse. */
            bool threatened_by_pawn = (movegen_pawn_attacks[side][sq] & their_pawns) != 0;

            if (threatened_by_pawn) {
                *mg -= sign * threat_by_pawn_mg[pt];
                *eg -= sign * threat_by_pawn_eg[pt];
            } else {
                *mg -= sign * threat_penalty_mg[pt];
                *eg -= sign * threat_penalty_eg[pt];
            }
        }

        /* Bonus for attacking opponent's high-value pieces */
        Bitboard their_pieces = pos->occupancy[opp];
        Bitboard our_attacks = 0;

        for (sq = 0; sq < BOARD_SQUARES; sq++) {
            if (movegen_is_square_attacked(pos, sq, side)) {
                our_attacks |= (1ULL << sq);
            }
        }

        Bitboard our_threats_to_them = their_pieces & our_attacks;
        while (our_threats_to_them) {
            sq = __builtin_ctzll(our_threats_to_them);
            our_threats_to_them &= our_threats_to_them - 1;

            Piece p = position_get_piece(pos, sq);
            if (!piece_is_valid(p) || piece_type(p) == KING || piece_type(p) == PAWN) continue;
            PieceType pt = piece_type(p);

            /* Bonus is half the penalty (asymmetric: attacking > being attacked) */
            *mg += sign * threat_penalty_mg[pt] / 2;
            *eg += sign * threat_penalty_eg[pt] / 2;
        }
    }
}

int eval_evaluate(Position *pos) {
    int mg_score = 0;
    int eg_score = 0;
    int pawn_mg = 0;
    int pawn_eg = 0;
    int phase;
    int score;

    if (pos == NULL) {
        return 0;
    }

    eval_init();

    if (movegen_is_checkmate(pos)) {
        return -EVAL_MATE_SCORE;
    }

    if (movegen_is_stalemate(pos)) {
        return 0;
    }

    if (draw_is_draw(pos)) {
        return draw_score(pos);
    }

    eval_add_material_terms(pos, &mg_score, &eg_score);

    mg_score += pos->pst_mg[WHITE] - pos->pst_mg[BLACK];
    eg_score += pos->pst_eg[WHITE] - pos->pst_eg[BLACK];

    eval_probe_pawn_structure(pos, &pawn_mg, &pawn_eg);
    mg_score += pawn_mg;
    eg_score += pawn_eg;

    eval_add_piece_activity_side(pos, WHITE, &mg_score, &eg_score);
    eval_add_piece_activity_side(pos, BLACK, &mg_score, &eg_score);

    eval_add_trapped_pieces_side(pos, WHITE, &mg_score, &eg_score);
    eval_add_trapped_pieces_side(pos, BLACK, &mg_score, &eg_score);

    eval_add_king_safety_side(pos, WHITE, &mg_score, &eg_score);
    eval_add_king_safety_side(pos, BLACK, &mg_score, &eg_score);

    eval_add_space_threat_terms(pos, &mg_score, &eg_score);

    /* Endgame-specific evaluation */
    {
        int eg_phase = 256 - eval_calculate_phase(pos);
        if (eg_phase > 128) {
            eval_add_endgame_terms(pos, &mg_score, &eg_score);
        }
    }

    /* ---- Threat-Aware Evaluation (Stockfish 18 inspired) ----
       Add bonuses/penalties based on which pieces are threatened.
       Currently disabled for stability — re-enable after tuning. */
    if (0) {
        int threat_mg = 0, threat_eg = 0;
        eval_add_threat_terms(pos, &threat_mg, &threat_eg);
        mg_score += threat_mg;
        eg_score += threat_eg;
    }

    phase = eval_calculate_phase(pos);
    score = ((mg_score * phase) + (eg_score * (256 - phase))) / 256;
    score += pos->side_to_move == WHITE ? EVAL_TEMPO_BONUS : -EVAL_TEMPO_BONUS;

    return pos->side_to_move == WHITE ? score : -score;
}

void eval_reset_pawn_hash_table(void) {
    memset(pawn_hash_table, 0, sizeof(pawn_hash_table));
}

void eval_reset_pawn_hash_stats(void) {
    memset(&pawn_hash_stats, 0, sizeof(pawn_hash_stats));
}

EvalPawnHashStats eval_get_pawn_hash_stats(void) {
    return pawn_hash_stats;
}
