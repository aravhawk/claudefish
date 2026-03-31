#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/evaluate.h"
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

static void expect_abs_le(const char *test_name, long long actual, long long limit, const char *label) {
    if (llabs(actual) > limit) {
        failf(test_name, "%s exceeded limit: |%lld| > %lld", label, actual, limit);
    }
}

static void expect_between(const char *test_name, long long actual, long long minimum, long long maximum, const char *label) {
    if (actual < minimum || actual > maximum) {
        failf(
            test_name,
            "%s out of range: expected [%lld, %lld], got %lld",
            label,
            minimum,
            maximum,
            actual
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

static int evaluate_fen(const char *test_name, const char *fen) {
    Position pos;

    if (!parse_position(test_name, fen, &pos)) {
        return 0;
    }

    return eval_evaluate(&pos);
}

static bool move_to_uci(Move move, char buffer[6]) {
    char source[3];
    char target[3];
    PieceType promotion_piece = move_promotion_piece(move);

    if (!bitboard_square_to_string(move_source(move), source) ||
        !bitboard_square_to_string(move_target(move), target)) {
        return false;
    }

    buffer[0] = source[0];
    buffer[1] = source[1];
    buffer[2] = target[0];
    buffer[3] = target[1];

    if (promotion_piece == MOVE_PROMOTION_NONE) {
        buffer[4] = '\0';
        return true;
    }

    switch (promotion_piece) {
        case KNIGHT:
            buffer[4] = 'n';
            break;
        case BISHOP:
            buffer[4] = 'b';
            break;
        case ROOK:
            buffer[4] = 'r';
            break;
        case QUEEN:
            buffer[4] = 'q';
            break;
        default:
            return false;
    }

    buffer[5] = '\0';
    return true;
}

static bool find_move_by_uci(const MoveList *list, const char *uci, Move *out_move) {
    size_t index;

    for (index = 0; index < list->count; ++index) {
        char move_uci[6];

        if (!move_to_uci(list->moves[index], move_uci)) {
            continue;
        }

        if (strcmp(move_uci, uci) == 0) {
            if (out_move != NULL) {
                *out_move = list->moves[index];
            }
            return true;
        }
    }

    return false;
}

static void expect_pst_equal(const char *test_name, const Position *actual, const Position *expected, const char *label) {
    if (actual->pst_mg[WHITE] != expected->pst_mg[WHITE] ||
        actual->pst_mg[BLACK] != expected->pst_mg[BLACK] ||
        actual->pst_eg[WHITE] != expected->pst_eg[WHITE] ||
        actual->pst_eg[BLACK] != expected->pst_eg[BLACK]) {
        failf(
            test_name,
            "%s mismatch: mg=(%d,%d) eg=(%d,%d) vs expected mg=(%d,%d) eg=(%d,%d)",
            label,
            actual->pst_mg[WHITE],
            actual->pst_mg[BLACK],
            actual->pst_eg[WHITE],
            actual->pst_eg[BLACK],
            expected->pst_mg[WHITE],
            expected->pst_mg[BLACK],
            expected->pst_eg[WHITE],
            expected->pst_eg[BLACK]
        );
    }
}

static void test_equal_positions_and_symmetry(void) {
    const char *test_name = "equal_positions_and_symmetry";
    int start_score;
    int bare_kings_score;
    int white_knight_white_to_move;
    int black_knight_white_to_move;
    int white_knight_black_to_move;

    ++tests_run;

    start_score = evaluate_fen(
        test_name,
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    );
    bare_kings_score = evaluate_fen(test_name, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    white_knight_white_to_move = evaluate_fen(test_name, "4k3/8/8/3N4/8/8/8/4K3 w - - 0 1");
    black_knight_white_to_move = evaluate_fen(test_name, "4k3/8/8/8/3n4/8/8/4K3 w - - 0 1");
    white_knight_black_to_move = evaluate_fen(test_name, "4k3/8/8/3N4/8/8/8/4K3 b - - 0 1");

    expect_abs_le(test_name, start_score, 50, "starting position score");
    expect_abs_le(test_name, bare_kings_score, 50, "bare kings score");
    expect_abs_le(
        test_name,
        white_knight_white_to_move + black_knight_white_to_move,
        5,
        "white-to-move mirrored symmetry sum"
    );
    expect_abs_le(
        test_name,
        white_knight_white_to_move + white_knight_black_to_move,
        5,
        "side-to-move relative symmetry sum"
    );
}

static void test_material_advantage_scores(void) {
    const char *test_name = "material_advantage_scores";
    int pawn_score;
    int piece_score;
    int queen_score;

    ++tests_run;

    pawn_score = evaluate_fen(test_name, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    piece_score = evaluate_fen(test_name, "4k3/8/8/8/8/8/3R4/4K3 w - - 0 1");
    queen_score = evaluate_fen(test_name, "4k3/8/8/8/8/8/3Q4/4K3 w - - 0 1");

    expect_between(test_name, pawn_score, 50, 200, "extra pawn score");
    expect_true(test_name, piece_score >= 250, "extra piece score should be at least 250cp");
    expect_true(test_name, queen_score >= 800, "extra queen score should be at least 800cp");
}

static void test_phase_scaling_and_tapering(void) {
    const char *test_name = "phase_scaling_and_tapering";
    Position start_pos;
    Position bare_kings;
    Position middlegame_pos;
    Position endgame_pos;
    int start_phase;
    int bare_phase;
    int middlegame_phase;
    int endgame_phase;
    int middlegame_score;
    int endgame_score;

    ++tests_run;

    if (!parse_position(test_name, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &start_pos) ||
        !parse_position(test_name, "4k3/8/8/8/8/8/8/4K3 w - - 0 1", &bare_kings) ||
        !parse_position(test_name, "rnbqkbnr/pppppppp/8/3N4/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &middlegame_pos) ||
        !parse_position(test_name, "4k3/8/8/3N4/8/8/8/4K3 w - - 0 1", &endgame_pos)) {
        return;
    }

    start_phase = eval_calculate_phase(&start_pos);
    bare_phase = eval_calculate_phase(&bare_kings);
    middlegame_phase = eval_calculate_phase(&middlegame_pos);
    endgame_phase = eval_calculate_phase(&endgame_pos);
    middlegame_score = eval_evaluate(&middlegame_pos);
    endgame_score = eval_evaluate(&endgame_pos);

    expect_int_eq(test_name, start_phase, 256, "starting position phase");
    expect_int_eq(test_name, bare_phase, 0, "bare kings phase");
    expect_true(test_name, middlegame_phase > endgame_phase, "middlegame phase should exceed endgame phase");
    expect_true(test_name, middlegame_phase >= 240, "middlegame phase should stay near opening");
    expect_true(test_name, endgame_phase <= 20, "endgame phase should stay near zero");
    expect_true(test_name, middlegame_score != endgame_score, "tapered evaluation should differ across phases");
}

static void test_terminal_scores(void) {
    const char *test_name = "terminal_scores";
    int checkmate_score;
    int stalemate_score;

    ++tests_run;

    checkmate_score = evaluate_fen(test_name, "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    stalemate_score = evaluate_fen(test_name, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");

    expect_int_eq(test_name, checkmate_score, -EVAL_MATE_SCORE, "checkmate score");
    expect_int_eq(test_name, stalemate_score, 0, "stalemate score");
}

static void test_pawn_hash_caching(void) {
    const char *test_name = "pawn_hash_caching";
    Position first_pos;
    Position second_pos;
    EvalPawnHashStats stats;
    int first_score;
    int second_score;

    ++tests_run;

    if (!parse_position(test_name, "4k3/8/8/8/3N4/8/4PP2/4K3 w - - 0 1", &first_pos) ||
        !parse_position(test_name, "4k3/8/8/8/8/3N4/4PP2/4K3 w - - 0 1", &second_pos)) {
        return;
    }

    eval_reset_pawn_hash_table();
    eval_reset_pawn_hash_stats();

    first_score = eval_evaluate(&first_pos);
    stats = eval_get_pawn_hash_stats();
    expect_int_eq(test_name, (long long) stats.probes, 1, "probes after first evaluation");
    expect_int_eq(test_name, (long long) stats.hits, 0, "hits after first evaluation");
    expect_int_eq(test_name, (long long) stats.stores, 1, "stores after first evaluation");

    second_score = eval_evaluate(&second_pos);
    stats = eval_get_pawn_hash_stats();
    expect_int_eq(test_name, (long long) stats.probes, 2, "probes after second evaluation");
    expect_int_eq(test_name, (long long) stats.hits, 1, "hits after second evaluation");
    expect_int_eq(test_name, (long long) stats.stores, 1, "stores after second evaluation");
    expect_true(test_name, first_score != second_score, "non-pawn features should still affect total score");
}

static void test_incremental_piece_square_updates(void) {
    const char *test_name = "incremental_piece_square_updates";
    Position pos;
    Position refreshed_pos;
    Position initial_pos;
    MoveList list;
    Move move;
    int initial_score;
    int restored_score;

    ++tests_run;

    if (!parse_position(test_name, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &pos)) {
        return;
    }

    initial_pos = pos;
    initial_score = eval_evaluate(&pos);
    movegen_generate_legal(&pos, &list);

    expect_true(test_name, find_move_by_uci(&list, "e2e4", &move), "missing e2e4 for incremental PST test");
    expect_true(test_name, movegen_make_move(&pos, move), "movegen_make_move should succeed for e2e4");

    refreshed_pos = pos;
    eval_refresh_position_state(&refreshed_pos);
    expect_pst_equal(test_name, &pos, &refreshed_pos, "incremental PST cache after make");
    expect_int_eq(
        test_name,
        eval_evaluate(&pos),
        eval_evaluate(&refreshed_pos),
        "score after refreshing PST cache"
    );

    expect_true(test_name, movegen_unmake_move(&pos), "movegen_unmake_move should restore e2e4");
    expect_pst_equal(test_name, &pos, &initial_pos, "incremental PST cache after unmake");

    restored_score = eval_evaluate(&pos);
    expect_int_eq(test_name, restored_score, initial_score, "score after unmake");
}

int test_evaluate_run(void) {
    tests_run = 0;
    tests_failed = 0;

    test_equal_positions_and_symmetry();
    test_material_advantage_scores();
    test_phase_scaling_and_tapering();
    test_terminal_scores();
    test_pawn_hash_caching();
    test_incremental_piece_square_updates();

    if (tests_failed == 0) {
        printf("PASS: %d evaluation tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d evaluation tests failed\n", tests_failed, tests_run);
    return 1;
}
