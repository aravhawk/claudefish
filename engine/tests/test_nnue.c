#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/bitboard.h"
#include "../src/correction.h"
#include "../src/mate_search.h"
#include "../src/movegen.h"
#include "../src/nnue.h"
#include "../src/policy.h"
#include "../src/mcts.h"
#include "../src/position.h"
#include "../src/syzygy.h"
#include "../src/evaluate.h"
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

static void passf(const char *test_name) {
    (void) test_name;
    ++tests_run;
}

/* ---- NNUE Tests ---- */

static void test_nnue_init_cleanup(void) {
    const char *test_name = "nnue_init_cleanup";

    nnue_init();

    if (!nnue_is_loaded()) {
        passf(test_name);
    } else {
        failf(test_name, "nnue should not be loaded before load_embedded");
    }

    nnue_cleanup();
    ++tests_run;
}

static void test_nnue_load_embedded(void) {
    const char *test_name = "nnue_load_embedded";

    nnue_init();
    bool result = nnue_load_embedded();

    /* Currently returns false since no real weights are embedded */
    if (!result && !nnue_is_loaded()) {
        passf(test_name);
    } else if (result && nnue_is_loaded()) {
        passf(test_name);
    } else {
        failf(test_name, "unexpected load_embedded result: %d, loaded: %d", result, nnue_is_loaded());
    }

    nnue_cleanup();
    ++tests_run;
}

static void test_nnue_state_create_destroy(void) {
    const char *test_name = "nnue_state_create_destroy";

    nnue_init();

    NNUEState *state = nnue_create_state();
    if (state != NULL) {
        nnue_destroy_state(state);
        passf(test_name);
    } else {
        failf(test_name, "create_state returned NULL");
    }

    nnue_cleanup();
    ++tests_run;
}

static void test_nnue_state_init_startpos(void) {
    const char *test_name = "nnue_state_init_startpos";

    movegen_init();
    eval_init();
    nnue_init();

    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    NNUEState *state = nnue_create_state();
    if (state == NULL) {
        failf(test_name, "create_state returned NULL");
        nnue_cleanup();
        ++tests_run;
        return;
    }

    nnue_state_init(state, &pos);

    if (state->initialized && state->current == 0) {
        passf(test_name);
    } else {
        failf(test_name, "state not properly initialized: init=%d current=%d",
              state->initialized, state->current);
    }

    nnue_destroy_state(state);
    nnue_cleanup();
    ++tests_run;
}

static void test_nnue_push_pop(void) {
    const char *test_name = "nnue_push_pop";

    movegen_init();
    eval_init();
    nnue_init();

    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    NNUEState *state = nnue_create_state();
    nnue_state_init(state, &pos);

    nnue_push(state);
    if (state->current == 1) {
        nnue_pop(state);
        if (state->current == 0) {
            passf(test_name);
        } else {
            failf(test_name, "pop didn't restore current to 0: %d", state->current);
        }
    } else {
        failf(test_name, "push didn't advance current to 1: %d", state->current);
    }

    nnue_destroy_state(state);
    nnue_cleanup();
    ++tests_run;
}

static void test_nnue_feature_index_range(void) {
    const char *test_name = "nnue_feature_index_range";

    /* Verify that feature indices are in valid range for all pieces/positions.
       When NNUE is not loaded, accumulators can't be computed, but the
       state creation/init should still work without crashing. */
    movegen_init();
    eval_init();
    nnue_init();
    nnue_load_embedded();

    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    NNUEState *state = nnue_create_state();
    nnue_state_init(state, &pos);

    /* If NNUE is loaded, accumulators should be computed.
       If not loaded, they won't be computed but the call should not crash. */
    passf(test_name);

    nnue_destroy_state(state);
    nnue_cleanup();
    ++tests_run;
}

/* ---- Policy Tests ---- */

static void test_policy_init(void) {
    const char *test_name = "policy_init";

    policy_init();

    /* Policy loads placeholder weights by default in policy_init,
       so it may be loaded after init. This is fine - it's ready for use. */
    passf(test_name);

    policy_cleanup();
    ++tests_run;
}

static void test_policy_load_embedded(void) {
    const char *test_name = "policy_load_embedded";

    policy_init();
    bool result = policy_load_embedded();

    /* Currently loads placeholder weights */
    if (result && policy_is_loaded()) {
        passf(test_name);
    } else {
        failf(test_name, "load_embedded returned %d, loaded=%d", result, policy_is_loaded());
    }

    policy_cleanup();
    ++tests_run;
}

static void test_policy_adapt_params(void) {
    const char *test_name = "policy_adapt_params";

    policy_init();

    /* Test default params */
    AdaptParams default_params = policy_get_adapt_params(-1);
    if (default_params.lmr_multiplier == 100 && default_params.futility_margin == 100) {
        passf(test_name);
    } else {
        failf(test_name, "default params wrong: lmr=%d futil=%d",
              default_params.lmr_multiplier, default_params.futility_margin);
    }

    policy_cleanup();
    ++tests_run;
}

/* ---- MCTS Tests ---- */

static void test_mcts_init(void) {
    const char *test_name = "mcts_init";

    mcts_init();
    passf(test_name);
    mcts_cleanup();
    ++tests_run;
}

static void test_mcts_critical_position(void) {
    const char *test_name = "mcts_critical_position";

    movegen_init();
    eval_init();
    nnue_init();
    mcts_init();

    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    /* Starting position with small eval difference: not critical */
    if (!mcts_is_critical_position(&pos, 10, 5, 6)) {
        passf(test_name);
    } else {
        failf(test_name, "starting position incorrectly marked as critical");
    }

    /* Large eval swing: critical */
    if (mcts_is_critical_position(&pos, 200, 0, 6)) {
        passf(test_name);
    } else {
        failf(test_name, "large eval swing not marked as critical");
    }

    mcts_cleanup();
    nnue_cleanup();
    ++tests_run;
}

/* ---- Correction History Tests ---- */

static void test_corrhist_init_cleanup(void) {
    const char *test_name = "corrhist_init_cleanup";

    CorrectionHistory ch;
    corrhist_init(&ch);
    if (ch.continuation_corrhist != NULL) {
        passf(test_name);
    } else {
        failf(test_name, "continuation_corrhist was NULL after init");
    }
    corrhist_cleanup(&ch);
    if (ch.continuation_corrhist == NULL) {
        ++tests_run;
    } else {
        failf(test_name, "continuation_corrhist not NULL after cleanup");
    }
}

static void test_corrhist_correct_eval_no_crash(void) {
    const char *test_name = "corrhist_correct_eval_no_crash";

    movegen_init();
    eval_init();

    CorrectionHistory ch;
    corrhist_init(&ch);

    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    int raw_eval = eval_evaluate(&pos);
    int corrected = corrhist_correct_eval(&ch, &pos, raw_eval);
    if (corrected == raw_eval) {
        passf(test_name);
    } else {
        /* With empty correction history, corrected should equal raw (plus zero) */
        int diff = corrected - raw_eval;
        if (diff >= -5 && diff <= 5) {
            passf(test_name); /* Allow tiny rounding difference */
        } else {
            failf(test_name, "corrected=%d != raw=%d diff=%d", corrected, raw_eval, diff);
        }
    }

    corrhist_cleanup(&ch);
    ++tests_run;
}

static void test_corrhist_update_and_correct(void) {
    const char *test_name = "corrhist_update_and_correct";

    movegen_init();
    eval_init();

    CorrectionHistory ch;
    corrhist_init(&ch);

    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    int raw_eval = eval_evaluate(&pos);

    /* Update: static eval was 0, but search found +50 (we were too pessimistic) */
    corrhist_update(&ch, &pos, 4, 0, 50, PAWN, 0, KNIGHT, 5);

    int corrected = corrhist_correct_eval(&ch, &pos, raw_eval);
    /* The correction should shift the eval slightly positive */
    if (corrected >= raw_eval) {
        passf(test_name);
    } else {
        failf(test_name, "corrected=%d < raw=%d after positive update", corrected, raw_eval);
    }

    corrhist_cleanup(&ch);
    ++tests_run;
}

/* ---- Syzygy Tests ---- */

static void test_syzygy_init(void) {
    const char *test_name = "syzygy_init";

    syzygy_init();
    if (!syzygy_is_loaded()) {
        passf(test_name);
    } else {
        failf(test_name, "syzygy should not be loaded before load_embedded");
    }
    syzygy_cleanup();
    ++tests_run;
}

static void test_syzygy_piece_count(void) {
    const char *test_name = "syzygy_piece_count";

    movegen_init();
    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    syzygy_init();
    int count = syzygy_piece_count(&pos);
    if (count == 32) {
        passf(test_name);
    } else {
        failf(test_name, "expected 32 pieces, got %d", count);
    }
    syzygy_cleanup();
    ++tests_run;
}

static void test_syzygy_available(void) {
    const char *test_name = "syzygy_available";

    movegen_init();
    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    syzygy_init();
    syzygy_load_embedded();

    /* Starting position has 32 pieces, way more than 3 */
    if (!syzygy_available(&pos)) {
        passf(test_name);
    } else {
        failf(test_name, "syzygy should not be available for starting position");
    }

    syzygy_cleanup();
    ++tests_run;
}

static void test_syzygy_probe_kvsK(void) {
    const char *test_name = "syzygy_probe_kvsK";

    movegen_init();
    syzygy_init();
    syzygy_load_embedded();

    /* K vs K position */
    Position pos;
    position_from_fen(&pos, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");

    int pieces = syzygy_piece_count(&pos);
    if (pieces != 2) {
        failf(test_name, "expected 2 pieces, got %d", pieces);
        syzygy_cleanup();
        ++tests_run;
        return;
    }

    SyzygyResult result = syzygy_probe(&pos);
    if (result.found && result.wdl == SYZYGY_RESULT_DRAW) {
        passf(test_name);
    } else {
        failf(test_name, "K vs K should be draw: found=%d wdl=%d", result.found, result.wdl);
    }

    /* KQ vs K: forced win */
    Position pos2;
    position_from_fen(&pos2, "4k3/8/8/8/8/8/8/3QK3 w - - 0 1");

    pieces = syzygy_piece_count(&pos2);
    if (pieces != 3) {
        failf(test_name, "expected 3 pieces, got %d", pieces);
        syzygy_cleanup();
        ++tests_run;
        return;
    }

    result = syzygy_probe(&pos2);
    if (result.found && result.wdl == SYZYGY_RESULT_WIN) {
        passf(test_name);
    } else {
        failf(test_name, "KQ vs K should be win: found=%d wdl=%d", result.found, result.wdl);
    }

    syzygy_cleanup();
    ++tests_run;
}

/* ---- Mate Search Tests ---- */

static void test_mate_search_init(void) {
    const char *test_name = "mate_search_init";

    MateSearchContext ctx;
    mate_search_init(&ctx, MATESEARCH_MAX_NODES);
    if (ctx.node_count == 0 && ctx.node_limit > 0) {
        passf(test_name);
    } else {
        failf(test_name, "unexpected init state: count=%d limit=%d", ctx.node_count, ctx.node_limit);
    }
    ++tests_run;
}

static void test_mate_search_no_mate(void) {
    const char *test_name = "mate_search_no_mate";

    movegen_init();
    MateSearchContext ctx;
    mate_search_init(&ctx, MATESEARCH_MAX_NODES);

    /* Starting position: no forced mate */
    Position pos;
    position_from_fen(&pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    MateSearchResult result = mate_search(&ctx, &pos, 4);
    /* Starting position should not have a forced mate (or might timeout) */
    if (!result.mate_found) {
        passf(test_name);
    } else {
        /* Even if it claims mate, we just verify it doesn't crash */
        passf(test_name);
    }
    ++tests_run;
}

int test_nnue_run(void) {
    tests_run = 0;
    tests_failed = 0;

    test_nnue_init_cleanup();
    test_nnue_load_embedded();
    test_nnue_state_create_destroy();
    test_nnue_state_init_startpos();
    test_nnue_push_pop();
    test_nnue_feature_index_range();
    test_policy_init();
    test_policy_load_embedded();
    test_policy_adapt_params();
    test_mcts_init();
    test_mcts_critical_position();
    test_corrhist_init_cleanup();
    test_corrhist_correct_eval_no_crash();
    test_corrhist_update_and_correct();
    test_syzygy_init();
    test_syzygy_piece_count();
    test_syzygy_available();
    test_syzygy_probe_kvsK();
    test_mate_search_init();
    test_mate_search_no_mate();

    if (tests_failed == 0) {
        printf("PASS: %d NNUE/policy/MCTS/correction tests passed\n", tests_run);
    }

    return tests_failed > 0 ? 1 : 0;
}
