#include "tt.h"

#include <string.h>

static TTEntry tt_table[TT_ENTRY_COUNT];
static uint8_t tt_age = 0;
static bool tt_initialized = false;

static uint32_t tt_key(uint64_t hash) {
    return (uint32_t) (hash >> 32);
}

static size_t tt_index(uint64_t hash) {
    return (size_t) (hash & (TT_ENTRY_COUNT - 1));
}

static int tt_score_to_storage(int score, int ply) {
    if (score > TT_MATE_BOUND) {
        return score + ply;
    }

    if (score < -TT_MATE_BOUND) {
        return score - ply;
    }

    return score;
}

void tt_init(void) {
    if (tt_initialized) {
        return;
    }

    tt_clear();
    tt_initialized = true;
}

void tt_clear(void) {
    memset(tt_table, 0, sizeof(tt_table));
    tt_age = 0;
}

void tt_new_search(void) {
    tt_init();
    tt_age = (uint8_t) (tt_age + 1);
}

bool tt_probe(uint64_t hash, TTEntry *out_entry) {
    TTEntry *entry;

    tt_init();
    entry = &tt_table[tt_index(hash)];

    if (entry->flag == TT_FLAG_NONE || entry->key != tt_key(hash)) {
        return false;
    }

    if (out_entry != NULL) {
        *out_entry = *entry;
    }

    return true;
}

Move tt_probe_move(uint64_t hash) {
    TTEntry entry;

    if (!tt_probe(hash, &entry)) {
        return 0;
    }

    return entry.best_move;
}

void tt_store(uint64_t hash, int depth, int score, int static_eval, Move best_move, TTFlag flag, int ply) {
    TTEntry *entry;
    int stored_score;

    tt_init();
    entry = &tt_table[tt_index(hash)];
    stored_score = tt_score_to_storage(score, ply);

    entry->key = tt_key(hash);
    entry->best_move = best_move;
    entry->score = (int16_t) stored_score;
    entry->static_eval = (int16_t) static_eval;
    entry->depth = depth < 0 ? 0 : (depth > 255 ? 255 : (uint8_t) depth);
    entry->flag = (uint8_t) flag;
    entry->age = tt_age;
    entry->reserved = 0;
}

int tt_score_from_entry(const TTEntry *entry, int ply) {
    int score;

    if (entry == NULL) {
        return 0;
    }

    score = entry->score;

    if (score > TT_MATE_BOUND) {
        return score - ply;
    }

    if (score < -TT_MATE_BOUND) {
        return score + ply;
    }

    return score;
}

size_t tt_entry_count(void) {
    return TT_ENTRY_COUNT;
}

uint8_t tt_current_age(void) {
    return tt_age;
}
