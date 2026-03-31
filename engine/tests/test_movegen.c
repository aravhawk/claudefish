#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/movegen.h"
#include "../src/position.h"
#include "test_suites.h"

static int tests_run = 0;
static int tests_failed = 0;

static void failf(const char *test_name, const char *fmt, ...) {
    va_list args;

    ++tests_failed;
    fprintf(stderr, "FAIL: %s: ", test_name);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}

static void expect_true(const char *test_name, bool condition, const char *message) {
    if (!condition) {
        failf(test_name, "%s", message);
    }
}

static void expect_int_eq(const char *test_name, long long actual, long long expected, const char *label) {
    if (actual != expected) {
        failf(test_name, "%s mismatch: expected %lld, got %lld", label, expected, actual);
    }
}

static void expect_u64_eq(const char *test_name, uint64_t actual, uint64_t expected, const char *label) {
    if (actual != expected) {
        failf(
            test_name,
            "%s mismatch: expected 0x%016llx, got 0x%016llx",
            label,
            (unsigned long long) expected,
            (unsigned long long) actual
        );
    }
}

static Bitboard naive_rook_attacks(int square, Bitboard occupancy) {
    Bitboard attacks = 0;
    int rank = bitboard_rank_of(square);
    int file = bitboard_file_of(square);
    int current;

    for (current = rank + 1; current < 8; ++current) {
        int target = bitboard_make_square(file, current);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    for (current = rank - 1; current >= 0; --current) {
        int target = bitboard_make_square(file, current);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    for (current = file + 1; current < 8; ++current) {
        int target = bitboard_make_square(current, rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    for (current = file - 1; current >= 0; --current) {
        int target = bitboard_make_square(current, rank);

        attacks |= BITBOARD_FROM_SQUARE(target);
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }
    }

    return attacks;
}

static Bitboard naive_bishop_attacks(int square, Bitboard occupancy) {
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
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
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
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
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
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
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
        if ((occupancy & BITBOARD_FROM_SQUARE(target)) != 0) {
            break;
        }

        --current_rank;
        --current_file;
    }

    return attacks;
}

static int count_moves_with_flag(const MoveList *list, uint8_t flag) {
    size_t index;
    int count = 0;

    for (index = 0; index < list->count; ++index) {
        if ((move_flags(list->moves[index]) & flag) != 0) {
            ++count;
        }
    }

    return count;
}

static bool move_list_contains(
    const MoveList *list,
    int source,
    int target,
    uint8_t flags,
    Piece captured_piece,
    PieceType promotion_piece
) {
    size_t index;

    for (index = 0; index < list->count; ++index) {
        Move move = list->moves[index];

        if (move_source(move) == source &&
            move_target(move) == target &&
            move_flags(move) == flags &&
            move_captured_piece(move) == captured_piece &&
            move_promotion_piece(move) == promotion_piece) {
            return true;
        }
    }

    return false;
}

static bool generate_moves_for_fen(const char *test_name, const char *fen, MoveList *list) {
    Position pos;

    if (!position_from_fen(&pos, fen)) {
        failf(test_name, "position_from_fen failed for \"%s\"", fen);
        return false;
    }

    movegen_generate_pseudo_legal(&pos, list);
    return true;
}

static void test_move_encoding_roundtrip(void) {
    const char *test_name = "move_encoding_roundtrip";
    Move move;

    ++tests_run;

    move = move_encode(
        E2,
        E4,
        (uint8_t) (MOVE_FLAG_CAPTURE | MOVE_FLAG_DOUBLE_PAWN_PUSH | MOVE_FLAG_CASTLING),
        B_ROOK,
        QUEEN
    );

    expect_int_eq(test_name, move_source(move), E2, "source");
    expect_int_eq(test_name, move_target(move), E4, "target");
    expect_int_eq(test_name, move_flags(move), MOVE_FLAG_CAPTURE | MOVE_FLAG_DOUBLE_PAWN_PUSH | MOVE_FLAG_CASTLING, "flags");
    expect_int_eq(test_name, move_captured_piece(move), B_ROOK, "captured_piece");
    expect_int_eq(test_name, move_promotion_piece(move), QUEEN, "promotion_piece");
}

static void test_precomputed_attack_tables(void) {
    const char *test_name = "precomputed_attack_tables";

    ++tests_run;

    movegen_init();

    expect_u64_eq(
        test_name,
        movegen_knight_attacks[A1],
        BITBOARD_FROM_SQUARE(B3) | BITBOARD_FROM_SQUARE(C2),
        "knight attacks from a1"
    );
    expect_u64_eq(
        test_name,
        movegen_knight_attacks[H8],
        BITBOARD_FROM_SQUARE(F7) | BITBOARD_FROM_SQUARE(G6),
        "knight attacks from h8"
    );
    expect_u64_eq(
        test_name,
        movegen_king_attacks[A1],
        BITBOARD_FROM_SQUARE(A2) | BITBOARD_FROM_SQUARE(B1) | BITBOARD_FROM_SQUARE(B2),
        "king attacks from a1"
    );
    expect_u64_eq(
        test_name,
        movegen_king_attacks[H8],
        BITBOARD_FROM_SQUARE(G7) | BITBOARD_FROM_SQUARE(G8) | BITBOARD_FROM_SQUARE(H7),
        "king attacks from h8"
    );
    expect_u64_eq(
        test_name,
        movegen_pawn_attacks[WHITE][A2],
        BITBOARD_FROM_SQUARE(B3),
        "white pawn attacks from a2"
    );
    expect_u64_eq(
        test_name,
        movegen_pawn_attacks[WHITE][E4],
        BITBOARD_FROM_SQUARE(D5) | BITBOARD_FROM_SQUARE(F5),
        "white pawn attacks from e4"
    );
    expect_u64_eq(
        test_name,
        movegen_pawn_attacks[BLACK][H7],
        BITBOARD_FROM_SQUARE(G6),
        "black pawn attacks from h7"
    );
}

static void test_magic_attacks_match_naive(void) {
    const char *test_name = "magic_attacks_match_naive";
    static const Bitboard occupancies[] = {
        0x0000000000000000ULL,
        0x0000001818000000ULL,
        0x00FF00000000FF00ULL,
        0x8142241818244281ULL,
        0x55AA55AA55AA55AAULL,
        0x0F0F0F0F0F0F0F0FULL,
        0xF0F0F0F0F0F0F0F0ULL,
        0x123456789ABCDEF0ULL,
        0x8000000000000001ULL,
        0x0102040810204080ULL,
        0x8040201008040201ULL,
        0x0000FFFF00000000ULL
    };
    size_t occupancy_index;

    ++tests_run;

    movegen_init();

    for (occupancy_index = 0; occupancy_index < sizeof(occupancies) / sizeof(occupancies[0]); ++occupancy_index) {
        Bitboard occupancy = occupancies[occupancy_index];
        int square;

        for (square = A1; square <= H8; ++square) {
            Bitboard rook_attacks = movegen_rook_attacks(square, occupancy);
            Bitboard bishop_attacks = movegen_bishop_attacks(square, occupancy);

            expect_u64_eq(test_name, rook_attacks, naive_rook_attacks(square, occupancy), "rook attacks");
            expect_u64_eq(test_name, bishop_attacks, naive_bishop_attacks(square, occupancy), "bishop attacks");
            expect_u64_eq(
                test_name,
                movegen_queen_attacks(square, occupancy),
                rook_attacks | bishop_attacks,
                "queen attacks"
            );
        }
    }
}

static void test_starting_position_move_generation(void) {
    const char *test_name = "starting_position_move_generation";
    MoveList list;

    ++tests_run;

    if (!generate_moves_for_fen(
            test_name,
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            &list
        )) {
        return;
    }

    expect_int_eq(test_name, list.count, 20, "move count");
    expect_int_eq(test_name, count_moves_with_flag(&list, MOVE_FLAG_CAPTURE), 0, "capture count");
    expect_int_eq(test_name, count_moves_with_flag(&list, MOVE_FLAG_DOUBLE_PAWN_PUSH), 8, "double pawn push count");
    expect_true(
        test_name,
        move_list_contains(&list, E2, E3, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE),
        "missing quiet pawn push e2e3"
    );
    expect_true(
        test_name,
        move_list_contains(&list, E2, E4, MOVE_FLAG_DOUBLE_PAWN_PUSH, NO_PIECE, MOVE_PROMOTION_NONE),
        "missing double pawn push e2e4"
    );
    expect_true(
        test_name,
        move_list_contains(&list, B1, A3, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE),
        "missing knight move b1a3"
    );
    expect_true(
        test_name,
        move_list_contains(&list, G1, F3, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE),
        "missing knight move g1f3"
    );
}

static void test_curated_pseudo_legal_move_counts(void) {
    const char *test_name = "curated_pseudo_legal_move_counts";
    struct {
        const char *fen;
        size_t expected_count;
    } cases[] = {
        { "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", 7 },
        { "4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1", 32 },
        { "4k3/8/8/8/8/8/N7/4K3 w - - 0 1", 8 },
        { "2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1", 13 }
    };
    size_t case_index;

    ++tests_run;

    for (case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]); ++case_index) {
        MoveList list;

        if (!generate_moves_for_fen(test_name, cases[case_index].fen, &list)) {
            return;
        }

        expect_int_eq(test_name, list.count, (long long) cases[case_index].expected_count, "curated move count");
    }
}

static void test_promotion_move_generation(void) {
    const char *test_name = "promotion_move_generation";
    MoveList list;

    ++tests_run;

    if (!generate_moves_for_fen(test_name, "2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1", &list)) {
        return;
    }

    expect_true(
        test_name,
        move_list_contains(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, QUEEN),
        "missing queen promotion"
    );
    expect_true(
        test_name,
        move_list_contains(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, ROOK),
        "missing rook promotion"
    );
    expect_true(
        test_name,
        move_list_contains(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, BISHOP),
        "missing bishop promotion"
    );
    expect_true(
        test_name,
        move_list_contains(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, KNIGHT),
        "missing knight promotion"
    );
    expect_true(
        test_name,
        move_list_contains(&list, B7, C8, MOVE_FLAG_CAPTURE, B_ROOK, QUEEN),
        "missing queen capture promotion"
    );
    expect_true(
        test_name,
        move_list_contains(&list, B7, C8, MOVE_FLAG_CAPTURE, B_ROOK, KNIGHT),
        "missing knight capture promotion"
    );
}

int test_movegen_run(void) {
    tests_run = 0;
    tests_failed = 0;

    test_move_encoding_roundtrip();
    test_precomputed_attack_tables();
    test_magic_attacks_match_naive();
    test_starting_position_move_generation();
    test_curated_pseudo_legal_move_counts();
    test_promotion_move_generation();

    if (tests_failed == 0) {
        printf("PASS: %d move generation tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d move generation tests failed\n", tests_failed, tests_run);
    return 1;
}
