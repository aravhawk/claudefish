#include "movegen.h"

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
}
