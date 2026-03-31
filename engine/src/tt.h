#ifndef CLAUDEFISH_TT_H
#define CLAUDEFISH_TT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "movegen.h"

enum {
    TT_ENTRY_COUNT = 1u << 20,
    TT_MATE_SCORE = 30000,
    TT_MATE_BOUND = TT_MATE_SCORE - 512
};

typedef enum TTFlag {
    TT_FLAG_NONE = 0,
    TT_FLAG_EXACT = 1,
    TT_FLAG_ALPHA = 2,
    TT_FLAG_BETA = 3
} TTFlag;

typedef struct TTEntry {
    uint32_t key;
    Move best_move;
    int16_t score;
    int16_t static_eval;
    uint8_t depth;
    uint8_t flag;
    uint8_t age;
    uint8_t reserved;
} TTEntry;

void tt_init(void);
void tt_clear(void);
void tt_new_search(void);
bool tt_probe(uint64_t hash, TTEntry *out_entry);
Move tt_probe_move(uint64_t hash);
void tt_store(uint64_t hash, int depth, int score, int static_eval, Move best_move, TTFlag flag, int ply);
int tt_score_from_entry(const TTEntry *entry, int ply);
size_t tt_entry_count(void);
uint8_t tt_current_age(void);

#endif
