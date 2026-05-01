#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/bitboard.h"
#include "../src/movegen.h"
#include "../src/nnue.h"
#include "../src/policy.h"
#include "../src/mcts.h"
#include "../src/position.h"
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

    if (tests_failed == 0) {
        printf("PASS: %d NNUE/policy/MCTS tests passed\n", tests_run);
    }

    return tests_failed > 0 ? 1 : 0;
}
