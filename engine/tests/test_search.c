#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/evaluate.h"
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

static void expect_u64_eq(const char *test_name, uint64_t actual, uint64_t expected, const char *label) {
    if (actual != expected) {
        failf(
            test_name,
            "%s mismatch: expected %llu, got %llu",
            label,
            (unsigned long long) expected,
            (unsigned long long) actual
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
    search_reset_heuristics();
    search_reset_options();

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

static bool search_position_with_options(
    const char *test_name,
    const char *fen,
    int depth,
    int time_limit_ms,
    const SearchOptions *options,
    SearchResult *result,
    Position *root_position
) {
    search_reset_heuristics();
    if (options == NULL) {
        search_reset_options();
    } else {
        search_set_options(options);
    }

    return search_position(test_name, fen, depth, time_limit_ms, result, root_position);
}

static uint64_t search_total_nodes(const SearchResult *result) {
    return result == NULL ? 0 : result->nodes + result->qnodes;
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

static void collect_immediate_stalemate_moves(Position *pos, MoveList *stalemate_moves) {
    MoveList legal_moves;
    size_t index;

    if (stalemate_moves == NULL) {
        return;
    }

    stalemate_moves->count = 0;
    if (pos == NULL) {
        return;
    }

    movegen_generate_legal(pos, &legal_moves);
    for (index = 0; index < legal_moves.count; ++index) {
        Move move = legal_moves.moves[index];

        if (!movegen_make_move(pos, move)) {
            continue;
        }

        if (movegen_is_stalemate(pos) && stalemate_moves->count < MOVEGEN_MAX_MOVES) {
            stalemate_moves->moves[stalemate_moves->count++] = move;
        }

        movegen_unmake_move(pos);
    }
}

static bool translate_position_fen(
    const char *base_fen,
    int file_delta,
    int rank_delta,
    char out_fen[128]
) {
    Position source;
    Position translated;
    int square;

    if (out_fen == NULL || !position_from_fen(&source, base_fen)) {
        return false;
    }

    position_clear(&translated);
    for (square = A1; square <= H8; ++square) {
        Piece piece = position_get_piece(&source, square);

        if (piece != NO_PIECE) {
            int file = bitboard_file_of(square) + file_delta;
            int rank = bitboard_rank_of(square) + rank_delta;
            int target_square;

            if (file < 0 || file >= 8 || rank < 0 || rank >= 8) {
                return false;
            }

            target_square = bitboard_make_square(file, rank);
            if (position_get_piece(&translated, target_square) != NO_PIECE ||
                !position_set_piece(&translated, target_square, piece)) {
                return false;
            }
        }
    }

    translated.side_to_move = source.side_to_move;
    translated.castling_rights = 0;
    translated.en_passant_sq = NO_SQUARE;
    translated.halfmove_clock = source.halfmove_clock;
    translated.fullmove_number = source.fullmove_number;
    position_refresh_hashes(&translated);

    return position_to_fen(&translated, out_fen, 128);
}

static bool find_stalemate_candidate(
    const char *const *candidates,
    size_t candidate_count,
    bool require_negative_eval,
    const char **out_fen,
    Position *out_position,
    MoveList *out_stalemate_moves,
    int *out_eval
) {
    size_t index;

    for (index = 0; index < candidate_count; ++index) {
        Position pos;
        MoveList stalemate_moves;
        int score;

        if (!position_from_fen(&pos, candidates[index])) {
            continue;
        }

        collect_immediate_stalemate_moves(&pos, &stalemate_moves);
        if (stalemate_moves.count == 0) {
            continue;
        }

        score = eval_evaluate(&pos);
        if ((require_negative_eval && score >= 0) || (!require_negative_eval && score <= 0)) {
            continue;
        }

        if (out_position != NULL) {
            *out_position = pos;
        }
        if (out_fen != NULL) {
            *out_fen = candidates[index];
        }
        if (out_stalemate_moves != NULL) {
            *out_stalemate_moves = stalemate_moves;
        }
        if (out_eval != NULL) {
            *out_eval = score;
        }
        return true;
    }

    return false;
}

static void test_mate_in_one_positions(void) {
    struct MateCase {
        const char *name;
        const char *fen;
    } cases[] = {
        { "mate_in_one_queen_box", "7k/5Q2/5K2/8/8/8/8/8 w - - 0 1" },
        { "mate_in_one_back_rank", "6k1/5ppp/8/8/8/8/4RPPP/6K1 w - - 0 1" },
        { "mate_in_one_smothered", "6rk/6pp/7N/8/8/8/8/4K3 w - - 0 1" }
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

static void test_zugzwang_null_move_safety(void) {
    const char *fens[] = {
        "8/8/8/2k5/8/2P5/3K4/8 w - - 0 1",
        "8/8/8/8/3k4/3P4/4K3/8 w - - 0 1"
    };
    SearchOptions enabled;
    SearchOptions disabled;
    size_t index;

    ++tests_run;

    search_reset_options();
    enabled = search_get_options();
    disabled = enabled;
    disabled.enable_null_move_pruning = false;

    for (index = 0; index < sizeof(fens) / sizeof(fens[0]); ++index) {
        SearchResult with_null;
        SearchResult without_null;
        Position with_null_root;
        Position without_null_root;
        char test_name[64];

        snprintf(test_name, sizeof(test_name), "zugzwang_null_move_%zu", index + 1);

        if (!search_position_with_options(test_name, fens[index], 6, 0, &enabled, &with_null, &with_null_root) ||
            !search_position_with_options(test_name, fens[index], 6, 0, &disabled, &without_null, &without_null_root)) {
            continue;
        }

        expect_int_eq(test_name, with_null.best_move, without_null.best_move, "best move");
        expect_int_eq(test_name, with_null.score, without_null.score, "score");
        expect_u64_eq(
            test_name,
            search_total_nodes(&with_null),
            search_total_nodes(&without_null),
            "node count without null-move usage"
        );
    }
}

static void test_lmr_matches_non_lmr_on_tactical_positions(void) {
    const char *base_positions[] = {
        "7k/5Q2/5K2/8/8/8/8/8 w - - 0 1",
        "7k/8/6K1/8/8/8/8/R7 w - - 0 1",
        "k7/2Q5/2K5/8/8/8/8/8 w - - 0 1",
        "k7/8/2KQ4/8/8/8/8/8 w - - 0 1",
        "7k/8/4QK2/8/8/8/8/8 w - - 0 1",
        "7k/8/4Q3/5K2/8/8/8/8 w - - 0 1",
        "4k3/3q4/8/7N/8/8/8/6K1 w - - 0 1",
        "4k3/4r3/3B4/8/8/8/8/4R1K1 w - - 0 1",
        "6k1/4q3/8/8/8/8/4B3/4R1K1 w - - 0 1",
        "3q2k1/3r4/8/1B6/8/8/8/3R2K1 w - - 0 1"
    };
    const int translations[][2] = {
        { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 2, 0 }, { -2, 0 }, { 1, 1 }, { -1, 1 }, { 1, -1 },
        { -1, -1 }, { 0, 2 }, { 0, -2 }, { 2, 1 }, { 1, 2 },
        { 2, 2 }, { 3, 0 }, { 0, 3 }, { 3, 1 }, { 1, 3 },
        { 4, 0 }, { 0, 4 }, { 2, 3 }, { 3, 2 }, { 4, 1 },
        { 4, 2 }, { 2, 4 }, { 5, 0 }
    };
    SearchOptions with_lmr;
    SearchOptions without_lmr;
    int compared_positions = 0;
    size_t base_index;

    ++tests_run;

    search_reset_options();
    with_lmr = search_get_options();
    without_lmr = with_lmr;
    without_lmr.enable_lmr = false;

    for (base_index = 0; base_index < sizeof(base_positions) / sizeof(base_positions[0]); ++base_index) {
        size_t translation_index;

        for (translation_index = 0;
             translation_index < sizeof(translations) / sizeof(translations[0]) && compared_positions < 50;
             ++translation_index) {
            char fen[128];
            char test_name[64];
            SearchResult lmr_result;
            SearchResult no_lmr_result;
            Position lmr_root;
            Position no_lmr_root;
            char lmr_best_move[6];
            char no_lmr_best_move[6];

            if (!translate_position_fen(
                    base_positions[base_index],
                    translations[translation_index][0],
                    translations[translation_index][1],
                    fen)) {
                continue;
            }

            snprintf(test_name, sizeof(test_name), "lmr_consistency_%d", compared_positions + 1);
            if (!search_position_with_options(test_name, fen, 5, 0, &with_lmr, &lmr_result, &lmr_root) ||
                !search_position_with_options(test_name, fen, 5, 0, &without_lmr, &no_lmr_result, &no_lmr_root)) {
                continue;
            }

            if (!search_move_to_uci(lmr_result.best_move, lmr_best_move) ||
                !search_move_to_uci(no_lmr_result.best_move, no_lmr_best_move)) {
                failf(test_name, "failed to convert best moves to UCI");
                continue;
            }

            expect_true(
                test_name,
                strcmp(lmr_best_move, no_lmr_best_move) == 0,
                "LMR changed the selected best move"
            );

            ++compared_positions;
        }
    }

    expect_int_eq("lmr_consistency_total", compared_positions, 50, "tactical positions compared");
}

static void test_lmr_threshold_table(void) {
    const char *test_name = "lmr_threshold_table";

    ++tests_run;

    expect_int_eq(test_name, search_debug_lmr_reduction(2, 8), 0, "depth-2 reduction");
    expect_true(test_name, search_debug_lmr_reduction(3, 4) > 0, "depth-3 reduction should be enabled");
    expect_true(test_name, search_debug_lmr_reduction(4, 5) > 0, "depth-4 reduction should be enabled");
}

static void test_stalemate_preferences(void) {
    const char *winning_candidates[] = {
        "7k/5K2/6Q1/8/8/8/8/8 w - - 0 1",
        "7k/5K2/7Q/8/8/8/8/8 w - - 0 1"
    };
    const char *losing_candidates[] = {
        "7k/5K2/6B1/8/8/8/8/1q6 w - - 0 1",
        "7k/5K2/6B1/8/8/8/2q5/8 w - - 0 1",
        "7k/5K2/6B1/8/8/3q4/8/8 w - - 0 1"
    };
    Position winning_position;
    Position losing_position;
    MoveList winning_stalemates;
    MoveList losing_stalemates;
    SearchResult winning_result;
    SearchResult losing_result;
    const char *winning_fen = NULL;
    const char *losing_fen = NULL;
    int winning_eval = 0;
    int losing_eval = 0;

    ++tests_run;

    expect_true(
        "stalemate_preferences_winning_setup",
        find_stalemate_candidate(
            winning_candidates,
            sizeof(winning_candidates) / sizeof(winning_candidates[0]),
            false,
            &winning_fen,
            &winning_position,
            &winning_stalemates,
            &winning_eval
        ),
        "expected a winning position with at least one immediate stalemate move"
    );

    expect_true(
        "stalemate_preferences_losing_setup",
        find_stalemate_candidate(
            losing_candidates,
            sizeof(losing_candidates) / sizeof(losing_candidates[0]),
            true,
            &losing_fen,
            &losing_position,
            &losing_stalemates,
            &losing_eval
        ),
        "expected a losing position with at least one immediate stalemate move"
    );

    if (!search_position_with_options(
            "stalemate_preferences_winning",
            winning_fen,
            1,
            0,
            NULL,
            &winning_result,
            &winning_position)) {
        return;
    }

    expect_true(
        "stalemate_preferences_winning",
        winning_eval > 0,
        "winning position should be evaluated as favorable"
    );
    expect_true(
        "stalemate_preferences_winning",
        !move_list_contains(&winning_stalemates, winning_result.best_move),
        "winning side should avoid an immediate stalemate move"
    );

    if (!search_position_with_options(
            "stalemate_preferences_losing",
            losing_fen,
            1,
            0,
            NULL,
            &losing_result,
            &losing_position)) {
        return;
    }

    expect_true(
        "stalemate_preferences_losing",
        losing_eval < 0,
        "losing position should be evaluated as unfavorable"
    );
    expect_true(
        "stalemate_preferences_losing",
        move_list_contains(&losing_stalemates, losing_result.best_move),
        "losing side should choose an immediate stalemate move when available"
    );
    expect_true(
        "stalemate_preferences_losing",
        losing_result.score > 0,
        "stalemate-seeking preference should outrank losing continuations"
    );
}

int test_search_run(void) {
    tests_run = 0;
    tests_failed = 0;

    search_reset_heuristics();
    search_reset_options();
    test_mate_in_one_positions();
    test_mate_in_two_positions();
    test_mate_in_three_position();
    test_tactical_positions();
    test_iterative_deepening_depth_logging();
    test_zugzwang_null_move_safety();
    test_lmr_matches_non_lmr_on_tactical_positions();
    test_lmr_threshold_table();
    test_stalemate_preferences();

    if (tests_failed == 0) {
        printf("PASS: %d search tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d search tests failed\n", tests_failed, tests_run);
    return 1;
}
