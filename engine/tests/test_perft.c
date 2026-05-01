#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/bitboard.h"
#include "../src/movegen.h"
#include "../src/position.h"
#include "test_suites.h"

/* Use gettimeofday for portable timing (clock_t may be hidden by system headers) */
#include <sys/time.h>

static double portable_clock(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

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

static char promotion_piece_to_char(PieceType piece_type) {
    switch (piece_type) {
        case KNIGHT:
            return 'n';
        case BISHOP:
            return 'b';
        case ROOK:
            return 'r';
        case QUEEN:
            return 'q';
        default:
            return '\0';
    }
}

static bool move_to_uci(Move move, char buffer[6]) {
    char source[3];
    char target[3];
    char promotion_char = promotion_piece_to_char(move_promotion_piece(move));

    if (buffer == NULL ||
        !bitboard_square_to_string(move_source(move), source) ||
        !bitboard_square_to_string(move_target(move), target)) {
        return false;
    }

    buffer[0] = source[0];
    buffer[1] = source[1];
    buffer[2] = target[0];
    buffer[3] = target[1];

    if (promotion_char != '\0') {
        buffer[4] = promotion_char;
        buffer[5] = '\0';
    } else {
        buffer[4] = '\0';
    }

    return true;
}

static bool move_list_has_duplicates(const MoveList *list, Move *duplicate_move) {
    size_t first_index;
    size_t second_index;

    if (list == NULL) {
        return false;
    }

    for (first_index = 0; first_index < list->count; ++first_index) {
        for (second_index = first_index + 1; second_index < list->count; ++second_index) {
            if (list->moves[first_index] == list->moves[second_index]) {
                if (duplicate_move != NULL) {
                    *duplicate_move = list->moves[first_index];
                }
                return true;
            }
        }
    }

    return false;
}

static bool validate_no_duplicates_recursive(Position *pos, int depth, const char *test_name) {
    MoveList list;
    Move duplicate_move;
    size_t index;

    if (pos == NULL || depth <= 0) {
        return true;
    }

    movegen_generate_legal(pos, &list);

    if (move_list_has_duplicates(&list, &duplicate_move)) {
        char move_uci[6];

        if (!move_to_uci(duplicate_move, move_uci)) {
            snprintf(move_uci, sizeof(move_uci), "????");
        }

        failf(test_name, "duplicate legal move generated: %s", move_uci);
        return false;
    }

    if (depth == 1) {
        return true;
    }

    for (index = 0; index < list.count; ++index) {
        if (!movegen_make_move(pos, list.moves[index])) {
            failf(test_name, "movegen_make_move failed during duplicate validation");
            return false;
        }

        if (!validate_no_duplicates_recursive(pos, depth - 1, test_name)) {
            return false;
        }

        if (!movegen_unmake_move(pos)) {
            failf(test_name, "movegen_unmake_move failed during duplicate validation");
            return false;
        }
    }

    return true;
}

static uint64_t perft(Position *pos, int depth) {
    MoveList list;
    uint64_t nodes = 0;
    size_t index;

    if (depth <= 0) {
        return 1;
    }

    movegen_generate_legal(pos, &list);

    if (depth == 1) {
        return (uint64_t) list.count;
    }

    for (index = 0; index < list.count; ++index) {
        if (!movegen_make_move(pos, list.moves[index])) {
            return 0;
        }

        nodes += perft(pos, depth - 1);

        if (!movegen_unmake_move(pos)) {
            return 0;
        }
    }

    return nodes;
}

static void divide_perft(Position *pos, int depth) {
    MoveList list;
    size_t index;

    if (pos == NULL || depth <= 0) {
        return;
    }

    movegen_generate_legal(pos, &list);
    fprintf(stderr, "divide perft depth %d (%zu legal root moves)\n", depth, list.count);

    for (index = 0; index < list.count; ++index) {
        uint64_t nodes;
        char move_uci[6];

        if (!move_to_uci(list.moves[index], move_uci)) {
            snprintf(move_uci, sizeof(move_uci), "????");
        }

        if (!movegen_make_move(pos, list.moves[index])) {
            fprintf(stderr, "  %s: <make_move failed>\n", move_uci);
            continue;
        }

        nodes = perft(pos, depth - 1);
        fprintf(stderr, "  %s: %llu\n", move_uci, (unsigned long long) nodes);

        if (!movegen_unmake_move(pos)) {
            fprintf(stderr, "  %s: <unmake_move failed>\n", move_uci);
            return;
        }
    }
}

static void run_curated_move_count_case(const char *test_name, const char *fen, uint64_t expected_moves) {
    Position pos;
    MoveList list;
    Move duplicate_move;
    char move_uci[6];

    ++tests_run;

    if (!parse_position(test_name, fen, &pos)) {
        return;
    }

    movegen_generate_legal(&pos, &list);
    expect_u64_eq(test_name, (uint64_t) list.count, expected_moves, "legal move count");

    if (move_list_has_duplicates(&list, &duplicate_move)) {
        if (!move_to_uci(duplicate_move, move_uci)) {
            snprintf(move_uci, sizeof(move_uci), "????");
        }
        failf(test_name, "duplicate legal move generated at root: %s", move_uci);
    }
}

static void run_standard_perft_case(
    const char *test_name,
    const char *fen,
    int max_depth,
    const uint64_t expected_nodes[7],
    bool enforce_performance_limit
) {
    Position pos;
    int depth;

    ++tests_run;

    if (!parse_position(test_name, fen, &pos)) {
        return;
    }

    expect_true(
        test_name,
        validate_no_duplicates_recursive(&pos, 2, test_name),
        "duplicate-move validation failed"
    );

    for (depth = 1; depth <= max_depth; ++depth) {
        Position iteration_pos;
        uint64_t actual_nodes;
        double started_at = 0.0;
        double elapsed_seconds = 0.0;

        if (!parse_position(test_name, fen, &iteration_pos)) {
            return;
        }

        if (enforce_performance_limit && depth == max_depth) {
            started_at = portable_clock();
        }

        actual_nodes = perft(&iteration_pos, depth);

        if (enforce_performance_limit && depth == max_depth) {
            elapsed_seconds = portable_clock() - started_at;
            if (elapsed_seconds > 30.0) {
                failf(
                    test_name,
                    "depth %d exceeded performance target: %.3f seconds",
                    depth,
                    elapsed_seconds
                );
            }
        }

        if (actual_nodes != expected_nodes[depth]) {
            failf(
                test_name,
                "depth %d nodes mismatch: expected %llu, got %llu",
                depth,
                (unsigned long long) expected_nodes[depth],
                (unsigned long long) actual_nodes
            );

            if (parse_position(test_name, fen, &iteration_pos)) {
                divide_perft(&iteration_pos, depth);
            }

            return;
        }
    }
}

int test_perft_run(void) {
    static const uint64_t position_1_nodes[7] = { 0, 20, 400, 8902, 197281, 4865609, 0 };
    static const uint64_t position_2_nodes[7] = { 0, 48, 2039, 97862, 4085603, 0, 0 };
    static const uint64_t position_3_nodes[7] = { 0, 14, 191, 2812, 43238, 674624, 11030083 };
    static const uint64_t position_4_nodes[7] = { 0, 6, 264, 9467, 422333, 15833292, 0 };
    static const uint64_t position_5_nodes[7] = { 0, 44, 1486, 62379, 2103487, 0, 0 };
    static const uint64_t position_6_nodes[7] = { 0, 46, 2079, 89890, 3894594, 0, 0 };

    tests_run = 0;
    tests_failed = 0;

    run_curated_move_count_case(
        "curated_starting_position_move_count",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        20
    );
    run_curated_move_count_case(
        "curated_castling_position_move_count",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        26
    );
    run_curated_move_count_case(
        "curated_en_passant_position_move_count",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        7
    );
    run_curated_move_count_case(
        "curated_promotion_position_move_count",
        "2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1",
        13
    );
    run_curated_move_count_case(
        "curated_checkmate_position_move_count",
        "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1",
        0
    );
    run_curated_move_count_case(
        "curated_stalemate_position_move_count",
        "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1",
        0
    );

    run_standard_perft_case(
        "perft_position_1_starting_position",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        5,
        position_1_nodes,
        true
    );
    run_standard_perft_case(
        "perft_position_2_kiwipete",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        4,
        position_2_nodes,
        false
    );
    run_standard_perft_case(
        "perft_position_3",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        6,
        position_3_nodes,
        false
    );
    run_standard_perft_case(
        "perft_position_4",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        5,
        position_4_nodes,
        false
    );
    run_standard_perft_case(
        "perft_position_5",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        4,
        position_5_nodes,
        false
    );
    run_standard_perft_case(
        "perft_position_6",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        4,
        position_6_nodes,
        false
    );

    if (tests_failed == 0) {
        printf("PASS: %d perft tests passed\n", tests_run);
        return 0;
    }

    fprintf(stderr, "FAIL: %d of %d perft tests failed\n", tests_failed, tests_run);
    return 1;
}
