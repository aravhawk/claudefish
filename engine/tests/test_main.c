#include <stdio.h>

#include "test_suites.h"

int main(void) {
    int failed = 0;

    failed += test_board_run();
    failed += test_movegen_run();
    failed += test_legality_run();
    failed += test_evaluate_run();
    failed += test_perft_run();
    failed += test_search_run();
    failed += test_engine_api_run();

    if (failed == 0) {
        printf("PASS: all engine tests passed\n");
        return 0;
    }

    fprintf(stderr, "FAIL: %d test suite(s) failed\n", failed);
    return 1;
}
