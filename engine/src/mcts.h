#ifndef CLAUDEFISH_MCTS_H
#define CLAUDEFISH_MCTS_H

#include <stdbool.h>
#include <stdint.h>

#include "movegen.h"
#include "nnue.h"
#include "position.h"
#include "types.h"

/* Mini-MCTS verification search for critical nodes.
   When the alpha-beta search detects a critical position (large eval swing,
   many captures, etc.), a short MCTS search using the policy network
   can verify or refute the alpha-beta result.
   This is novel: no engine combines MCTS verification within alpha-beta. */

enum {
    MCTS_MAX_NODES = 512,
    MCTS_MAX_CHILDREN = MOVEGEN_MAX_MOVES,
    MCTS_SIMULATION_DEPTH = 4,
    MCTS_CPUCT_NUMERATOR = 3, /* 1.5 = 3/2, using integer math */
    MCTS_CPUCT_DENOMINATOR = 2,
    MCTS_EVAL_THRESHOLD = 150, /* eval swing threshold to trigger MCTS */
    MCTS_DEFAULT_SIMULATIONS = 100
};

typedef struct MCTSNode {
    Move move;
    int visit_count;
    double value_sum;     /* sum of all backpropagated values */
    double prior;          /* policy prior probability */
    int16_t nnue_eval;     /* cached NNUE eval at this node */
    bool expanded;
    bool terminal;         /* terminal node (checkmate/stalemate) */
    int num_children;
    int16_t children_start; /* index into children array */
} MCTSNode;

typedef struct MCTSResult {
    Move best_move;
    double best_value;      /* in [-1, 1] from root player's perspective */
    int simulations_run;
    bool disagrees;         /* true if MCTS disagrees with alpha-beta */
    int mcts_eval;          /* MCTS evaluation in centipawns */
} MCTSResult;

typedef struct MCTSContext {
    MCTSNode nodes[MCTS_MAX_NODES];
    Move children_buf[MCTS_MAX_NODES * MCTS_MAX_CHILDREN]; /* child move indices */
    int num_nodes;
    int root_player;       /* WHITE or BLACK: the player who initiated the search */
    NNUEState nnue_state;  /* per-MCTS NNUE state */
} MCTSContext;

/* Initialize MCTS subsystem */
void mcts_init(void);

/* Run MCTS verification on a critical position.
   Returns the MCTS result including best move and whether it disagrees with alpha-beta.
   simulations: number of MCTS simulations to run (0 = use default)
   ab_best_move: the move alpha-beta thinks is best (to check disagreement)
   ab_eval: the alpha-beta evaluation in centipawns */
MCTSResult mcts_verify(
    Position *pos,
    NNUEState *parent_nnue_state,
    int simulations,
    Move ab_best_move,
    int ab_eval
);

/* Check if a position is "critical" enough to warrant MCTS verification.
   Criteria: large eval swing between moves, many captures, in-check, etc. */
bool mcts_is_critical_position(
    const Position *pos,
    int best_move_eval,
    int second_move_eval,
    int depth
);

/* Cleanup */
void mcts_cleanup(void);

#endif
