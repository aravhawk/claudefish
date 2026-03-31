#include "time.h"

#include <sys/time.h>

uint64_t time_now_ms(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((uint64_t) tv.tv_sec * 1000ULL) + ((uint64_t) tv.tv_usec / 1000ULL);
}

int time_calculate_allocation_ms(const TimeControl *control) {
    int remaining_ms;
    int increment_ms;
    int moves_to_go;
    int allocation;
    int guard_ms;
    int maximum_allocation;

    if (control == NULL) {
        return 1;
    }

    if (control->move_time_ms > 0) {
        return control->move_time_ms;
    }

    remaining_ms = control->remaining_ms > 0 ? control->remaining_ms : 1;
    increment_ms = control->increment_ms > 0 ? control->increment_ms : 0;
    moves_to_go = control->moves_to_go > 0 ? control->moves_to_go : TIME_DEFAULT_MOVES_TO_GO;

    allocation = (remaining_ms / moves_to_go) + ((increment_ms * 3) / 4);
    if (allocation < 1) {
        allocation = 1;
    }

    guard_ms = remaining_ms / 10;
    if (guard_ms < 50) {
        guard_ms = 50;
    }

    maximum_allocation = remaining_ms - guard_ms;
    if (maximum_allocation < 1) {
        maximum_allocation = remaining_ms;
    }

    if (maximum_allocation < 1) {
        maximum_allocation = 1;
    }

    if (allocation > maximum_allocation) {
        allocation = maximum_allocation;
    }

    return allocation;
}

void time_start(SearchTimer *timer, int limit_ms) {
    if (timer == NULL) {
        return;
    }

    timer->start_ms = time_now_ms();
    timer->limit_ms = limit_ms;
}

double time_elapsed_ms(const SearchTimer *timer) {
    if (timer == NULL) {
        return 0.0;
    }

    return (double) (time_now_ms() - timer->start_ms);
}

bool time_is_expired(const SearchTimer *timer) {
    return timer != NULL && timer->limit_ms > 0 && time_elapsed_ms(timer) >= (double) timer->limit_ms;
}
