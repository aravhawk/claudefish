#ifndef CLAUDEFISH_MATE_SEARCH_H
#define CLAUDEFISH_MATE_SEARCH_H

#include <stdbool.h>
#include <stdint.h>
#include "types.h"

struct Position;

enum {
    MATESEARCH_MAX_NODES = 2000,
    MATESEARCH_MAX_PLY = 64,
    MATESEARCH_MAX_CHILDREN = 32, /* max children per node */
    MATESEARCH_INFINITE = 1000000
};

typedef enum PNValue {
    PN_PROVEN = 0,
    PN_DISPROVEN = MATESEARCH_INFINITE,
    PN_UNKNOWN = 1
} PNValue;

typedef struct PNNodes {
    int proof;
    int disproof;
    bool expanded;
    bool is_and_node; /* AND = opponent to move, OR = our turn */
    int num_children;
    int children_indices[MATESEARCH_MAX_CHILDREN]; /* indices within node array */
} PNNodes;

typedef struct MateSearchResult {
    bool mate_found;
    int mate_distance; /* plies to mate, or -1 if no mate */
    bool disproven;    /* we can prove no forced mate exists within budget */
} MateSearchResult;

typedef struct MateSearchContext {
    PNNodes nodes[MATESEARCH_MAX_NODES];
    int node_count;
    int node_limit;
} MateSearchContext;

void mate_search_init(MateSearchContext *ctx, int node_limit);
MateSearchResult mate_search(MateSearchContext *ctx, struct Position *pos, int max_depth);

#endif
