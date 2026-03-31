#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/movegen.h"
#include "../src/position.h"
#include "test_suites.h"

bool movegen_is_square_attacked(const Position *pos, int square, Color attacker);
bool movegen_is_in_check(const Position *pos, Color side);
Bitboard movegen_pinned_pieces(const Position *pos, Color side);
void movegen_generate_legal(Position *pos, MoveList *list);
bool movegen_make_move(Position *pos, Move move);
bool movegen_unmake_move(Position *pos);
bool movegen_is_checkmate(Position *pos);
bool movegen_is_stalemate(Position *pos);

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

static bool position_fields_equal(const Position *actual, const Position *expected) {
    size_t index;

    for (index = 0; index < PIECE_BITBOARDS; ++index) {
        if (actual->piece_bitboards[index] != expected->piece_bitboards[index]) {
            return false;
        }
    }

    for (index = 0; index < 3; ++index) {
        if (actual->occupancy[index] != expected->occupancy[index]) {
            return false;
        }
    }

    if (memcmp(actual->mailbox, expected->mailbox, sizeof(actual->mailbox)) != 0) {
        return false;
    }

    return actual->side_to_move == expected->side_to_move &&
           actual->castling_rights == expected->castling_rights &&
           actual->en_passant_sq == expected->en_passant_sq &&
           actual->halfmove_clock == expected->halfmove_clock &&
           actual->fullmove_number == expected->fullmove_number &&
           actual->zobrist_hash == expected->zobrist_hash &&
           actual->pawn_hash == expected->pawn_hash;
}

static void expect_positions_equal(
    const char *test_name,
    const Position *actual,
    const Position *expected,
    const char *label
) {
    if (!position_fields_equal(actual, expected)) {
        char actual_fen[128];
        char expected_fen[128];

        if (!position_to_fen(actual, actual_fen, sizeof(actual_fen))) {
            snprintf(actual_fen, sizeof(actual_fen), "<invalid>");
        }
        if (!position_to_fen(expected, expected_fen, sizeof(expected_fen))) {
            snprintf(expected_fen, sizeof(expected_fen), "<invalid>");
        }

        failf(
            test_name,
            "%s mismatch: expected \"%s\" / hash 0x%016llx, got \"%s\" / hash 0x%016llx",
            label,
            expected_fen,
            (unsigned long long) expected->zobrist_hash,
            actual_fen,
            (unsigned long long) actual->zobrist_hash
        );
    }
}

static bool parse_position(const char *test_name, const char *fen, Position *pos) {
    if (!position_from_fen(pos, fen)) {
        failf(test_name, "position_from_fen failed for \"%s\"", fen);
        return false;
    }

    return true;
}

static bool generate_legal_moves(const char *test_name, const char *fen, Position *pos, MoveList *list) {
    if (!parse_position(test_name, fen, pos)) {
        return false;
    }

    movegen_generate_legal(pos, list);
    return true;
}

static bool find_move(
    const MoveList *list,
    int source,
    int target,
    uint8_t flags,
    int captured_piece,
    PieceType promotion_piece,
    Move *out_move
) {
    size_t index;

    for (index = 0; index < list->count; ++index) {
        Move move = list->moves[index];

        if (move_source(move) != source || move_target(move) != target || move_flags(move) != flags) {
            continue;
        }

        if (captured_piece >= 0 && move_captured_piece(move) != (Piece) captured_piece) {
            continue;
        }

        if (move_promotion_piece(move) != promotion_piece) {
            continue;
        }

        if (out_move != NULL) {
            *out_move = move;
        }
        return true;
    }

    return false;
}

static int count_moves_from_square(const MoveList *list, int source) {
    size_t index;
    int count = 0;

    for (index = 0; index < list->count; ++index) {
        if (move_source(list->moves[index]) == source) {
            ++count;
        }
    }

    return count;
}

static void test_castling_generation_and_blockers(void) {
    const char *test_name = "castling_generation_and_blockers";
    Position pos;
    MoveList list;

    ++tests_run;

    if (!generate_legal_moves(test_name, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        find_move(&list, E1, G1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "missing white kingside castle"
    );
    expect_true(
        test_name,
        find_move(&list, E1, C1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "missing white queenside castle"
    );

    if (!generate_legal_moves(test_name, "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        find_move(&list, E8, G8, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "missing black kingside castle"
    );
    expect_true(
        test_name,
        find_move(&list, E8, C8, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "missing black queenside castle"
    );

    if (!generate_legal_moves(test_name, "r3k2r/8/8/8/2b5/8/8/R3K2R w KQkq - 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        !find_move(&list, E1, G1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "white kingside castle should be blocked by attacked transit square"
    );
    expect_true(
        test_name,
        find_move(&list, E1, C1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "white queenside castle should remain legal"
    );

    if (!generate_legal_moves(test_name, "r3k2r/8/8/7b/8/8/8/R3K2R w KQkq - 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        !find_move(&list, E1, C1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "white queenside castle should be blocked by attacked transit/destination square"
    );
    expect_true(
        test_name,
        find_move(&list, E1, G1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "white kingside castle should remain legal"
    );

    if (!generate_legal_moves(test_name, "r3k2r/8/8/8/8/8/8/RN2K2R w KQkq - 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        !find_move(&list, E1, C1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "white queenside castle should be blocked by occupied path"
    );

    if (!generate_legal_moves(test_name, "r3k2r/8/8/8/4r3/8/8/R3K2R w KQkq - 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        !find_move(&list, E1, G1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "white castling should be illegal while king is in check"
    );
    expect_true(
        test_name,
        !find_move(&list, E1, C1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "white castling should be illegal while king is in check"
    );
}

static void test_castling_make_move_and_rights_updates(void) {
    const char *test_name = "castling_make_move_and_rights_updates";
    const char *base_fen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    Position pos;
    Position before;
    MoveList list;
    Move move;

    ++tests_run;

    if (!generate_legal_moves(test_name, base_fen, &pos, &list)) {
        return;
    }

    before = pos;
    expect_true(
        test_name,
        find_move(&list, E1, G1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE, &move),
        "missing castling move for make/unmake"
    );
    expect_true(test_name, movegen_make_move(&pos, move), "make white kingside castle");
    expect_int_eq(test_name, position_get_piece(&pos, G1), W_KING, "white king after O-O");
    expect_int_eq(test_name, position_get_piece(&pos, F1), W_ROOK, "white rook after O-O");
    expect_int_eq(test_name, position_get_piece(&pos, E1), NO_PIECE, "empty e1 after O-O");
    expect_int_eq(test_name, position_get_piece(&pos, H1), NO_PIECE, "empty h1 after O-O");
    expect_int_eq(
        test_name,
        pos.castling_rights,
        CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE,
        "castling rights after white castle"
    );
    expect_true(test_name, movegen_unmake_move(&pos), "unmake white kingside castle");
    expect_positions_equal(test_name, &pos, &before, "restore after white kingside castle");

    if (!generate_legal_moves(test_name, base_fen, &pos, &list)) {
        return;
    }

    before = pos;
    expect_true(
        test_name,
        find_move(&list, E1, F1, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, &move),
        "missing white king move e1f1"
    );
    expect_true(test_name, movegen_make_move(&pos, move), "make white king move");
    expect_int_eq(
        test_name,
        pos.castling_rights,
        CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE,
        "castling rights after white king move"
    );
    expect_true(test_name, movegen_unmake_move(&pos), "unmake white king move");
    expect_positions_equal(test_name, &pos, &before, "restore after white king move");

    if (!generate_legal_moves(test_name, base_fen, &pos, &list)) {
        return;
    }

    before = pos;
    expect_true(
        test_name,
        find_move(&list, A1, A2, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, &move),
        "missing white rook move a1a2"
    );
    expect_true(test_name, movegen_make_move(&pos, move), "make white rook move");
    expect_int_eq(
        test_name,
        pos.castling_rights,
        CASTLE_WHITE_KINGSIDE | CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE,
        "castling rights after queenside rook move"
    );
    expect_true(test_name, movegen_unmake_move(&pos), "unmake white rook move");
    expect_positions_equal(test_name, &pos, &before, "restore after white rook move");

    if (!generate_legal_moves(test_name, base_fen, &pos, &list)) {
        return;
    }

    before = pos;
    expect_true(
        test_name,
        find_move(&list, A1, A8, MOVE_FLAG_CAPTURE, B_ROOK, MOVE_PROMOTION_NONE, &move),
        "missing white rook capture a1xa8"
    );
    expect_true(test_name, movegen_make_move(&pos, move), "make rook capture on starting square");
    expect_int_eq(
        test_name,
        pos.castling_rights,
        CASTLE_WHITE_KINGSIDE | CASTLE_BLACK_KINGSIDE,
        "castling rights after rook capture on starting square"
    );
    expect_true(test_name, movegen_unmake_move(&pos), "unmake rook capture on starting square");
    expect_positions_equal(test_name, &pos, &before, "restore after rook capture on starting square");
}

static void test_en_passant_legality_and_expiry(void) {
    const char *test_name = "en_passant_legality_and_expiry";
    Position pos;
    Position before;
    MoveList list;
    Move move;

    ++tests_run;

    if (!generate_legal_moves(test_name, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", &pos, &list)) {
        return;
    }

    before = pos;
    expect_true(
        test_name,
        find_move(&list, E5, D6, MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT, B_PAWN, MOVE_PROMOTION_NONE, &move),
        "missing legal en passant capture"
    );
    expect_true(test_name, movegen_make_move(&pos, move), "make legal en passant");
    expect_int_eq(test_name, position_get_piece(&pos, D6), W_PAWN, "white pawn after en passant");
    expect_int_eq(test_name, position_get_piece(&pos, D5), NO_PIECE, "captured pawn removed by en passant");
    expect_int_eq(test_name, pos.en_passant_sq, NO_SQUARE, "en passant square cleared after en passant");
    expect_int_eq(test_name, pos.halfmove_clock, 0, "halfmove reset after en passant");
    expect_true(test_name, movegen_unmake_move(&pos), "unmake en passant");
    expect_positions_equal(test_name, &pos, &before, "restore after en passant");

    if (!generate_legal_moves(test_name, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", &pos, &list)) {
        return;
    }

    before = pos;
    expect_true(
        test_name,
        find_move(&list, E1, F1, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, &move),
        "missing quiet move for en passant expiry"
    );
    expect_true(test_name, movegen_make_move(&pos, move), "make quiet move");
    expect_int_eq(test_name, pos.en_passant_sq, NO_SQUARE, "en passant square expired after quiet move");
    expect_true(test_name, movegen_unmake_move(&pos), "unmake quiet move");
    expect_positions_equal(test_name, &pos, &before, "restore after quiet move");

    if (!generate_legal_moves(test_name, "4r1k1/8/8/3pP3/8/8/8/4K3 w - d6 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        !find_move(&list, E5, D6, MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT, B_PAWN, MOVE_PROMOTION_NONE, NULL),
        "pinned en passant should be illegal"
    );

    if (!generate_legal_moves(test_name, "4k3/8/8/r1pPK3/8/8/8/8 w - c6 0 1", &pos, &list)) {
        return;
    }

    expect_true(
        test_name,
        !find_move(&list, D5, C6, MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT, B_PAWN, MOVE_PROMOTION_NONE, NULL),
        "discovered-check en passant should be illegal"
    );
}

static void test_promotion_legal_generation(void) {
    const char *test_name = "promotion_legal_generation";
    Position pos;
    MoveList list;

    ++tests_run;

    if (!generate_legal_moves(test_name, "2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1", &pos, &list)) {
        return;
    }

    expect_int_eq(test_name, count_moves_from_square(&list, B7), 8, "white promotion move count");
    expect_true(test_name, find_move(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, QUEEN, NULL), "missing white queen promotion");
    expect_true(test_name, find_move(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, ROOK, NULL), "missing white rook promotion");
    expect_true(test_name, find_move(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, BISHOP, NULL), "missing white bishop promotion");
    expect_true(test_name, find_move(&list, B7, B8, MOVE_FLAG_NONE, NO_PIECE, KNIGHT, NULL), "missing white knight promotion");
    expect_true(test_name, find_move(&list, B7, C8, MOVE_FLAG_CAPTURE, B_ROOK, QUEEN, NULL), "missing white queen capture promotion");
    expect_true(test_name, find_move(&list, B7, C8, MOVE_FLAG_CAPTURE, B_ROOK, ROOK, NULL), "missing white rook capture promotion");
    expect_true(test_name, find_move(&list, B7, C8, MOVE_FLAG_CAPTURE, B_ROOK, BISHOP, NULL), "missing white bishop capture promotion");
    expect_true(test_name, find_move(&list, B7, C8, MOVE_FLAG_CAPTURE, B_ROOK, KNIGHT, NULL), "missing white knight capture promotion");

    if (!generate_legal_moves(test_name, "4k3/8/8/8/8/8/p7/1R2K3 b - - 0 1", &pos, &list)) {
        return;
    }

    expect_int_eq(test_name, count_moves_from_square(&list, A2), 8, "black promotion move count");
    expect_true(test_name, find_move(&list, A2, A1, MOVE_FLAG_NONE, NO_PIECE, QUEEN, NULL), "missing black queen promotion");
    expect_true(test_name, find_move(&list, A2, A1, MOVE_FLAG_NONE, NO_PIECE, ROOK, NULL), "missing black rook promotion");
    expect_true(test_name, find_move(&list, A2, A1, MOVE_FLAG_NONE, NO_PIECE, BISHOP, NULL), "missing black bishop promotion");
    expect_true(test_name, find_move(&list, A2, A1, MOVE_FLAG_NONE, NO_PIECE, KNIGHT, NULL), "missing black knight promotion");
    expect_true(test_name, find_move(&list, A2, B1, MOVE_FLAG_CAPTURE, W_ROOK, QUEEN, NULL), "missing black queen capture promotion");
    expect_true(test_name, find_move(&list, A2, B1, MOVE_FLAG_CAPTURE, W_ROOK, ROOK, NULL), "missing black rook capture promotion");
    expect_true(test_name, find_move(&list, A2, B1, MOVE_FLAG_CAPTURE, W_ROOK, BISHOP, NULL), "missing black bishop capture promotion");
    expect_true(test_name, find_move(&list, A2, B1, MOVE_FLAG_CAPTURE, W_ROOK, KNIGHT, NULL), "missing black knight capture promotion");
}

static void test_legal_move_filtering_and_pins(void) {
    const char *test_name = "legal_move_filtering_and_pins";
    Position pos;
    MoveList list;
    size_t index;

    ++tests_run;

    if (!generate_legal_moves(test_name, "4r1k1/8/8/1B6/8/3B4/8/4K3 w - - 0 1", &pos, &list)) {
        return;
    }

    expect_true(test_name, movegen_is_in_check(&pos, WHITE), "white should be in single check");
    expect_true(
        test_name,
        find_move(&list, E1, F1, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "single-check position should include a king move"
    );
    expect_true(
        test_name,
        find_move(&list, D3, E2, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "single-check position should include a blocking move"
    );
    expect_true(
        test_name,
        find_move(&list, B5, E8, MOVE_FLAG_CAPTURE, B_ROOK, MOVE_PROMOTION_NONE, NULL),
        "single-check position should include a checker capture"
    );
    expect_true(
        test_name,
        !find_move(&list, D3, C4, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "single-check position should exclude unrelated moves"
    );

    if (!generate_legal_moves(test_name, "4r1k1/8/8/8/1b6/8/8/4K3 w - - 0 1", &pos, &list)) {
        return;
    }

    expect_true(test_name, movegen_is_in_check(&pos, WHITE), "white should be in double check");
    for (index = 0; index < list.count; ++index) {
        expect_int_eq(test_name, move_source(list.moves[index]), E1, "double-check move source");
    }

    if (!generate_legal_moves(test_name, "4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1", &pos, &list)) {
        return;
    }

    expect_u64_eq(test_name, movegen_pinned_pieces(&pos, WHITE), BITBOARD_FROM_SQUARE(E2), "pinned pieces");
    expect_true(
        test_name,
        find_move(&list, E2, E3, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "pinned rook should be able to move on the pin ray"
    );
    expect_true(
        test_name,
        !find_move(&list, E2, D2, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE, NULL),
        "pinned rook should not move off the pin ray"
    );
}

static void test_mate_and_stalemate_detection(void) {
    const char *test_name = "mate_and_stalemate_detection";
    Position pos;
    MoveList list;

    ++tests_run;

    if (!generate_legal_moves(test_name, "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1", &pos, &list)) {
        return;
    }

    expect_true(test_name, movegen_is_in_check(&pos, BLACK), "black should be in check");
    expect_int_eq(test_name, list.count, 0, "checkmate legal move count");
    expect_true(test_name, movegen_is_checkmate(&pos), "position should be checkmate");
    expect_true(test_name, !movegen_is_stalemate(&pos), "checkmate should not be stalemate");

    if (!generate_legal_moves(test_name, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", &pos, &list)) {
        return;
    }

    expect_true(test_name, !movegen_is_in_check(&pos, BLACK), "black should not be in check");
    expect_int_eq(test_name, list.count, 0, "stalemate legal move count");
    expect_true(test_name, movegen_is_stalemate(&pos), "position should be stalemate");
    expect_true(test_name, !movegen_is_checkmate(&pos), "stalemate should not be checkmate");
}

static void assert_make_unmake_restores(
    const char *test_name,
    const char *fen,
    int source,
    int target,
    uint8_t flags,
    int captured_piece,
    PieceType promotion_piece
) {
    Position pos;
    Position before;
    MoveList list;
    Move move;

    if (!generate_legal_moves(test_name, fen, &pos, &list)) {
        return;
    }

    before = pos;
    expect_true(
        test_name,
        find_move(&list, source, target, flags, captured_piece, promotion_piece, &move),
        "target move not found"
    );
    expect_true(test_name, movegen_make_move(&pos, move), "make move");
    expect_true(test_name, movegen_unmake_move(&pos), "unmake move");
    expect_positions_equal(test_name, &pos, &before, "make/unmake restore");
}

static bool verify_recursive_integrity(Position *pos, int depth, int *visited_nodes, const char *test_name) {
    MoveList list;
    size_t index;

    if (depth == 0) {
        ++(*visited_nodes);
        return true;
    }

    movegen_generate_legal(pos, &list);

    for (index = 0; index < list.count; ++index) {
        Position before = *pos;

        if (!movegen_make_move(pos, list.moves[index])) {
            failf(test_name, "movegen_make_move failed during recursive integrity walk");
            return false;
        }

        if (!verify_recursive_integrity(pos, depth - 1, visited_nodes, test_name)) {
            return false;
        }

        if (!movegen_unmake_move(pos)) {
            failf(test_name, "movegen_unmake_move failed during recursive integrity walk");
            return false;
        }

        if (!position_fields_equal(pos, &before)) {
            failf(test_name, "recursive make/unmake did not restore the prior position");
            return false;
        }
    }

    return true;
}

static void test_make_unmake_integrity(void) {
    const char *test_name = "make_unmake_integrity";
    Position pos;
    int visited_nodes = 0;

    ++tests_run;

    assert_make_unmake_restores(test_name, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", E2, E3, MOVE_FLAG_NONE, NO_PIECE, MOVE_PROMOTION_NONE);
    assert_make_unmake_restores(test_name, "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", E4, D5, MOVE_FLAG_CAPTURE, B_PAWN, MOVE_PROMOTION_NONE);
    assert_make_unmake_restores(test_name, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", E1, G1, MOVE_FLAG_CASTLING, NO_PIECE, MOVE_PROMOTION_NONE);
    assert_make_unmake_restores(test_name, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", E5, D6, MOVE_FLAG_CAPTURE | MOVE_FLAG_EN_PASSANT, B_PAWN, MOVE_PROMOTION_NONE);
    assert_make_unmake_restores(test_name, "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", B7, B8, MOVE_FLAG_NONE, NO_PIECE, QUEEN);
    assert_make_unmake_restores(test_name, "2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1", B7, C8, MOVE_FLAG_CAPTURE, B_ROOK, KNIGHT);

    if (!parse_position(test_name, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &pos)) {
        return;
    }

    expect_true(
        test_name,
        verify_recursive_integrity(&pos, 3, &visited_nodes, test_name),
        "recursive integrity verification failed"
    );
    expect_true(test_name, visited_nodes > 20, "recursive integrity should visit more than 20 nodes");
}

int test_legality_run(void) {
    tests_run = 0;
    tests_failed = 0;

    test_castling_generation_and_blockers();
    test_castling_make_move_and_rights_updates();
    test_en_passant_legality_and_expiry();
    test_promotion_legal_generation();
    test_legal_move_filtering_and_pins();
    test_mate_and_stalemate_detection();
    test_make_unmake_integrity();

    if (tests_failed == 0) {
        printf("PASS: %d legality tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d legality tests failed\n", tests_failed, tests_run);
    return 1;
}
