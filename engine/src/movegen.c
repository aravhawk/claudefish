#include "movegen.h"

#include "zobrist.h"

Bitboard movegen_knight_attacks[BOARD_SQUARES];
Bitboard movegen_king_attacks[BOARD_SQUARES];
Bitboard movegen_pawn_attacks[2][BOARD_SQUARES];

static Bitboard bishop_masks[BOARD_SQUARES];
static Bitboard rook_masks[BOARD_SQUARES];
static uint8_t bishop_relevant_bits[BOARD_SQUARES];
static uint8_t rook_relevant_bits[BOARD_SQUARES];
static Bitboard bishop_attack_table[BOARD_SQUARES][512];
static Bitboard rook_attack_table[BOARD_SQUARES][4096];
static bool movegen_initialized = false;

static const uint64_t rook_magics[BOARD_SQUARES] = {
    0x2080004000208010ULL, 0x0480200040008010ULL, 0x9880200080100008ULL, 0x0100100020090004ULL,
    0x0A00041021080200ULL, 0x1200108422000821ULL, 0x1080220000800100ULL, 0x0100008022005100ULL,
    0x0012800221400180ULL, 0x0000400020005000ULL, 0x2001001041002000ULL, 0x402B002500081000ULL,
    0x8404800400800800ULL, 0x6010800400808200ULL, 0x204A000200010804ULL, 0xB806000080420104ULL,
    0x0060388000400086ULL, 0x0020004000500020ULL, 0x0808420012008028ULL, 0x0026020008201340ULL,
    0x0008028008804400ULL, 0x8412008002040080ULL, 0x04A0040010810802ULL, 0x02000200040040B1ULL,
    0x0160400080008020ULL, 0xA000400080200080ULL, 0x0400200280100081ULL, 0x0010001080080085ULL,
    0x4038001100050900ULL, 0x1884000480020080ULL, 0x4001000100040200ULL, 0x0480004200110094ULL,
    0x00C0004022800080ULL, 0x0000802000804000ULL, 0x2000802000801002ULL, 0x0010080080801000ULL,
    0x0081001005000800ULL, 0x8805000401000802ULL, 0x2808100104000802ULL, 0x9400007506000084ULL,
    0x0180002000404002ULL, 0x501001402000C000ULL, 0x4000410020010010ULL, 0x02004222000A0010ULL,
    0x4400080004008080ULL, 0x042C000810020200ULL, 0x0442100801840002ULL, 0x8401000040810002ULL,
    0x00C0208000401880ULL, 0x0001082046028200ULL, 0x8200200010008080ULL, 0x1802002040081200ULL,
    0x84080004000A8080ULL, 0x0023000894001300ULL, 0x0000304288010400ULL, 0x0161004401008200ULL,
    0x3000430218208001ULL, 0x0089001680204001ULL, 0x5281004008200113ULL, 0x6005000420081001ULL,
    0x1002001021040802ULL, 0x081D004204008801ULL, 0x3000082A1011008CULL, 0x0402040081002042ULL
};

static const uint64_t bishop_magics[BOARD_SQUARES] = {
    0x0002040404085200ULL, 0x0404103202003010ULL, 0x0010040048580800ULL, 0x1104040880000A00ULL,
    0x0402021000204604ULL, 0x114202B004924D00ULL, 0x2000880430451080ULL, 0x0000220800845000ULL,
    0x08C4842082040100ULL, 0x0000840800842081ULL, 0x40C0081081060000ULL, 0x0802022082040002ULL,
    0x00020C0308010000ULL, 0x2004820202A10284ULL, 0x00000208042209A2ULL, 0x0800802108221028ULL,
    0x0088006288090800ULL, 0x81A4202011120A00ULL, 0x04B0000103120010ULL, 0x8012000402120080ULL,
    0x04040012010C1000ULL, 0x20198124100C0100ULL, 0x0000942200900803ULL, 0x010880810410C200ULL,
    0x0110050010041002ULL, 0x08701000020C8130ULL, 0xC00C100009004080ULL, 0x0038080000202020ULL,
    0x070084002A802000ULL, 0x0904820008880441ULL, 0xA811040000440441ULL, 0x0081084106005421ULL,
    0x1002024000115040ULL, 0x0460880904441000ULL, 0x4220104401080800ULL, 0x40182008004A8820ULL,
    0x4012008400060021ULL, 0x0C02008200010041ULL, 0x0011484201110100ULL, 0x0002240A50010040ULL,
    0x0614100806006802ULL, 0x0040880808404280ULL, 0x1840840041000800ULL, 0x2004004202200802ULL,
    0x0800080208202400ULL, 0x100401004E000103ULL, 0x0A0404440040CC20ULL, 0x20010A4C00400100ULL,
    0x0016008414420580ULL, 0x1112011088441204ULL, 0x1A00010401040000ULL, 0x0121200210440800ULL,
    0x0802082204240044ULL, 0x0000088208021008ULL, 0x0008021092020C80ULL, 0x0410010104048801ULL,
    0x810204410C1002A0ULL, 0x8524008088088200ULL, 0x9021200864061800ULL, 0x0800500500208810ULL,
    0x0100080004104400ULL, 0x2460404084084880ULL, 0x004108089020A600ULL, 0x0010900208102420ULL
};

static const int pin_rank_deltas[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int pin_file_deltas[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

static int movegen_find_king_square(const Position *pos, Color side) {
    Bitboard king_bitboard;

    if (pos == NULL) {
        return NO_SQUARE;
    }

    king_bitboard = pos->piece_bitboards[piece_bitboard_index(make_piece(side, KING))];
    return bitboard_lsb(king_bitboard);
}

static Piece movegen_make_promotion_piece(Color side, PieceType promotion_piece) {
    if (promotion_piece < KNIGHT || promotion_piece > QUEEN) {
        return NO_PIECE;
    }

    return make_piece(side, promotion_piece);
}

static void movegen_update_castling_rights_for_piece(uint8_t *castling_rights, Piece piece, int square) {
    if (castling_rights == NULL) {
        return;
    }

    switch (piece) {
        case W_KING:
            *castling_rights &= (uint8_t) ~(CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE);
            break;
        case B_KING:
            *castling_rights &= (uint8_t) ~(CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE);
            break;
        case W_ROOK:
            if (square == A1) {
                *castling_rights &= (uint8_t) ~CASTLE_WHITE_QUEENSIDE;
            } else if (square == H1) {
                *castling_rights &= (uint8_t) ~CASTLE_WHITE_KINGSIDE;
            }
            break;
        case B_ROOK:
            if (square == A8) {
                *castling_rights &= (uint8_t) ~CASTLE_BLACK_QUEENSIDE;
            } else if (square == H8) {
                *castling_rights &= (uint8_t) ~CASTLE_BLACK_KINGSIDE;
            }
            break;
        default:
            break;
    }
}

static Bitboard generate_knight_attacks(int square) {
    static const int rank_offsets[8] = { 2, 2, 1, 1, -1, -1, -2, -2 };
    static const int file_offsets[8] = { 1, -1, 2, -2, 2, -2, 1, -1 };
    Bitboard attacks = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int offset;

    for (offset = 0; offset < 8; ++offset) {
        int target_rank = rank + rank_offsets[offset];
        int target_file = file + file_offsets[offset];

        if (target_rank >= 0 && target_rank < 8 && target_file >= 0 && target_file < 8) {
            attacks |= BITBOARD_FROM_SQUARE(bitboard_make_square(target_file, target_rank));
        }
    }

    return attacks;
}

static Bitboard generate_king_attacks(int square) {
    Bitboard attacks = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int rank_delta;
    int file_delta;

    for (rank_delta = -1; rank_delta <= 1; ++rank_delta) {
        for (file_delta = -1; file_delta <= 1; ++file_delta) {
            int target_rank;
            int target_file;

            if (rank_delta == 0 && file_delta == 0) {
                continue;
            }

            target_rank = rank + rank_delta;
            target_file = file + file_delta;

            if (target_rank >= 0 && target_rank < 8 && target_file >= 0 && target_file < 8) {
                attacks |= BITBOARD_FROM_SQUARE(bitboard_make_square(target_file, target_rank));
            }
        }
    }

    return attacks;
}

static Bitboard generate_pawn_attacks(Color color, int square) {
    Bitboard attacks = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int target_rank = rank + (color == WHITE ? 1 : -1);

    if (target_rank < 0 || target_rank >= 8) {
        return 0;
    }

    if (file > 0) {
        attacks |= BITBOARD_FROM_SQUARE(bitboard_make_square(file - 1, target_rank));
    }
    if (file < 7) {
        attacks |= BITBOARD_FROM_SQUARE(bitboard_make_square(file + 1, target_rank));
    }

    return attacks;
}

static Bitboard generate_bishop_mask(int square) {
    Bitboard mask = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int current_rank;
    int current_file;

    current_rank = rank + 1;
    current_file = file + 1;
    while (current_rank <= 6 && current_file <= 6) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(current_file, current_rank));
        ++current_rank;
        ++current_file;
    }

    current_rank = rank + 1;
    current_file = file - 1;
    while (current_rank <= 6 && current_file >= 1) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(current_file, current_rank));
        ++current_rank;
        --current_file;
    }

    current_rank = rank - 1;
    current_file = file + 1;
    while (current_rank >= 1 && current_file <= 6) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(current_file, current_rank));
        --current_rank;
        ++current_file;
    }

    current_rank = rank - 1;
    current_file = file - 1;
    while (current_rank >= 1 && current_file >= 1) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(current_file, current_rank));
        --current_rank;
        --current_file;
    }

    return mask;
}

static Bitboard generate_rook_mask(int square) {
    Bitboard mask = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int current;

    for (current = rank + 1; current <= 6; ++current) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(file, current));
    }
    for (current = rank - 1; current >= 1; --current) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(file, current));
    }
    for (current = file + 1; current <= 6; ++current) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(current, rank));
    }
    for (current = file - 1; current >= 1; --current) {
        mask |= BITBOARD_FROM_SQUARE(bitboard_make_square(current, rank));
    }

    return mask;
}

static Bitboard generate_bishop_attacks_on_the_fly(int square, Bitboard blockers) {
    Bitboard attacks = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int current_rank;
    int current_file;

    current_rank = rank + 1;
    current_file = file + 1;
    while (current_rank < 8 && current_file < 8) {
        int target = bitboard_make_square(current_file, current_rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }

        ++current_rank;
        ++current_file;
    }

    current_rank = rank + 1;
    current_file = file - 1;
    while (current_rank < 8 && current_file >= 0) {
        int target = bitboard_make_square(current_file, current_rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }

        ++current_rank;
        --current_file;
    }

    current_rank = rank - 1;
    current_file = file + 1;
    while (current_rank >= 0 && current_file < 8) {
        int target = bitboard_make_square(current_file, current_rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }

        --current_rank;
        ++current_file;
    }

    current_rank = rank - 1;
    current_file = file - 1;
    while (current_rank >= 0 && current_file >= 0) {
        int target = bitboard_make_square(current_file, current_rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }

        --current_rank;
        --current_file;
    }

    return attacks;
}

static Bitboard generate_rook_attacks_on_the_fly(int square, Bitboard blockers) {
    Bitboard attacks = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int current;

    for (current = rank + 1; current < 8; ++current) {
        int target = bitboard_make_square(file, current);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    for (current = rank - 1; current >= 0; --current) {
        int target = bitboard_make_square(file, current);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    for (current = file + 1; current < 8; ++current) {
        int target = bitboard_make_square(current, rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    for (current = file - 1; current >= 0; --current) {
        int target = bitboard_make_square(current, rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((blockers & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    return attacks;
}

static Bitboard occupancy_from_index(int index, int relevant_bits, Bitboard mask) {
    Bitboard occupancy = 0;
    int bit_index;
    Bitboard remaining = mask;

    for (bit_index = 0; bit_index < relevant_bits; ++bit_index) {
        int square = bitboard_pop_lsb(&remaining);

        if ((index & (1 << bit_index)) != 0) {
            occupancy |= BITBOARD_FROM_SQUARE(square);
        }
    }

    return occupancy;
}

static size_t bishop_magic_index(int square, Bitboard occupancy) {
    Bitboard relevant_occupancy = occupancy & bishop_masks[square];

    return (size_t) ((relevant_occupancy * bishop_magics[square]) >> (64 - bishop_relevant_bits[square]));
}

static size_t rook_magic_index(int square, Bitboard occupancy) {
    Bitboard relevant_occupancy = occupancy & rook_masks[square];

    return (size_t) ((relevant_occupancy * rook_magics[square]) >> (64 - rook_relevant_bits[square]));
}

static void init_leaper_attacks(void) {
    int square;

    for (square = A1; square <= H8; ++square) {
        movegen_knight_attacks[square] = generate_knight_attacks(square);
        movegen_king_attacks[square] = generate_king_attacks(square);
        movegen_pawn_attacks[WHITE][square] = generate_pawn_attacks(WHITE, square);
        movegen_pawn_attacks[BLACK][square] = generate_pawn_attacks(BLACK, square);
    }
}

static void init_slider_tables(void) {
    int square;

    for (square = A1; square <= H8; ++square) {
        int occupancy_index;
        int bishop_variations;
        int rook_variations;

        bishop_masks[square] = generate_bishop_mask(square);
        rook_masks[square] = generate_rook_mask(square);
        bishop_relevant_bits[square] = (uint8_t) bitboard_popcount(bishop_masks[square]);
        rook_relevant_bits[square] = (uint8_t) bitboard_popcount(rook_masks[square]);

        bishop_variations = 1 << bishop_relevant_bits[square];
        for (occupancy_index = 0; occupancy_index < bishop_variations; ++occupancy_index) {
            Bitboard occupancy = occupancy_from_index(occupancy_index, bishop_relevant_bits[square], bishop_masks[square]);
            size_t magic_index = bishop_magic_index(square, occupancy);

            bishop_attack_table[square][magic_index] = generate_bishop_attacks_on_the_fly(square, occupancy);
        }

        rook_variations = 1 << rook_relevant_bits[square];
        for (occupancy_index = 0; occupancy_index < rook_variations; ++occupancy_index) {
            Bitboard occupancy = occupancy_from_index(occupancy_index, rook_relevant_bits[square], rook_masks[square]);
            size_t magic_index = rook_magic_index(square, occupancy);

            rook_attack_table[square][magic_index] = generate_rook_attacks_on_the_fly(square, occupancy);
        }
    }
}

Bitboard movegen_attackers_to(const Position *pos, int square, Color attacker) {
    Bitboard attackers = 0;
    Bitboard attacker_pawns;
    Bitboard attacker_knights;
    Bitboard attacker_bishops;
    Bitboard attacker_rooks;
    Bitboard attacker_queens;
    Bitboard attacker_king;

    if (pos == NULL || !square_is_valid(square) || (attacker != WHITE && attacker != BLACK)) {
        return 0;
    }

    movegen_init();

    attacker_pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(attacker, PAWN))];
    attacker_knights = pos->piece_bitboards[piece_bitboard_index(make_piece(attacker, KNIGHT))];
    attacker_bishops = pos->piece_bitboards[piece_bitboard_index(make_piece(attacker, BISHOP))];
    attacker_rooks = pos->piece_bitboards[piece_bitboard_index(make_piece(attacker, ROOK))];
    attacker_queens = pos->piece_bitboards[piece_bitboard_index(make_piece(attacker, QUEEN))];
    attacker_king = pos->piece_bitboards[piece_bitboard_index(make_piece(attacker, KING))];

    if (attacker == WHITE) {
        attackers |= movegen_pawn_attacks[BLACK][square] & attacker_pawns;
    } else {
        attackers |= movegen_pawn_attacks[WHITE][square] & attacker_pawns;
    }

    attackers |= movegen_knight_attacks[square] & attacker_knights;
    attackers |= movegen_bishop_attacks(square, pos->occupancy[BOTH]) & (attacker_bishops | attacker_queens);
    attackers |= movegen_rook_attacks(square, pos->occupancy[BOTH]) & (attacker_rooks | attacker_queens);
    attackers |= movegen_king_attacks[square] & attacker_king;

    return attackers;
}

bool movegen_is_square_attacked(const Position *pos, int square, Color attacker) {
    return movegen_attackers_to(pos, square, attacker) != 0;
}

bool movegen_is_in_check(const Position *pos, Color side) {
    int king_square;
    Color attacker;

    if (pos == NULL || (side != WHITE && side != BLACK)) {
        return false;
    }

    king_square = movegen_find_king_square(pos, side);
    if (king_square == NO_SQUARE) {
        return false;
    }

    attacker = side == WHITE ? BLACK : WHITE;
    return movegen_is_square_attacked(pos, king_square, attacker);
}

Bitboard movegen_pinned_pieces(const Position *pos, Color side) {
    Bitboard pinned = 0;
    int king_square;
    int king_rank;
    int king_file;
    int direction_index;

    if (pos == NULL || (side != WHITE && side != BLACK)) {
        return 0;
    }

    king_square = movegen_find_king_square(pos, side);
    if (king_square == NO_SQUARE) {
        return 0;
    }

    king_rank = bitboard_rank_of(king_square);
    king_file = bitboard_file_of(king_square);

    for (direction_index = 0; direction_index < 8; ++direction_index) {
        int rank = king_rank + pin_rank_deltas[direction_index];
        int file = king_file + pin_file_deltas[direction_index];
        int candidate_square = NO_SQUARE;

        while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
            int square = bitboard_make_square(file, rank);
            Piece piece = position_get_piece(pos, square);

            if (piece == NO_PIECE) {
                rank += pin_rank_deltas[direction_index];
                file += pin_file_deltas[direction_index];
                continue;
            }

            if ((Color) piece_color(piece) == side) {
                if (candidate_square != NO_SQUARE) {
                    break;
                }

                candidate_square = square;
                rank += pin_rank_deltas[direction_index];
                file += pin_file_deltas[direction_index];
                continue;
            }

            if (candidate_square != NO_SQUARE) {
                bool diagonal = direction_index >= 4;
                int type = piece_type(piece);

                if ((diagonal && (type == BISHOP || type == QUEEN)) ||
                    (!diagonal && (type == ROOK || type == QUEEN))) {
                    pinned |= BITBOARD_FROM_SQUARE(candidate_square);
                }
            }
            break;
        }
    }

    return pinned;
}

static bool move_list_push(MoveList *list, Move move) {
    if (list == NULL || list->count >= MOVEGEN_MAX_MOVES) {
        return false;
    }

    list->moves[list->count++] = move;
    return true;
}

static void move_list_add_regular_moves(const Position *pos, MoveList *list, int source, Bitboard targets) {
    while (targets != 0) {
        int target = bitboard_pop_lsb(&targets);
        Piece captured_piece = position_get_piece(pos, target);
        uint8_t flags = captured_piece == NO_PIECE ? MOVE_FLAG_NONE : MOVE_FLAG_CAPTURE;

        move_list_push(list, move_encode(source, target, flags, captured_piece, MOVE_PROMOTION_NONE));
    }
}

static void move_list_add_promotions(const Position *pos, MoveList *list, int source, int target, uint8_t flags) {
    static const PieceType promotion_pieces[] = { QUEEN, ROOK, BISHOP, KNIGHT };
    Piece captured_piece = position_get_piece(pos, target);
    size_t index;

    for (index = 0; index < sizeof(promotion_pieces) / sizeof(promotion_pieces[0]); ++index) {
        move_list_push(list, move_encode(source, target, flags, captured_piece, promotion_pieces[index]));
    }
}

static void generate_pawn_moves(const Position *pos, MoveList *list, Color side_to_move) {
    Bitboard pawns = pos->piece_bitboards[piece_bitboard_index(make_piece(side_to_move, PAWN))];
    Bitboard enemy_occupancy = pos->occupancy[side_to_move == WHITE ? BLACK : WHITE];
    Bitboard all_occupancy = pos->occupancy[BOTH];

    while (pawns != 0) {
        int source = bitboard_pop_lsb(&pawns);
        int rank = bitboard_rank_of(source);
        int direction = side_to_move == WHITE ? 8 : -8;
        int start_rank = side_to_move == WHITE ? 1 : 6;
        int promotion_rank = side_to_move == WHITE ? 6 : 1;
        int single_push = source + direction;
        Bitboard capture_targets = movegen_pawn_attacks[side_to_move][source] & enemy_occupancy;

        if (square_is_valid(single_push) && (all_occupancy & BITBOARD_FROM_SQUARE(single_push)) == 0) {
            if (rank == promotion_rank) {
                move_list_add_promotions(pos, list, source, single_push, MOVE_FLAG_NONE);
            } else {
                move_list_push(list, move_encode(source, single_push, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE));

                if (rank == start_rank) {
                    int double_push = source + (direction * 2);

                    if (square_is_valid(double_push) &&
                        (all_occupancy & BITBOARD_FROM_SQUARE(double_push)) == 0) {
                        move_list_push(
                            list,
                            move_encode(source, double_push, MOVE_FLAG_DOUBLE_PAWN_PUSH, NO_PIECE, MOVE_PROMOTION_NONE)
                        );
                    }
                }
            }
        }

        while (capture_targets != 0) {
            int target = bitboard_pop_lsb(&capture_targets);

            if (rank == promotion_rank) {
                move_list_add_promotions(pos, list, source, target, MOVE_FLAG_CAPTURE);
            } else {
                move_list_push(
                    list,
                    move_encode(
                        source,
                        target,
                        MOVE_FLAG_CAPTURE,
                        position_get_piece(pos, target),
                        MOVE_PROMOTION_NONE
                    )
                );
            }
        }

        if (pos->en_passant_sq != NO_SQUARE &&
            (movegen_pawn_attacks[side_to_move][source] & BITBOARD_FROM_SQUARE(pos->en_passant_sq)) != 0) {
            move_list_push(
                list,
                move_encode(
                    source,
                    pos->en_passant_sq,
                    MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT,
                    make_piece(side_to_move == WHITE ? BLACK : WHITE, PAWN),
                    MOVE_PROMOTION_NONE
                )
            );
        }
    }
}

static void generate_leaper_moves(const Position *pos, MoveList *list, Color side_to_move, PieceType piece_type, Bitboard attacks[BOARD_SQUARES]) {
    Bitboard pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side_to_move, piece_type))];
    Bitboard own_occupancy = pos->occupancy[side_to_move];

    while (pieces != 0) {
        int source = bitboard_pop_lsb(&pieces);
        Bitboard targets = attacks[source] & ~own_occupancy;

        move_list_add_regular_moves(pos, list, source, targets);
    }
}

static void generate_slider_moves(const Position *pos, MoveList *list, Color side_to_move, PieceType piece_type) {
    Bitboard pieces = pos->piece_bitboards[piece_bitboard_index(make_piece(side_to_move, piece_type))];
    Bitboard own_occupancy = pos->occupancy[side_to_move];
    Bitboard all_occupancy = pos->occupancy[BOTH];

    while (pieces != 0) {
        int source = bitboard_pop_lsb(&pieces);
        Bitboard targets = 0;

        switch (piece_type) {
            case BISHOP:
                targets = movegen_bishop_attacks(source, all_occupancy);
                break;
            case ROOK:
                targets = movegen_rook_attacks(source, all_occupancy);
                break;
            case QUEEN:
                targets = movegen_queen_attacks(source, all_occupancy);
                break;
            default:
                break;
        }

        move_list_add_regular_moves(pos, list, source, targets & ~own_occupancy);
    }
}

static void generate_castling_moves(const Position *pos, MoveList *list, Color side_to_move) {
    Color enemy = side_to_move == WHITE ? BLACK : WHITE;

    if (movegen_is_in_check(pos, side_to_move)) {
        return;
    }

    if (side_to_move == WHITE) {
        if ((pos->castling_rights & CASTLE_WHITE_KINGSIDE) != 0 &&
            position_get_piece(pos, E1) == W_KING &&
            position_get_piece(pos, H1) == W_ROOK &&
            position_get_piece(pos, F1) == NO_PIECE &&
            position_get_piece(pos, G1) == NO_PIECE &&
            !movegen_is_square_attacked(pos, F1, enemy) &&
            !movegen_is_square_attacked(pos, G1, enemy)) {
            move_list_push(list, move_encode(E1, G1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE));
        }

        if ((pos->castling_rights & CASTLE_WHITE_QUEENSIDE) != 0 &&
            position_get_piece(pos, E1) == W_KING &&
            position_get_piece(pos, A1) == W_ROOK &&
            position_get_piece(pos, D1) == NO_PIECE &&
            position_get_piece(pos, C1) == NO_PIECE &&
            position_get_piece(pos, B1) == NO_PIECE &&
            !movegen_is_square_attacked(pos, D1, enemy) &&
            !movegen_is_square_attacked(pos, C1, enemy)) {
            move_list_push(list, move_encode(E1, C1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE));
        }
    } else {
        if ((pos->castling_rights & CASTLE_BLACK_KINGSIDE) != 0 &&
            position_get_piece(pos, E8) == B_KING &&
            position_get_piece(pos, H8) == B_ROOK &&
            position_get_piece(pos, F8) == NO_PIECE &&
            position_get_piece(pos, G8) == NO_PIECE &&
            !movegen_is_square_attacked(pos, F8, enemy) &&
            !movegen_is_square_attacked(pos, G8, enemy)) {
            move_list_push(list, move_encode(E8, G8, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE));
        }

        if ((pos->castling_rights & CASTLE_BLACK_QUEENSIDE) != 0 &&
            position_get_piece(pos, E8) == B_KING &&
            position_get_piece(pos, A8) == B_ROOK &&
            position_get_piece(pos, D8) == NO_PIECE &&
            position_get_piece(pos, C8) == NO_PIECE &&
            position_get_piece(pos, B8) == NO_PIECE &&
            !movegen_is_square_attacked(pos, D8, enemy) &&
            !movegen_is_square_attacked(pos, C8, enemy)) {
            move_list_push(list, move_encode(E8, C8, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE));
        }
    }
}

void movegen_init(void) {
    if (movegen_initialized) {
        return;
    }

    init_leaper_attacks();
    init_slider_tables();
    movegen_initialized = true;
}

Bitboard movegen_bishop_attacks(int square, Bitboard occupancy) {
    if (!square_is_valid(square)) {
        return 0;
    }

    movegen_init();
    return bishop_attack_table[square][bishop_magic_index(square, occupancy)];
}

Bitboard movegen_rook_attacks(int square, Bitboard occupancy) {
    if (!square_is_valid(square)) {
        return 0;
    }

    movegen_init();
    return rook_attack_table[square][rook_magic_index(square, occupancy)];
}

Bitboard movegen_queen_attacks(int square, Bitboard occupancy) {
    return movegen_bishop_attacks(square, occupancy) | movegen_rook_attacks(square, occupancy);
}

void movegen_generate_pseudo_legal(const Position *pos, MoveList *list) {
    Color side_to_move;

    if (list == NULL) {
        return;
    }

    list->count = 0;

    if (pos == NULL) {
        return;
    }

    movegen_init();

    side_to_move = (Color) pos->side_to_move;

    generate_pawn_moves(pos, list, side_to_move);
    generate_leaper_moves(pos, list, side_to_move, KNIGHT, movegen_knight_attacks);
    generate_slider_moves(pos, list, side_to_move, BISHOP);
    generate_slider_moves(pos, list, side_to_move, ROOK);
    generate_slider_moves(pos, list, side_to_move, QUEEN);
    generate_leaper_moves(pos, list, side_to_move, KING, movegen_king_attacks);
    generate_castling_moves(pos, list, side_to_move);
}

bool movegen_make_move(Position *pos, Move move) {
    PositionState *state;
    Color side_to_move;
    Color enemy;
    int source;
    int target;
    int captured_square;
    Piece moving_piece;
    Piece captured_piece;
    Piece target_piece;
    PieceType promotion_piece;
    uint8_t flags;
    uint8_t castling_rights;
    int8_t en_passant_sq = NO_SQUARE;

    if (pos == NULL ||
        pos->state_count >= POSITION_STATE_STACK_CAPACITY ||
        pos->history_count >= POSITION_STATE_STACK_CAPACITY + 1) {
        return false;
    }

    source = move_source(move);
    target = move_target(move);
    flags = move_flags(move);
    promotion_piece = move_promotion_piece(move);

    if (!square_is_valid(source) || !square_is_valid(target)) {
        return false;
    }

    moving_piece = position_get_piece(pos, source);
    if (!piece_is_valid(moving_piece)) {
        return false;
    }

    side_to_move = (Color) pos->side_to_move;
    if ((Color) piece_color(moving_piece) != side_to_move) {
        return false;
    }

    enemy = side_to_move == WHITE ? BLACK : WHITE;
    captured_square = target;
    if ((flags & MOVE_FLAG_EN_PASSANT) != 0) {
        captured_square = target + (side_to_move == WHITE ? -8 : 8);
    }

    captured_piece = square_is_valid(captured_square) ? position_get_piece(pos, captured_square) : NO_PIECE;
    if ((flags & MOVE_FLAG_EN_PASSANT) != 0 && captured_piece != make_piece(enemy, PAWN)) {
        return false;
    }
    if ((flags & MOVE_FLAG_CAPTURE) == 0 && captured_piece != NO_PIECE) {
        return false;
    }
    if ((flags & MOVE_FLAG_CAPTURE) != 0 && captured_piece == NO_PIECE) {
        return false;
    }

    state = &pos->state_stack[pos->state_count++];
    state->castling_rights = pos->castling_rights;
    state->en_passant_sq = pos->en_passant_sq;
    state->halfmove_clock = pos->halfmove_clock;
    state->fullmove_number = pos->fullmove_number;
    state->zobrist_hash = pos->zobrist_hash;
    state->pawn_hash = pos->pawn_hash;
    state->move = move;
    state->captured_piece = (uint8_t) captured_piece;

    if (pos->en_passant_sq != NO_SQUARE) {
        pos->zobrist_hash ^= zobrist_en_passant_keys[bitboard_file_of(pos->en_passant_sq)];
    }
    pos->zobrist_hash ^= zobrist_castling_keys[pos->castling_rights & 0x0F];

    if (!position_clear_piece(pos, source)) {
        return false;
    }
    if (captured_piece != NO_PIECE && !position_clear_piece(pos, captured_square)) {
        return false;
    }

    if ((flags & MOVE_FLAG_CASTLING) != 0) {
        int rook_from = NO_SQUARE;
        int rook_to = NO_SQUARE;

        if (moving_piece == W_KING && target == G1) {
            rook_from = H1;
            rook_to = F1;
        } else if (moving_piece == W_KING && target == C1) {
            rook_from = A1;
            rook_to = D1;
        } else if (moving_piece == B_KING && target == G8) {
            rook_from = H8;
            rook_to = F8;
        } else if (moving_piece == B_KING && target == C8) {
            rook_from = A8;
            rook_to = D8;
        } else {
            return false;
        }

        if (!position_clear_piece(pos, rook_from) ||
            !position_set_piece(pos, rook_to, make_piece(side_to_move, ROOK))) {
            return false;
        }
    }

    target_piece = moving_piece;
    if (promotion_piece != MOVE_PROMOTION_NONE) {
        target_piece = movegen_make_promotion_piece(side_to_move, promotion_piece);
        if (!piece_is_valid(target_piece) || piece_type(moving_piece) != PAWN) {
            return false;
        }
    }

    if (!position_set_piece(pos, target, target_piece)) {
        return false;
    }

    castling_rights = pos->castling_rights;
    movegen_update_castling_rights_for_piece(&castling_rights, moving_piece, source);
    if (captured_piece != NO_PIECE) {
        movegen_update_castling_rights_for_piece(&castling_rights, captured_piece, captured_square);
    }

    if ((flags & MOVE_FLAG_DOUBLE_PAWN_PUSH) != 0) {
        en_passant_sq = (int8_t) (source + (side_to_move == WHITE ? 8 : -8));
    }

    pos->castling_rights = castling_rights;
    pos->en_passant_sq = en_passant_sq;
    pos->halfmove_clock = (piece_type(moving_piece) == PAWN || captured_piece != NO_PIECE)
        ? 0
        : (uint16_t) (state->halfmove_clock + 1);
    pos->fullmove_number = (uint16_t) (state->fullmove_number + (side_to_move == BLACK ? 1 : 0));
    pos->side_to_move = enemy;

    pos->zobrist_hash ^= zobrist_castling_keys[pos->castling_rights & 0x0F];
    if (pos->en_passant_sq != NO_SQUARE) {
        pos->zobrist_hash ^= zobrist_en_passant_keys[bitboard_file_of(pos->en_passant_sq)];
    }
    pos->zobrist_hash ^= zobrist_side_key;
    pos->history_hashes[pos->history_count++] = pos->zobrist_hash;

    return true;
}

bool movegen_make_null_move(Position *pos) {
    PositionState *state;
    Color side_to_move;

    if (pos == NULL ||
        pos->state_count >= POSITION_STATE_STACK_CAPACITY ||
        pos->history_count >= POSITION_STATE_STACK_CAPACITY + 1) {
        return false;
    }

    state = &pos->state_stack[pos->state_count++];
    state->castling_rights = pos->castling_rights;
    state->en_passant_sq = pos->en_passant_sq;
    state->halfmove_clock = pos->halfmove_clock;
    state->fullmove_number = pos->fullmove_number;
    state->zobrist_hash = pos->zobrist_hash;
    state->pawn_hash = pos->pawn_hash;
    state->move = 0;
    state->captured_piece = NO_PIECE;

    side_to_move = (Color) pos->side_to_move;

    if (pos->en_passant_sq != NO_SQUARE) {
        pos->zobrist_hash ^= zobrist_en_passant_keys[bitboard_file_of(pos->en_passant_sq)];
    }

    pos->en_passant_sq = NO_SQUARE;
    pos->halfmove_clock = (uint16_t) (pos->halfmove_clock + 1);
    pos->fullmove_number = (uint16_t) (pos->fullmove_number + (side_to_move == BLACK ? 1 : 0));
    pos->side_to_move = side_to_move == WHITE ? BLACK : WHITE;
    pos->zobrist_hash ^= zobrist_side_key;
    pos->history_hashes[pos->history_count++] = pos->zobrist_hash;

    return true;
}

bool movegen_unmake_move(Position *pos) {
    PositionState state;
    Color side_to_move;
    int source;
    int target;
    int captured_square;
    uint8_t flags;
    Piece piece_on_target;
    Piece restored_piece;
    Piece captured_piece;

    if (pos == NULL || pos->state_count == 0) {
        return false;
    }

    state = pos->state_stack[--pos->state_count];
    source = move_source((Move) state.move);
    target = move_target((Move) state.move);
    flags = move_flags((Move) state.move);
    captured_piece = (Piece) state.captured_piece;
    side_to_move = pos->side_to_move == WHITE ? BLACK : WHITE;

    if (!square_is_valid(source) || !square_is_valid(target)) {
        return false;
    }

    piece_on_target = position_get_piece(pos, target);
    if (!piece_is_valid(piece_on_target)) {
        return false;
    }

    if (!position_clear_piece(pos, target)) {
        return false;
    }

    if ((flags & MOVE_FLAG_CASTLING) != 0) {
        int rook_from = NO_SQUARE;
        int rook_to = NO_SQUARE;

        if (side_to_move == WHITE && target == G1) {
            rook_from = H1;
            rook_to = F1;
        } else if (side_to_move == WHITE && target == C1) {
            rook_from = A1;
            rook_to = D1;
        } else if (side_to_move == BLACK && target == G8) {
            rook_from = H8;
            rook_to = F8;
        } else if (side_to_move == BLACK && target == C8) {
            rook_from = A8;
            rook_to = D8;
        } else {
            return false;
        }

        if (!position_clear_piece(pos, rook_to) ||
            !position_set_piece(pos, rook_from, make_piece(side_to_move, ROOK))) {
            return false;
        }
    }

    restored_piece = piece_on_target;
    if (move_promotion_piece((Move) state.move) != MOVE_PROMOTION_NONE) {
        restored_piece = make_piece(side_to_move, PAWN);
    }

    if (!position_set_piece(pos, source, restored_piece)) {
        return false;
    }

    if (captured_piece != NO_PIECE) {
        captured_square = target;
        if ((flags & MOVE_FLAG_EN_PASSANT) != 0) {
            captured_square = target + (side_to_move == WHITE ? -8 : 8);
        }

        if (!position_set_piece(pos, captured_square, captured_piece)) {
            return false;
        }
    }

    pos->side_to_move = side_to_move;
    pos->castling_rights = state.castling_rights;
    pos->en_passant_sq = state.en_passant_sq;
    pos->halfmove_clock = state.halfmove_clock;
    pos->fullmove_number = state.fullmove_number;
    pos->zobrist_hash = state.zobrist_hash;
    pos->pawn_hash = state.pawn_hash;
    if (pos->history_count > 1) {
        --pos->history_count;
    }
    pos->history_hashes[pos->history_count - 1] = pos->zobrist_hash;

    return true;
}

bool movegen_unmake_null_move(Position *pos) {
    PositionState state;

    if (pos == NULL || pos->state_count == 0) {
        return false;
    }

    state = pos->state_stack[--pos->state_count];
    pos->side_to_move = pos->side_to_move == WHITE ? BLACK : WHITE;
    pos->castling_rights = state.castling_rights;
    pos->en_passant_sq = state.en_passant_sq;
    pos->halfmove_clock = state.halfmove_clock;
    pos->fullmove_number = state.fullmove_number;
    pos->zobrist_hash = state.zobrist_hash;
    pos->pawn_hash = state.pawn_hash;
    if (pos->history_count > 1) {
        --pos->history_count;
    }
    pos->history_hashes[pos->history_count - 1] = pos->zobrist_hash;

    return true;
}

void movegen_generate_legal(Position *pos, MoveList *list) {
    MoveList pseudo_legal;
    Color side_to_move;
    size_t index;

    if (list == NULL) {
        return;
    }

    list->count = 0;

    if (pos == NULL) {
        return;
    }

    side_to_move = (Color) pos->side_to_move;
    movegen_generate_pseudo_legal(pos, &pseudo_legal);

    for (index = 0; index < pseudo_legal.count; ++index) {
        Move move = pseudo_legal.moves[index];

        if (!movegen_make_move(pos, move)) {
            continue;
        }

        if (!movegen_is_in_check(pos, side_to_move)) {
            move_list_push(list, move);
        }

        movegen_unmake_move(pos);
    }
}

bool movegen_has_legal_moves(Position *pos) {
    MoveList list;

    if (pos == NULL) {
        return false;
    }

    movegen_generate_legal(pos, &list);
    return list.count > 0;
}

bool movegen_is_checkmate(Position *pos) {
    if (pos == NULL) {
        return false;
    }

    return movegen_is_in_check(pos, (Color) pos->side_to_move) && !movegen_has_legal_moves(pos);
}

bool movegen_is_stalemate(Position *pos) {
    if (pos == NULL) {
        return false;
    }

    return !movegen_is_in_check(pos, (Color) pos->side_to_move) && !movegen_has_legal_moves(pos);
}
