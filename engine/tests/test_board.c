#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/bitboard.h"
#include "../src/position.h"
#include "../src/zobrist.h"
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

static void expect_str_eq(const char *test_name, const char *actual, const char *expected, const char *label) {
    if (strcmp(actual, expected) != 0) {
        failf(test_name, "%s mismatch: expected \"%s\", got \"%s\"", label, expected, actual);
    }
}

static void test_lerf_square_mapping(void) {
    const char *test_name = "lerf_square_mapping";
    char square_name[3] = {0};

    ++tests_run;

    expect_int_eq(test_name, A1, 0, "A1");
    expect_int_eq(test_name, H1, 7, "H1");
    expect_int_eq(test_name, A8, 56, "A8");
    expect_int_eq(test_name, H8, 63, "H8");
    expect_int_eq(test_name, bitboard_square_from_string("a1"), A1, "square_from_string(a1)");
    expect_int_eq(test_name, bitboard_square_from_string("h8"), H8, "square_from_string(h8)");

    expect_true(test_name, bitboard_square_to_string(C6, square_name), "square_to_string(C6) returned false");
    expect_str_eq(test_name, square_name, "c6", "square_to_string(C6)");
}

static void test_starting_position_layout(void) {
    const char *test_name = "starting_position_layout";
    const char *fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Position pos;
    char roundtrip[128];

    ++tests_run;

    expect_true(test_name, position_from_fen(&pos, fen), "position_from_fen(startpos) returned false");
    expect_true(test_name, position_to_fen(&pos, roundtrip, sizeof(roundtrip)), "position_to_fen(startpos) returned false");
    expect_str_eq(test_name, roundtrip, fen, "start position roundtrip");

    expect_int_eq(test_name, pos.side_to_move, WHITE, "side_to_move");
    expect_int_eq(
        test_name,
        pos.castling_rights,
        CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE | CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE,
        "castling_rights"
    );
    expect_int_eq(test_name, pos.en_passant_sq, NO_SQUARE, "en_passant_sq");
    expect_int_eq(test_name, pos.halfmove_clock, 0, "halfmove_clock");
    expect_int_eq(test_name, pos.fullmove_number, 1, "fullmove_number");

    expect_u64_eq(test_name, pos.piece_bitboards[piece_bitboard_index(W_PAWN)], 0x000000000000FF00ULL, "white pawns");
    expect_u64_eq(test_name, pos.piece_bitboards[piece_bitboard_index(W_ROOK)], 0x0000000000000081ULL, "white rooks");
    expect_u64_eq(test_name, pos.piece_bitboards[piece_bitboard_index(B_KING)], 0x1000000000000000ULL, "black king");
    expect_u64_eq(test_name, pos.occupancy[WHITE], 0x000000000000FFFFULL, "white occupancy");
    expect_u64_eq(test_name, pos.occupancy[BLACK], 0xFFFF000000000000ULL, "black occupancy");
    expect_u64_eq(test_name, pos.occupancy[BOTH], 0xFFFF00000000FFFFULL, "both occupancy");

    expect_int_eq(test_name, position_get_piece(&pos, E1), W_KING, "piece at e1");
    expect_int_eq(test_name, position_get_piece(&pos, D8), B_QUEEN, "piece at d8");
    expect_int_eq(test_name, position_get_piece(&pos, A2), W_PAWN, "piece at a2");
    expect_int_eq(test_name, position_get_piece(&pos, H7), B_PAWN, "piece at h7");
    expect_int_eq(test_name, position_get_piece(&pos, E4), NO_PIECE, "piece at e4");

    expect_int_eq(test_name, bitboard_popcount(pos.occupancy[BOTH]), 32, "piece count");
    expect_u64_eq(test_name, pos.zobrist_hash, zobrist_compute_hash(&pos), "zobrist_hash");
    expect_u64_eq(test_name, pos.pawn_hash, zobrist_compute_pawn_hash(&pos), "pawn_hash");
}

static void test_fen_roundtrip_known_positions(void) {
    const char *test_name = "fen_roundtrip_known_positions";
    const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "q3k3/1P6/2n1b3/3r4/4Q3/2B1N3/6p1/4K2R b - - 7 42"
    };
    size_t i;

    ++tests_run;

    for (i = 0; i < sizeof(fens) / sizeof(fens[0]); ++i) {
        Position pos;
        char roundtrip[128];
        char label[64];

        snprintf(label, sizeof(label), "FEN[%zu]", i);
        expect_true(test_name, position_from_fen(&pos, fens[i]), label);
        expect_true(test_name, position_to_fen(&pos, roundtrip, sizeof(roundtrip)), "position_to_fen returned false");
        expect_str_eq(test_name, roundtrip, fens[i], label);
        expect_u64_eq(test_name, pos.zobrist_hash, zobrist_compute_hash(&pos), "recomputed zobrist");
        expect_u64_eq(test_name, pos.pawn_hash, zobrist_compute_pawn_hash(&pos), "recomputed pawn hash");
    }
}

static void append_castling_string(int rights, char *buffer, size_t size) {
    size_t len = 0;

    if (rights == 0) {
        snprintf(buffer, size, "-");
        return;
    }

    if ((rights & CASTLE_WHITE_KINGSIDE) != 0 && len + 1 < size) {
        buffer[len++] = 'K';
    }
    if ((rights & CASTLE_WHITE_QUEENSIDE) != 0 && len + 1 < size) {
        buffer[len++] = 'Q';
    }
    if ((rights & CASTLE_BLACK_KINGSIDE) != 0 && len + 1 < size) {
        buffer[len++] = 'k';
    }
    if ((rights & CASTLE_BLACK_QUEENSIDE) != 0 && len + 1 < size) {
        buffer[len++] = 'q';
    }

    buffer[len] = '\0';
}

static void test_all_castling_right_combinations(void) {
    const char *test_name = "all_castling_right_combinations";
    int rights;

    ++tests_run;

    for (rights = 0; rights < 16; ++rights) {
        Position pos;
        char rights_field[5];
        char fen[96];
        char roundtrip[128];

        append_castling_string(rights, rights_field, sizeof(rights_field));
        snprintf(fen, sizeof(fen), "r3k2r/8/8/8/8/8/8/R3K2R w %s - 0 1", rights_field);

        expect_true(test_name, position_from_fen(&pos, fen), "position_from_fen(castling) returned false");
        expect_true(test_name, position_to_fen(&pos, roundtrip, sizeof(roundtrip)), "position_to_fen(castling) returned false");
        expect_str_eq(test_name, roundtrip, fen, "castling roundtrip");
        expect_int_eq(test_name, pos.castling_rights, rights, "castling rights mask");
    }
}

static void test_non_canonical_castling_rights_are_rejected(void) {
    const char *test_name = "non_canonical_castling_rights_are_rejected";
    const char *fens[] = {
        "4k3/8/8/8/8/8/8/4K3 w QK - 0 1",
        "4k3/8/8/8/8/8/8/4K3 w qk - 0 1",
        "4k3/8/8/8/8/8/8/4K3 w kQ - 0 1",
        "4k3/8/8/8/8/8/8/4K3 w qK - 0 1"
    };
    size_t index;

    ++tests_run;

    for (index = 0; index < sizeof(fens) / sizeof(fens[0]); ++index) {
        Position pos;

        expect_true(test_name, !position_from_fen(&pos, fens[index]), "non-canonical castling rights should be invalid");
    }
}

static void test_en_passant_roundtrip_positions(void) {
    const char *test_name = "en_passant_roundtrip_positions";
    const char *fens[] = {
        "4k3/8/8/8/8/8/8/4K3 b - a3 0 1",
        "4k3/8/8/8/8/8/8/4K3 b - d3 0 1",
        "4k3/8/8/8/8/8/8/4K3 b - h3 0 1",
        "4k3/8/8/8/8/8/8/4K3 w - a6 0 1",
        "4k3/8/8/8/8/8/8/4K3 w - d6 0 1",
        "4k3/8/8/8/8/8/8/4K3 w - h6 0 1"
    };
    size_t i;

    ++tests_run;

    for (i = 0; i < sizeof(fens) / sizeof(fens[0]); ++i) {
        Position pos;
        char roundtrip[128];

        expect_true(test_name, position_from_fen(&pos, fens[i]), "position_from_fen(en passant) returned false");
        expect_true(test_name, position_to_fen(&pos, roundtrip, sizeof(roundtrip)), "position_to_fen(en passant) returned false");
        expect_str_eq(test_name, roundtrip, fens[i], "en passant roundtrip");
        expect_u64_eq(test_name, pos.zobrist_hash, zobrist_compute_hash(&pos), "en passant zobrist");
    }
}

static void test_hash_consistency_after_piece_updates(void) {
    const char *test_name = "hash_consistency_after_piece_updates";
    Position pos;
    uint64_t base_hash;
    uint64_t base_pawn_hash;

    ++tests_run;

    position_clear(&pos);
    expect_true(test_name, position_set_piece(&pos, E1, W_KING), "set white king");
    expect_true(test_name, position_set_piece(&pos, E8, B_KING), "set black king");

    base_hash = pos.zobrist_hash;
    base_pawn_hash = pos.pawn_hash;

    expect_u64_eq(test_name, pos.zobrist_hash, zobrist_compute_hash(&pos), "initial zobrist");
    expect_u64_eq(test_name, pos.pawn_hash, zobrist_compute_pawn_hash(&pos), "initial pawn hash");

    expect_true(test_name, position_set_piece(&pos, D2, W_PAWN), "set white pawn");
    expect_int_eq(test_name, position_get_piece(&pos, D2), W_PAWN, "piece at d2 after pawn set");
    expect_u64_eq(test_name, pos.zobrist_hash, zobrist_compute_hash(&pos), "zobrist after pawn set");
    expect_u64_eq(test_name, pos.pawn_hash, zobrist_compute_pawn_hash(&pos), "pawn hash after pawn set");
    expect_true(test_name, pos.zobrist_hash != base_hash, "zobrist hash did not change after adding pawn");
    expect_true(test_name, pos.pawn_hash != base_pawn_hash, "pawn hash did not change after adding pawn");

    expect_true(test_name, position_set_piece(&pos, D2, W_QUEEN), "replace pawn with queen");
    expect_int_eq(test_name, position_get_piece(&pos, D2), W_QUEEN, "piece at d2 after queen set");
    expect_u64_eq(test_name, pos.zobrist_hash, zobrist_compute_hash(&pos), "zobrist after queen replace");
    expect_u64_eq(test_name, pos.pawn_hash, zobrist_compute_pawn_hash(&pos), "pawn hash after queen replace");
    expect_u64_eq(test_name, pos.pawn_hash, base_pawn_hash, "pawn hash after replacing pawn with queen");

    expect_true(test_name, position_clear_piece(&pos, D2), "clear piece at d2");
    expect_int_eq(test_name, position_get_piece(&pos, D2), NO_PIECE, "piece at d2 after clear");
    expect_u64_eq(test_name, pos.zobrist_hash, zobrist_compute_hash(&pos), "zobrist after clear");
    expect_u64_eq(test_name, pos.pawn_hash, zobrist_compute_pawn_hash(&pos), "pawn hash after clear");
    expect_u64_eq(test_name, pos.zobrist_hash, base_hash, "restored zobrist hash");
    expect_u64_eq(test_name, pos.pawn_hash, base_pawn_hash, "restored pawn hash");
}

static void test_hashes_reflect_non_board_state(void) {
    const char *test_name = "hashes_reflect_non_board_state";
    Position base;
    Position side_changed;
    Position castling_changed;
    Position ep_changed;

    ++tests_run;

    expect_true(
        test_name,
        position_from_fen(&base, "4k3/8/8/8/8/8/8/4K3 w - - 0 1"),
        "parse base position"
    );
    expect_true(
        test_name,
        position_from_fen(&side_changed, "4k3/8/8/8/8/8/8/4K3 b - - 0 1"),
        "parse side-changed position"
    );
    expect_true(
        test_name,
        position_from_fen(&castling_changed, "4k3/8/8/8/8/8/8/4K3 w K - 0 1"),
        "parse castling-changed position"
    );
    expect_true(
        test_name,
        position_from_fen(&ep_changed, "4k3/8/8/8/8/8/8/4K3 b - a3 0 1"),
        "parse ep-changed position"
    );

    expect_true(test_name, base.zobrist_hash != side_changed.zobrist_hash, "side_to_move should affect zobrist hash");
    expect_true(test_name, base.zobrist_hash != castling_changed.zobrist_hash, "castling should affect zobrist hash");
    expect_true(test_name, side_changed.zobrist_hash != ep_changed.zobrist_hash, "en passant should affect zobrist hash");
    expect_u64_eq(test_name, base.pawn_hash, side_changed.pawn_hash, "pawn hash independent of side");
    expect_u64_eq(test_name, base.pawn_hash, castling_changed.pawn_hash, "pawn hash independent of castling");
    expect_u64_eq(test_name, base.pawn_hash, ep_changed.pawn_hash, "pawn hash independent of en passant");
}

int test_board_run(void) {
    tests_run = 0;
    tests_failed = 0;

    zobrist_init();

    test_lerf_square_mapping();
    test_starting_position_layout();
    test_fen_roundtrip_known_positions();
    test_all_castling_right_combinations();
    test_non_canonical_castling_rights_are_rejected();
    test_en_passant_roundtrip_positions();
    test_hash_consistency_after_piece_updates();
    test_hashes_reflect_non_board_state();

    if (tests_failed == 0) {
        printf("PASS: %d board representation tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d board representation tests failed\n", tests_failed, tests_run);
    return 1;
}
