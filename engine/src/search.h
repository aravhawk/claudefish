#ifndef CLAUDEFISH_SEARCH_H
#define CLAUDEFISH_SEARCH_H

#include <stdbool.h>
#include <stdint.h>

#include "movegen.h"

enum {
    SEARCH_MAX_PLY = 64,
    SEARCH_MAX_DEPTH = 64,
    SEARCH_INF = 32000,
    SEARCH_MATE_SCORE = 30000,
    SEARCH_MATE_BOUND = SEARCH_MATE_SCORE - SEARCH_MAX_PLY,
    SEARCH_DELTA_MARGIN = 50
};

typedef struct SearchIteration {
    int depth;
    int score;
    uint64_t nodes;
    Move best_move;
    int pv_length;
    Move pv[SEARCH_MAX_PLY];
} SearchIteration;

typedef struct SearchResult {
    Move best_move;
    int score;
    int completed_depth;
    int pv_length;
    Move pv[SEARCH_MAX_PLY];
    uint64_t nodes;
    uint64_t qnodes;
    bool stopped;
    double elapsed_ms;
    int iteration_count;
    SearchIteration iterations[SEARCH_MAX_DEPTH];
} SearchResult;

typedef struct SearchOptions {
    bool enable_null_move_pruning;
    bool enable_lmr;
    bool enable_futility_pruning;
    bool enable_razoring;
    bool enable_aspiration_windows;
    bool enable_check_extensions;
} SearchOptions;

void search_init(void);
void search_reset_heuristics(void);
SearchOptions search_get_options(void);
void search_set_options(const SearchOptions *options);
void search_reset_options(void);
bool search_iterative_deepening(Position *pos, int max_depth, int time_limit_ms, SearchResult *out_result);
int search_extract_pv(Position *pos, int max_depth, Move *pv_out, int pv_capacity);
bool search_move_to_uci(Move move, char buffer[6]);
bool search_is_mate_score(int score);
int search_mate_distance(int score);

#endif
