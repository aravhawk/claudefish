#ifndef CLAUDEFISH_POLICY_H
#define CLAUDEFISH_POLICY_H

#include <stdint.h>

#include "movegen.h"
#include "nnue.h"
#include "position.h"
#include "types.h"

/* Policy Network: small head on top of NNUE accumulator that predicts
   move probabilities for use in alpha-beta move ordering.
   This is the novel technique - no CCRL engine uses a policy network
   inside alpha-beta search. */

enum {
    /* Policy head dimensions - small enough to be fast, large enough to be useful */
    POLICY_HIDDEN_DIM = 64,
    POLICY_OUTPUT_DIM = 64 * 64, /* from_square * 64 + to_square, max 4096 move slots */

    /* Policy score range - added on top of history scores */
    POLICY_SCORE_SCALE = 50000,

    /* Position classification output dims for adaptive search */
    ADAPT_NUM_CLASSES = 4, /* tactical, positional, endgame, material_imbalance */
    ADAPT_HIDDEN_DIM = 32
};

typedef struct PolicyWeights {
    /* Policy head: takes ClippedReLU'd NNUE accumulator (512 values) -> move priors
       h1: 512 * POLICY_HIDDEN_DIM int8 weights + POLICY_HIDDEN_DIM int32 biases
       out: POLICY_HIDDEN_DIM * POLICY_OUTPUT_DIM int8 weights + POLICY_OUTPUT_DIM int32 biases */
    int8_t *h1_weights;
    int32_t *h1_biases;
    int8_t *out_weights;
    int32_t *out_biases;

    /* Adaptive head: takes ClippedReLU'd accumulator -> position class
       h1: 512 * ADAPT_HIDDEN_DIM int8 weights + ADAPT_HIDDEN_DIM int32 biases
       out: ADAPT_HIDDEN_DIM * ADAPT_NUM_CLASSES int8 weights + ADAPT_NUM_CLASSES int32 biases */
    int8_t *adapt_h1_weights;
    int32_t *adapt_h1_biases;
    int8_t *adapt_out_weights;
    int32_t *adapt_out_biases;
} PolicyWeights;

typedef struct PolicyState {
    bool loaded;
    PolicyWeights weights;
} PolicyState;

/* Initialize policy subsystem */
void policy_init(void);

/* Load policy weights (currently generates placeholder weights) */
bool policy_load_embedded(void);

/* Check if policy network is loaded */
bool policy_is_loaded(void);

/* Get policy score for a move, used in move ordering.
   Returns a score in range [0, POLICY_SCORE_SCALE] to be added to move ordering score.
   Requires the current NNUE accumulator state. */
int policy_score_move(
    const NNUEAccumulator *acc,
    Move move
);

/* Classify position type for adaptive search parameters.
   Returns index in [0, ADAPT_NUM_CLASSES) or -1 on error.
   0 = tactical, 1 = quiet_positional, 2 = endgame, 3 = material_imbalance */
int policy_classify_position(
    const NNUEAccumulator *acc,
    const Position *pos
);

/* Get adaptive search parameter multipliers based on position class.
   Returns multipliers for: lmr_reduction, null_move_r, futility_margin, lmp_threshold */
typedef struct AdaptParams {
    int lmr_multiplier;      /* percentage: 100 = default, 120 = 20% more reduction */
    int null_move_r_delta;  /* additive delta to base null move reduction */
    int futility_margin;    /* percentage: 100 = default */
    int lmp_threshold;      /* percentage: 100 = default */
    int extension_threshold; /* threshold for extending: lower = more extensions */
} AdaptParams;

AdaptParams policy_get_adapt_params(int position_class);

/* Cleanup */
void policy_cleanup(void);

#endif
