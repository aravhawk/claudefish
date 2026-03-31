#ifndef CLAUDEFISH_TIME_H
#define CLAUDEFISH_TIME_H

#include <stdbool.h>
#include <stdint.h>

enum {
    TIME_DEFAULT_MOVES_TO_GO = 30,
    TIME_NODE_CHECK_GRANULARITY = 4096
};

typedef struct TimeControl {
    int remaining_ms;
    int increment_ms;
    int move_time_ms;
    int moves_to_go;
} TimeControl;

typedef struct SearchTimer {
    uint64_t start_ms;
    int limit_ms;
} SearchTimer;

uint64_t time_now_ms(void);
int time_calculate_allocation_ms(const TimeControl *control);
void time_start(SearchTimer *timer, int limit_ms);
double time_elapsed_ms(const SearchTimer *timer);
bool time_is_expired(const SearchTimer *timer);

#endif
