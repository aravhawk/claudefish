#ifndef CLAUDEFISH_NNUE_H
#define CLAUDEFISH_NNUE_H

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "position.h"

/* NNUE architecture: HalfKP feature set
   Feature transformer: 41024 -> 256 (per perspective, 2 accumulators)
   Hidden layers: 512 -> 32 -> 32 -> 1
   All integer quantized: int16 accumulator, int8 hidden layers */

enum {
    /* HalfKP: (king_square * 10 + piece_type_on_square) mapping
       64 squares for king * 641 features per king square = 41024 total features
       Each non-king piece on the board activates one feature:
         feature_index = king_square * 641 + (piece_color * 6 + piece_type) * 64 + piece_square */
    NNUE_HALFKP_DIM_PER_KING = 641,
    NNUE_HALFKP_INPUT_DIM = 64 * NNUE_HALFKP_DIM_PER_KING, /* = 41024 */

    /* ---- Threat Input Features (Stockfish 18 SFNNv10 inspired) ----
       For each non-king piece, we add a feature if it's attacked by an enemy
       piece and another if it's attacked by an enemy pawn.
       64 squares × 2 threat types × 2 colors = 256 extra features per king square.
       New DIM_PER_KING = 641 + 256 = 897, total = 64 * 897 = 57408 */
    NNUE_THREAT_INPUT_DIM = 256, /* 64 squares × 2 types × 2 colors */
    NNUE_HALFKP_THREAT_DIM_PER_KING = NNUE_HALFKP_DIM_PER_KING + NNUE_THREAT_INPUT_DIM,
    NNUE_TOTAL_INPUT_DIM = 64 * NNUE_HALFKP_THREAT_DIM_PER_KING, /* = 57408 */

    /* Keep the old constant for backward compatibility with weight loading */
    NNUE_INPUT_DIM = NNUE_TOTAL_INPUT_DIM,

    /* Network dimensions */
    NNUE_ACCUMULATOR_DIM = 256,
    NNUE_HIDDEN1_DIM = 32,
    NNUE_HIDDEN2_DIM = 32,

    /* Quantization: feature transformer uses int16 weights/biases
       Hidden layers use int8 weights, int32 biases, with ClippedReLU in [0,127] */
    NNUE_WEIGHT_SCALE_BITS = 6,
    NNUE_ACTIVATION_RANGE = 127,

    /* Accumulator stack depth (max search ply) */
    NNUE_MAX_PLY = 128,

    /* NNUE eval is in centipawns, scale to match classical eval range */
    NNUE_EVAL_SCALE = 16,

    /* Embedded net file magic and version */
    NNUE_MAGIC = 0x4E4E5545, /* "NNUE" */
    NNUE_VERSION = 7
};

typedef struct NNUEAccumulator {
    int16_t values[2][NNUE_ACCUMULATOR_DIM]; /* [white, black] */
    bool computed[2]; /* whether each perspective is up-to-date */
} NNUEAccumulator;

typedef struct NNUEState {
    NNUEAccumulator acc_stack[NNUE_MAX_PLY];
    int current; /* current accumulator index in stack */
    bool initialized;

    /* Dirty piece tracking for incremental updates */
    int dirty_num; /* number of dirty pieces (1-3) */
    struct {
        Piece piece;
        int from; /* NO_SQUARE if piece was added */
        int to;   /* NO_SQUARE if piece was removed */
    } dirty[3];
} NNUEState;

typedef struct NNUEWeights {
    /* Feature transformer: int16 weights and biases
       weights: [NNUE_HALFKP_INPUT_DIM][NNUE_ACCUMULATOR_DIM]
       biases:  [NNUE_ACCUMULATOR_DIM] */
    int16_t *ft_weights; /* allocated, row-major */
    int16_t *ft_biases;

    /* Hidden layer 1: int8 weights, int32 biases
       weights: [NNUE_ACCUMULATOR_DIM * 2][NNUE_HIDDEN1_DIM]
       biases:  [NNUE_HIDDEN1_DIM] */
    int8_t *h1_weights;
    int32_t *h1_biases;

    /* Hidden layer 2: int8 weights, int32 biases
       weights: [NNUE_HIDDEN1_DIM][NNUE_HIDDEN2_DIM]
       biases:  [NNUE_HIDDEN2_DIM] */
    int8_t *h2_weights;
    int32_t *h2_biases;

    /* Output layer: int8 weights, int32 bias
       weights: [NNUE_HIDDEN2_DIM] (single output)
       bias: scalar */
    int32_t out_bias;
} NNUEWeights;

typedef struct NNUENet {
    NNUEWeights weights;
    bool loaded;
    char description[256];
} NNUENet;

/* Initialize NNUE subsystem */
void nnue_init(void);

/* Load network from embedded binary data (no file I/O) */
bool nnue_load_embedded(void);

/* Load network from a file path */
bool nnue_load_from_file(const char *path);

/* Check if a network is loaded and ready */
bool nnue_is_loaded(void);

/* Initialize NNUE state for a position (full recomputation) */
void nnue_state_init(NNUEState *state, const Position *pos);

/* Push accumulator onto stack (call before make_move) */
void nnue_push(NNUEState *state);

/* Pop accumulator from stack (call after unmake_move) */
void nnue_pop(NNUEState *state);

/* Update accumulator incrementally for a single piece move:
   piece moved from->to, or added/removed.
   Call after each piece change during make_move. */
void nnue_update_accumulator_incremental(
    NNUEState *state,
    const Position *pos,
    Piece piece,
    int from,
    int to
);

/* Recompute accumulator from scratch (fallback) */
void nnue_refresh_accumulators(NNUEState *state, const Position *pos);

/* Full NNUE evaluation: returns score in centipawns from side-to-move perspective */
int nnue_evaluate(NNUEState *state, const Position *pos);

/* Get NNUE state for external use (e.g., per-search-context) */
NNUEState *nnue_create_state(void);
void nnue_destroy_state(NNUEState *state);

/* Cleanup */
void nnue_cleanup(void);

#endif
