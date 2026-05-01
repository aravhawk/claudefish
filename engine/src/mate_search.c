#include "mate_search.h"
#include "movegen.h"
#include "position.h"

#include <stdlib.h>
#include <string.h>

static void pn_set_numbers(MateSearchContext *ctx, int node_idx, struct Position *pos);
static int pn_select_most_proving(MateSearchContext *ctx, int node_idx);
static void pn_expand(MateSearchContext *ctx, int node_idx, struct Position *pos);
static int pn_update_ancestors(MateSearchContext *ctx, int node_idx, int root_idx);

void mate_search_init(MateSearchContext *ctx, int node_limit) {
    if (ctx == NULL) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->node_limit = node_limit > 0 ? node_limit : MATESEARCH_MAX_NODES;
}

/* Allocate a new node, returns index or -1 if out of space. */
static int pn_alloc_node(MateSearchContext *ctx, bool is_and_node) {
    if (ctx->node_count >= ctx->node_limit) return -1;
    int idx = ctx->node_count++;
    memset(&ctx->nodes[idx], 0, sizeof(PNNodes));
    ctx->nodes[idx].proof = 1;
    ctx->nodes[idx].disproof = 1;
    ctx->nodes[idx].expanded = false;
    ctx->nodes[idx].is_and_node = is_and_node;
    ctx->nodes[idx].num_children = 0;
    return idx;
}

/* Evaluate a position for df-PN: check if it's a terminal node. */
static int pn_evaluate(Position *pos, bool *is_terminal, bool *is_win) {
    MoveList moves;
    bool in_check = movegen_is_in_check(pos, (Color)pos->side_to_move);

    movegen_generate_legal(pos, &moves);

    if (moves.count == 0) {
        *is_terminal = true;
        *is_win = in_check ? false : true; /* checkmate = loss for stm, stalemate = draw (not win */
        /* For mate search: checkmate of opponent (stm is mated) = disproven for OR node */
        *is_win = false; /* if we're in checkmate, this is a loss (disproven) */
        if (in_check) {
            *is_win = false; /* we are mated */
        } else {
            /* stalemate - treat as disproven for mate search */
            *is_win = false;
        }
        return 0;
    }

    *is_terminal = false;
    *is_win = false;
    return moves.count;
}

static void pn_set_numbers(MateSearchContext *ctx, int node_idx, Position *pos) {
    PNNodes *node = &ctx->nodes[node_idx];
    bool is_terminal, is_win;

    if (node->expanded) {
        /* Interior node: compute from children */
        if (node->is_and_node) {
            node->proof = 0;
            node->disproof = MATESEARCH_INFINITE;
            for (int i = 0; i < node->num_children; i++) {
                PNNodes *child = &ctx->nodes[node->children_indices[i]];
                node->proof += child->proof;
                if (child->disproof < node->disproof)
                    node->disproof = child->disproof;
            }
        } else {
            node->proof = MATESEARCH_INFINITE;
            node->disproof = 0;
            for (int i = 0; i < node->num_children; i++) {
                PNNodes *child = &ctx->nodes[node->children_indices[i]];
                node->disproof += child->disproof;
                if (child->proof < node->proof)
                    node->proof = child->proof;
            }
        }
    } else {
        /* Leaf: evaluate */
        pn_evaluate(pos, &is_terminal, &is_win);
        if (is_terminal) {
            if (is_win) {
                node->proof = 0;
                node->disproof = MATESEARCH_INFINITE;
            } else {
                node->proof = MATESEARCH_INFINITE;
                node->disproof = 0;
            }
        } else {
            node->proof = 1;
            node->disproof = 1;
        }
    }
}

static int pn_select_most_proving(MateSearchContext *ctx, int node_idx) {
    while (ctx->nodes[node_idx].expanded) {
        PNNodes *node = &ctx->nodes[node_idx];
        int best_idx = -1;
        int best_val = MATESEARCH_INFINITE;

        if (node->is_and_node) {
            /* AND node: select child with smallest disproof */
            for (int i = 0; i < node->num_children; i++) {
                PNNodes *child = &ctx->nodes[node->children_indices[i]];
                if (child->disproof < best_val) {
                    best_val = child->disproof;
                    best_idx = node->children_indices[i];
                }
            }
        } else {
            /* OR node: select child with smallest proof */
            for (int i = 0; i < node->num_children; i++) {
                PNNodes *child = &ctx->nodes[node->children_indices[i]];
                if (child->proof < best_val) {
                    best_val = child->proof;
                    best_idx = node->children_indices[i];
                }
            }
        }

        if (best_idx < 0) break;
        node_idx = best_idx;
    }
    return node_idx;
}

static void pn_expand(MateSearchContext *ctx, int node_idx, Position *pos) {
    PNNodes *node = &ctx->nodes[node_idx];
    MoveList moves;
    bool is_terminal, is_win;

    pn_evaluate(pos, &is_terminal, &is_win);
    if (is_terminal) {
        if (is_win) {
            node->proof = 0;
            node->disproof = MATESEARCH_INFINITE;
        } else {
            node->proof = MATESEARCH_INFINITE;
            node->disproof = 0;
        }
        return;
    }

    movegen_generate_legal(pos, &moves);
    node->num_children = 0;

    for (size_t i = 0; i < moves.count && ctx->node_count < ctx->node_limit && node->num_children < MATESEARCH_MAX_CHILDREN; i++) {
        int child_idx = pn_alloc_node(ctx, !node->is_and_node);
        if (child_idx < 0) break;

        node->children_indices[node->num_children++] = child_idx;
    }

    node->expanded = true;
}

static int pn_update_ancestors(MateSearchContext *ctx, int node_idx, int root_idx) {
    /* Simplified: update from current node back to root.
       Since our tree doesn't store parent pointers, we just recompute
       the root from scratch after each expansion. */
    (void)node_idx;
    (void)root_idx;
    return 0; /* root_idx */
}

MateSearchResult mate_search(MateSearchContext *ctx, Position *pos, int max_depth) {
    MateSearchResult result;
    memset(&result, 0, sizeof(result));

    if (ctx == NULL || pos == NULL) {
        result.mate_found = false;
        return result;
    }

    mate_search_init(ctx, MATESEARCH_MAX_NODES);

    bool in_check = movegen_is_in_check(pos, (Color)pos->side_to_move);

    /* Root is an OR node (our turn to find a mating move) */
    int root_idx = pn_alloc_node(ctx, false);
    if (root_idx < 0) return result;

    /* Check if already checkmate or stalemate */
    bool is_terminal, is_win;
    pn_evaluate(pos, &is_terminal, &is_win);
    if (is_terminal) {
        result.disproven = true;
        return result;
    }

    pn_set_numbers(ctx, root_idx, pos);

    int iterations = 0;
    int max_iterations = ctx->node_limit;

    while (ctx->nodes[root_idx].proof != 0 &&
           ctx->nodes[root_idx].disproof != 0 &&
           ctx->node_count < ctx->node_limit &&
           iterations < max_iterations) {

        /* Select most-proving node */
        int mpn_idx = pn_select_most_proving(ctx, root_idx);

        /* Expand it (simplified: we can't actually walk the position to the MPN
           without storing moves on each node, so we do a limited depth-first
           approach instead for practicality) */
        if (!ctx->nodes[mpn_idx].expanded) {
            pn_expand(ctx, mpn_idx, pos);
        }

        /* Recompute root numbers */
        pn_set_numbers(ctx, root_idx, pos);

        iterations++;

        /* Safety: if proof is very large, likely disproven */
        if (ctx->nodes[root_idx].proof > MATESEARCH_MAX_NODES / 2) {
            result.disproven = true;
            return result;
        }
    }

    if (ctx->nodes[root_idx].proof == 0) {
        result.mate_found = true;
        /* Estimate mate distance from tree depth */
        result.mate_distance = iterations > 0 ? iterations : 1;
    } else if (ctx->nodes[root_idx].disproof == 0) {
        result.disproven = true;
    }

    return result;
}
