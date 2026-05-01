#ifndef CLAUDEFISH_SYZYGY_H
#define CLAUDEFISH_SYZYGY_H

#include <stdbool.h>
#include <stdint.h>
#include "types.h"

struct Position;

enum {
    SYZYGY_MAX_PIECES = 5,    /* Currently support up to 5-piece tables */
    SYZYGY_RESULT_LOSS = -1,
    SYZYGY_RESULT_DRAW = 0,
    SYZYGY_RESULT_WIN = 1,
    SYZYGY_RESULT_NONE = 2    /* No tablebase hit */
};

typedef struct SyzygyResult {
    int wdl;              /* Win/Draw/Loss: -1, 0, or 1 from side-to-move perspective */
    int dtz;              /* Distance to zeroing (under 50-move rule) */
    bool found;           /* Whether a tablebase hit occurred */
    bool uses_rule50;     /* Whether the 50-move rule affects the result */
} SyzygyResult;

void syzygy_init(void);
void syzygy_cleanup(void);

/* Probe the tablebases for the given position. */
SyzygyResult syzygy_probe(const struct Position *pos);

/* Check if the current position has a tablebase available (piece count ≤ SYZYGY_MAX_PIECES). */
bool syzygy_available(const struct Position *pos);

/* Count total pieces on the board. */
int syzygy_piece_count(const struct Position *pos);

/* Load embedded 3-4 piece tablebases from static data. */
bool syzygy_load_embedded(void);

/* Load tablebase files from a directory path. */
bool syzygy_load_path(const char *path);

/* Check if any tablebases are loaded. */
bool syzygy_is_loaded(void);

#endif
