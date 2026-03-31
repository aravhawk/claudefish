#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/book.h"
#include "../src/draw.h"
#include "../src/engine.h"
#include "../src/search.h"
#include "../src/time.h"
#include "test_suites.h"

static int tests_run = 0;
static int tests_failed = 0;

static void failf(const char *test_name, const char *message) {
    ++tests_failed;
    fprintf(stderr, "FAIL: %s: %s\n", test_name, message);
}

static void expect_true(const char *test_name, bool condition, const char *message) {
    if (!condition) {
        failf(test_name, message);
    }
}

static void expect_int_eq(const char *test_name, long long actual, long long expected, const char *label) {
    char message[128];

    if (actual == expected) {
        return;
    }

    snprintf(message, sizeof(message), "%s mismatch: expected %lld, got %lld", label, expected, actual);
    failf(test_name, message);
}

static bool parse_position(const char *test_name, const char *fen, Position *pos) {
    if (!position_from_fen(pos, fen)) {
        char message[256];

        snprintf(message, sizeof(message), "position_from_fen failed for \"%s\"", fen);
        failf(test_name, message);
        return false;
    }

    return true;
}

static bool move_list_contains(const MoveList *list, Move move) {
    size_t index;

    if (list == NULL) {
        return false;
    }

    for (index = 0; index < list->count; ++index) {
        if (list->moves[index] == move) {
            return true;
        }
    }

    return false;
}

static Move find_move_by_uci(Position *pos, const char *uci) {
    MoveList legal_moves;
    size_t index;

    if (pos == NULL || uci == NULL) {
        return 0;
    }

    movegen_generate_legal(pos, &legal_moves);
    for (index = 0; index < legal_moves.count; ++index) {
        char move_uci[6];

        if (search_move_to_uci(legal_moves.moves[index], move_uci) && strcmp(move_uci, uci) == 0) {
            return legal_moves.moves[index];
        }
    }

    return 0;
}

static bool is_valid_uci_move(const char *move) {
    size_t length;

    if (move == NULL) {
        return false;
    }

    length = strlen(move);
    if (length != 4 && length != 5) {
        return false;
    }

    if (move[0] < 'a' || move[0] > 'h' ||
        move[1] < '1' || move[1] > '8' ||
        move[2] < 'a' || move[2] > 'h' ||
        move[3] < '1' || move[3] > '8') {
        return false;
    }

    if (length == 5 && strchr("qrbn", move[4]) == NULL) {
        return false;
    }

    return true;
}

static int count_csv_moves(const char *moves) {
    int count = 0;
    bool in_token = false;

    if (moves == NULL || *moves == '\0') {
        return 0;
    }

    while (*moves != '\0') {
        if (*moves == ',') {
            in_token = false;
        } else if (!in_token) {
            in_token = true;
            ++count;
        }
        ++moves;
    }

    return count;
}

static bool csv_contains_move(const char *csv, const char *needle) {
    size_t needle_length;

    if (csv == NULL || needle == NULL) {
        return false;
    }

    needle_length = strlen(needle);
    while (*csv != '\0') {
        const char *start = csv;
        size_t length = 0;

        while (*csv != '\0' && *csv != ',') {
            ++csv;
            ++length;
        }

        if (length == needle_length && strncmp(start, needle, needle_length) == 0) {
            return true;
        }

        if (*csv == ',') {
            ++csv;
        }
    }

    return false;
}

static void test_opening_book_queries(void) {
    const char *test_name = "opening_book_queries";
    struct BookCase {
        const char *fen;
        bool expected_hit;
    } cases[] = {
        { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", true },
        { "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", true },
        { "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1", true },
        { "8/8/8/8/8/8/6k1/6K1 w - - 0 1", false }
    };
    size_t index;

    ++tests_run;

    expect_int_eq(test_name, init_engine(), 0, "init_engine");
    book_set_seed(1U);

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        Position pos;
        Move move = 0;
        MoveList legal_moves;
        bool hit;

        if (!parse_position(test_name, cases[index].fen, &pos)) {
            continue;
        }

        hit = book_probe_move(&pos, &move);
        expect_true(test_name, hit == cases[index].expected_hit, "book hit expectation mismatch");

        if (hit) {
            movegen_generate_legal(&pos, &legal_moves);
            expect_true(test_name, move != 0, "book returned an empty move");
            expect_true(test_name, move_list_contains(&legal_moves, move), "book move was not legal");
        }
    }
}

static void test_draw_detection_and_search_integration(void) {
    const char *test_name = "draw_detection_and_search_integration";
    const char *repetition_moves[] = { "g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8" };
    Position repetition;
    Position fifty_move;
    Position kvk;
    Position kbvk;
    Position knvk;
    Position kbnvk;
    Position pawn_reset;
    Position capture_reset;
    SearchResult result;
    size_t index;

    ++tests_run;

    if (!parse_position(test_name, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &repetition)) {
        return;
    }

    for (index = 0; index < sizeof(repetition_moves) / sizeof(repetition_moves[0]); ++index) {
        Move move = find_move_by_uci(&repetition, repetition_moves[index]);

        expect_true(test_name, move != 0, "expected repetition move to be legal");
        if (move == 0 || !movegen_make_move(&repetition, move)) {
            failf(test_name, "failed to apply repetition move");
            return;
        }
    }

    expect_true(test_name, draw_is_threefold_repetition(&repetition), "threefold repetition should be detected");
    expect_true(test_name, draw_is_draw(&repetition), "repetition position should be drawn");
    expect_true(
        test_name,
        search_iterative_deepening(&repetition, 6, 0, &result) && result.score == 0,
        "search should score a repeated position as drawn"
    );

    if (!parse_position(test_name, "6k1/8/8/8/8/8/8/R5K1 w - - 100 1", &fifty_move)) {
        return;
    }

    expect_true(test_name, draw_is_fifty_move_rule(&fifty_move), "50-move rule should trigger at 100 halfmoves");
    expect_true(test_name, draw_is_draw(&fifty_move), "50-move position should be drawn");
    expect_true(
        test_name,
        search_iterative_deepening(&fifty_move, 4, 0, &result) && result.score == 0,
        "search should score a 50-move position as drawn"
    );

    if (!parse_position(test_name, "8/8/8/8/8/8/6k1/6K1 w - - 0 1", &kvk) ||
        !parse_position(test_name, "8/8/8/8/8/8/6k1/5BK1 w - - 0 1", &kbvk) ||
        !parse_position(test_name, "8/8/8/8/8/8/6k1/5NK1 w - - 0 1", &knvk) ||
        !parse_position(test_name, "8/8/8/8/8/8/6k1/4BNK1 w - - 0 1", &kbnvk)) {
        return;
    }

    expect_true(test_name, draw_has_insufficient_material(&kvk), "KvK should be insufficient material");
    expect_true(test_name, draw_has_insufficient_material(&kbvk), "KBvK should be insufficient material");
    expect_true(test_name, draw_has_insufficient_material(&knvk), "KNvK should be insufficient material");
    expect_true(test_name, !draw_has_insufficient_material(&kbnvk), "KBNvK should not be insufficient material");

    if (!parse_position(test_name, "4k3/8/8/8/8/8/4P3/4K3 w - - 99 1", &pawn_reset)) {
        return;
    }

    expect_true(test_name, movegen_make_move(&pawn_reset, find_move_by_uci(&pawn_reset, "e2e4")), "pawn move should apply");
    expect_int_eq(test_name, pawn_reset.halfmove_clock, 0, "halfmove clock after pawn move");
    expect_true(test_name, !draw_is_fifty_move_rule(&pawn_reset), "pawn move should reset 50-move counter");

    if (!parse_position(test_name, "4k3/8/8/8/8/3p4/4P3/4K3 w - - 99 1", &capture_reset)) {
        return;
    }

    expect_true(
        test_name,
        movegen_make_move(&capture_reset, find_move_by_uci(&capture_reset, "e2d3")),
        "capture should apply"
    );
    expect_int_eq(test_name, capture_reset.halfmove_clock, 0, "halfmove clock after capture");
    expect_true(test_name, !draw_is_fifty_move_rule(&capture_reset), "capture should reset 50-move counter");
}

static void test_time_management_budgeting(void) {
    const char *test_name = "time_management_budgeting";
    TimeControl fixed = { 60000, 1000, 250, 30 };
    TimeControl sudden_death = { 30000, 0, 0, 0 };
    TimeControl with_increment = { 15000, 500, 0, 0 };

    ++tests_run;

    expect_int_eq(test_name, time_calculate_allocation_ms(&fixed), 250, "fixed move time allocation");
    expect_int_eq(test_name, time_calculate_allocation_ms(&sudden_death), 1000, "sudden death allocation");
    expect_int_eq(test_name, time_calculate_allocation_ms(&with_increment), 875, "increment allocation");
}

static void test_search_respects_time_limits(void) {
    const char *positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "rnbqkb1r/pp1ppppp/5n2/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "rnbqkbnr/pp2pppp/8/2pp4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq c6 0 3",
        "rnbqkbnr/pp2pppp/2p5/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 3",
        "r1bq1rk1/pp1nbppp/2npp3/2p5/2BPP3/2N2N2/PPQ2PPP/R1B2RK1 w - - 0 8",
        "2r2rk1/pp1n1ppp/2pbpn2/q7/3P4/2NBPN2/PPQ2PPP/2R2RK1 w - - 0 12",
        "r2q1rk1/ppp2ppp/2n1pn2/2bp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - - 0 8"
    };
    size_t index;

    ++tests_run;

    for (index = 0; index < sizeof(positions) / sizeof(positions[0]); ++index) {
        Position pos;
        SearchResult result;
        MoveList legal_moves;
        char test_name[64];

        snprintf(test_name, sizeof(test_name), "time_limited_search_%zu", index + 1);
        if (!parse_position(test_name, positions[index], &pos)) {
            continue;
        }

        expect_true(
            test_name,
            search_iterative_deepening(&pos, SEARCH_MAX_DEPTH, 80, &result),
            "search should complete at least one iteration"
        );
        movegen_generate_legal(&pos, &legal_moves);
        expect_true(test_name, move_list_contains(&legal_moves, result.best_move), "best move should be legal");
        expect_true(test_name, result.elapsed_ms >= 16.0, "search should use at least 20% of the allocation");
        expect_true(test_name, result.elapsed_ms <= 250.0, "search should stop close to the time limit");
    }
}

static void test_engine_api_end_to_end(void) {
    const char *test_name = "engine_api_end_to_end";
    const char *legal_moves;
    const char *best_move;
    const char *positions[] = {
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "rnbqkb1r/pp1ppppp/5n2/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "r1bq1rk1/pp1nbppp/2npp3/2p5/2BPP3/2N2N2/PPQ2PPP/R1B2RK1 w - - 0 8"
    };
    size_t index;

    ++tests_run;

    expect_int_eq(test_name, init_engine(), 0, "init_engine");
    expect_int_eq(
        test_name,
        set_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
        0,
        "set_position"
    );

    legal_moves = get_legal_moves();
    expect_int_eq(test_name, count_csv_moves(legal_moves), 20, "starting move count");
    expect_true(test_name, evaluate_position() >= -50 && evaluate_position() <= 50, "starting evaluation should be near zero");

    best_move = search_best_move(4, 50);
    expect_true(test_name, is_valid_uci_move(best_move), "search_best_move should return UCI");
    expect_true(test_name, csv_contains_move(legal_moves, best_move), "best move should be legal");

    expect_int_eq(test_name, set_position("not a valid fen"), -1, "invalid FEN return code");
    expect_int_eq(
        test_name,
        set_position("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"),
        0,
        "set_position after invalid FEN"
    );
    expect_true(test_name, csv_contains_move(get_legal_moves(), "e1g1"), "castling UCI should use king destination");
    expect_true(test_name, csv_contains_move(get_legal_moves(), "e1c1"), "queenside castling UCI should use king destination");

    expect_int_eq(
        test_name,
        set_position("k7/4P3/8/8/8/8/8/4K3 w - - 0 1"),
        0,
        "promotion position"
    );
    expect_true(test_name, csv_contains_move(get_legal_moves(), "e7e8q"), "queen promotion should use UCI suffix");
    expect_true(test_name, csv_contains_move(get_legal_moves(), "e7e8r"), "rook promotion should use UCI suffix");
    expect_true(test_name, csv_contains_move(get_legal_moves(), "e7e8b"), "bishop promotion should use UCI suffix");
    expect_true(test_name, csv_contains_move(get_legal_moves(), "e7e8n"), "knight promotion should use UCI suffix");

    for (index = 0; index < sizeof(positions) / sizeof(positions[0]); ++index) {
        expect_int_eq(test_name, set_position(positions[index]), 0, "sequential set_position");
        best_move = search_best_move(5, 40);
        expect_true(test_name, is_valid_uci_move(best_move), "sequential search should return UCI");
        expect_true(test_name, csv_contains_move(get_legal_moves(), best_move), "sequential best move should be legal");
    }
}

static void test_set_position_preserves_repetition_history(void) {
    const char *test_name = "set_position_preserves_repetition_history";
    const char *moves[] = { "g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8" };
    Position pos;
    size_t index;

    ++tests_run;

    expect_int_eq(test_name, init_engine(), 0, "init_engine");
    expect_true(
        test_name,
        parse_position(test_name, "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &pos),
        "parse repetition seed position"
    );
    expect_int_eq(
        test_name,
        set_position("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
        0,
        "set initial repetition position"
    );

    for (index = 0; index < sizeof(moves) / sizeof(moves[0]); ++index) {
        Move move = find_move_by_uci(&pos, moves[index]);
        char fen[128];

        expect_true(test_name, move != 0, "expected repetition move to be legal");
        if (move == 0 || !movegen_make_move(&pos, move)) {
            failf(test_name, "failed to apply repetition move");
            return;
        }

        expect_true(test_name, position_to_fen(&pos, fen, sizeof(fen)), "position_to_fen for repetition sequence");
        expect_int_eq(test_name, set_position(fen), 0, "set_position for repetition sequence");
    }

    expect_int_eq(test_name, evaluate_position(), 0, "repetition should evaluate as a draw through set_position");
}

int test_engine_api_run(void) {
    tests_run = 0;
    tests_failed = 0;

    test_opening_book_queries();
    test_draw_detection_and_search_integration();
    test_time_management_budgeting();
    test_search_respects_time_limits();
    test_engine_api_end_to_end();
    test_set_position_preserves_repetition_history();

    if (tests_failed == 0) {
        printf("PASS: %d engine API tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d engine API tests failed\n", tests_failed, tests_run);
    return 1;
}
