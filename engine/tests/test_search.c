#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/search.h"
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

static bool parse_position(const char *test_name, const char *fen, Position *pos) {
    if (!position_from_fen(pos, fen)) {
        failf(test_name, "position_from_fen failed for \"%s\"", fen);
        return false;
    }

    return true;
}

static bool result_has_legal_best_move(const char *test_name, Position *pos, const SearchResult *result) {
    MoveList legal_moves;
    size_t index;

    if (result->best_move == 0) {
        failf(test_name, "search returned no best move");
        return false;
    }

    movegen_generate_legal(pos, &legal_moves);
    for (index = 0; index < legal_moves.count; ++index) {
        if (legal_moves.moves[index] == result->best_move) {
            return true;
        }
    }

    failf(test_name, "best move was not legal in the root position");
    return false;
}

static bool line_reaches_checkmate(
    const char *test_name,
    const char *fen,
    const SearchResult *result,
    int mate_distance
) {
    Position pos;
    int plies_to_apply = (mate_distance * 2) - 1;
    int ply;

    if (!parse_position(test_name, fen, &pos)) {
        return false;
    }

    if (result->pv_length < plies_to_apply) {
        failf(
            test_name,
            "principal variation too short: expected at least %d plies, got %d",
            plies_to_apply,
            result->pv_length
        );
        return false;
    }

    for (ply = 0; ply < plies_to_apply; ++ply) {
        if (!movegen_make_move(&pos, result->pv[ply])) {
            failf(test_name, "movegen_make_move failed while applying PV at ply %d", ply + 1);
            return false;
        }
    }

    if (!movegen_is_checkmate(&pos)) {
        failf(test_name, "PV did not end in checkmate after %d plies", plies_to_apply);
        return false;
    }

    return true;
}

static bool search_position(
    const char *test_name,
    const char *fen,
    int depth,
    int time_limit_ms,
    SearchResult *result,
    Position *root_position
) {
    if (!parse_position(test_name, fen, root_position)) {
        return false;
    }

    if (!search_iterative_deepening(root_position, depth, time_limit_ms, result)) {
        failf(test_name, "search_iterative_deepening failed");
        return false;
    }

    if (!result_has_legal_best_move(test_name, root_position, result)) {
        return false;
    }

    return true;
}

static void test_mate_in_one_positions(void) {
    struct MateCase {
        const char *name;
        const char *fen;
    } cases[] = {
        { "mate_in_one_queen_box", "7k/5Q2/5K2/8/8/8/8/8 w - - 0 1" },
        { "mate_in_one_rook_wall", "7k/8/6K1/8/8/8/8/R7 w - - 0 1" },
        { "mate_in_one_corner_net", "k7/2Q5/2K5/8/8/8/8/8 w - - 0 1" }
    };
    size_t index;

    ++tests_run;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SearchResult result;
        Position root_position;

        if (!search_position(cases[index].name, cases[index].fen, 2, 0, &result, &root_position)) {
            continue;
        }

        expect_true(cases[index].name, search_is_mate_score(result.score), "score should indicate a forced mate");
        expect_int_eq(cases[index].name, search_mate_distance(result.score), 1, "mate distance");
        expect_true(
            cases[index].name,
            line_reaches_checkmate(cases[index].name, cases[index].fen, &result, 1),
            "PV should mate in one"
        );
    }
}

static void test_mate_in_two_positions(void) {
    struct MateCase {
        const char *name;
        const char *fen;
    } cases[] = {
        { "mate_in_two_queen_shrink_left", "k7/8/2KQ4/8/8/8/8/8 w - - 0 1" },
        { "mate_in_two_queen_shrink_right", "7k/8/4QK2/8/8/8/8/8 w - - 0 1" }
    };
    size_t index;

    ++tests_run;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SearchResult result;
        Position root_position;

        if (!search_position(cases[index].name, cases[index].fen, 4, 0, &result, &root_position)) {
            continue;
        }

        expect_true(cases[index].name, search_is_mate_score(result.score), "score should indicate a forced mate");
        expect_int_eq(cases[index].name, search_mate_distance(result.score), 2, "mate distance");
        expect_true(
            cases[index].name,
            line_reaches_checkmate(cases[index].name, cases[index].fen, &result, 2),
            "PV should mate in two"
        );
    }
}

static void test_mate_in_three_position(void) {
    const char *test_name = "mate_in_three_queen_squeeze";
    const char *fen = "7k/8/4Q3/5K2/8/8/8/8 w - - 0 1";
    SearchResult result;
    Position root_position;

    ++tests_run;

    if (!search_position(test_name, fen, 6, 0, &result, &root_position)) {
        return;
    }

    expect_true(test_name, search_is_mate_score(result.score), "score should indicate a forced mate");
    expect_int_eq(test_name, search_mate_distance(result.score), 3, "mate distance");
    expect_true(test_name, line_reaches_checkmate(test_name, fen, &result, 3), "PV should mate in three");
}

static void test_tactical_positions(void) {
    struct TacticCase {
        const char *name;
        const char *fen;
        const char *best_move;
    } cases[] = {
        { "tactic_knight_fork", "4k3/3q4/8/7N/8/8/8/6K1 w - - 0 1", "h5f6" },
        { "tactic_pin_exploitation", "4k3/4r3/3B4/8/8/8/8/4R1K1 w - - 0 1", "e1e7" },
        { "tactic_discovered_attack", "6k1/4q3/8/8/8/8/4B3/4R1K1 w - - 0 1", "e2c4" },
        { "tactic_removal_of_defender", "3q2k1/3r4/8/1B6/8/8/8/3R2K1 w - - 0 1", "b5d7" }
    };
    size_t index;

    ++tests_run;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SearchResult result;
        Position root_position;
        char best_move_uci[6];

        if (!search_position(cases[index].name, cases[index].fen, 4, 0, &result, &root_position)) {
            continue;
        }

        if (!search_move_to_uci(result.best_move, best_move_uci)) {
            failf(cases[index].name, "failed to convert best move to UCI");
            continue;
        }

        expect_true(
            cases[index].name,
            strcmp(best_move_uci, cases[index].best_move) == 0,
            "best move did not match expected tactical solution"
        );
    }
}

static void test_iterative_deepening_depth_logging(void) {
    const char *test_name = "iterative_deepening_depth_logging";
    const char *fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    SearchResult result;
    Position root_position;
    int index;

    ++tests_run;

    if (!search_position(test_name, fen, 6, 10000, &result, &root_position)) {
        return;
    }

    expect_true(test_name, result.completed_depth >= 6, "search should complete depth 6 within 10 seconds");
    expect_true(test_name, result.elapsed_ms <= 10000.0, "search should stay within the 10 second limit");
    expect_int_eq(test_name, result.iteration_count, result.completed_depth, "iteration count");

    for (index = 0; index < result.iteration_count; ++index) {
        char best_move_uci[6] = "";

        expect_int_eq(test_name, result.iterations[index].depth, index + 1, "logged depth");
        expect_true(test_name, result.iterations[index].nodes > 0, "each depth should search at least one node");
        expect_true(test_name, result.iterations[index].pv_length > 0, "each depth should have a PV");

        if (search_move_to_uci(result.iterations[index].best_move, best_move_uci)) {
            printf(
                "info depth %d score %d nodes %llu pv %s\n",
                result.iterations[index].depth,
                result.iterations[index].score,
                (unsigned long long) result.iterations[index].nodes,
                best_move_uci
            );
        }
    }
}

int test_search_run(void) {
    tests_run = 0;
    tests_failed = 0;

    search_reset_heuristics();
    test_mate_in_one_positions();
    test_mate_in_two_positions();
    test_mate_in_three_position();
    test_tactical_positions();
    test_iterative_deepening_depth_logging();

    if (tests_failed == 0) {
        printf("PASS: %d search tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d search tests failed\n", tests_failed, tests_run);
    return 1;
}
