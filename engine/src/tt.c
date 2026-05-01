#include "tt.h"

#include <stdlib.h>
#include <string.h>

static TTBucket *tt_table = NULL;
static uint8_t tt_age = 0;
static bool tt_initialized = false;
static size_t tt_allocated_buckets = 0;
static size_t tt_size_mb = TT_DEFAULT_SIZE_MB;

size_t tt_bucket_count_actual = 0;

static uint32_t tt_key(uint64_t hash) {
    return (uint32_t) (hash >> 32);
}

static size_t tt_bucket_index(uint64_t hash) {
    return (size_t) ((hash >> 16) & (tt_bucket_count_actual - 1));
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

static size_t tt_compute_bucket_count(size_t size_mb) {
    size_t total_bytes = size_mb * 1024ULL * 1024ULL;
    size_t bucket_size = sizeof(TTBucket);
    size_t count = total_bytes / bucket_size;

    /* Round down to power of 2 */
    size_t power = 1;
    while (power * 2 <= count) {
        power *= 2;
    }

    return power > 0 ? power : 1;
}

void tt_init(void) {
    if (tt_initialized) {
        return;
    }

    tt_resize(tt_size_mb);
}

void tt_resize(size_t size_mb) {
    size_t bucket_count;

    if (size_mb == 0) {
        size_mb = TT_DEFAULT_SIZE_MB;
    }

    tt_size_mb = size_mb;
    bucket_count = tt_compute_bucket_count(size_mb);

    if (tt_table != NULL) {
        free(tt_table);
        tt_table = NULL;
    }

    tt_table = (TTBucket *) calloc(bucket_count, sizeof(TTBucket));
    if (tt_table == NULL) {
        tt_bucket_count_actual = 0;
        tt_allocated_buckets = 0;
        return;
    }

    tt_allocated_buckets = bucket_count;
    tt_bucket_count_actual = bucket_count;
    tt_age = 0;
    tt_initialized = true;
}

void tt_clear(void) {
    if (tt_table == NULL || tt_allocated_buckets == 0) {
        return;
    }

    memset(tt_table, 0, tt_allocated_buckets * sizeof(TTBucket));
    tt_age = 0;
}

void tt_new_search(void) {
    tt_init();
    if (tt_table == NULL) {
        return;
    }

    tt_age = (uint8_t) (tt_age + 1);
    /* Wrap age to fit in uint8_t (0-255), but keep it valid for comparison */
    if (tt_age == 0) {
        tt_age = 1;
    }
}

bool tt_probe(uint64_t hash, TTEntry *out_entry) {
    TTBucket *bucket;
    uint32_t key;
    int i;

    tt_init();
    if (tt_table == NULL || tt_bucket_count_actual == 0) {
        return false;
    }

    key = tt_key(hash);
    bucket = &tt_table[tt_bucket_index(hash)];

    for (i = 0; i < TT_BUCKET_SIZE; ++i) {
        if (bucket->entries[i].flag != TT_FLAG_NONE && bucket->entries[i].key == key) {
            /* Update age on hit to indicate this entry is still useful */
            bucket->entries[i].age = tt_age;

            if (out_entry != NULL) {
                *out_entry = bucket->entries[i];
            }
            return true;
        }
    }

    return false;
}

Move tt_probe_move(uint64_t hash) {
    TTBucket *bucket;
    uint32_t key;
    int i;

    tt_init();
    if (tt_table == NULL || tt_bucket_count_actual == 0) {
        return 0;
    }

    key = tt_key(hash);
    bucket = &tt_table[tt_bucket_index(hash)];

    for (i = 0; i < TT_BUCKET_SIZE; ++i) {
        if (bucket->entries[i].flag != TT_FLAG_NONE && bucket->entries[i].key == key) {
            return bucket->entries[i].best_move;
        }
    }

    return 0;
}

void tt_store(uint64_t hash, int depth, int score, int static_eval, Move best_move, TTFlag flag, int ply) {
    TTBucket *bucket;
    uint32_t key;
    int stored_score;
    int i;
    int replace_index;

    tt_init();
    if (tt_table == NULL || tt_bucket_count_actual == 0) {
        return;
    }

    key = tt_key(hash);
    bucket = &tt_table[tt_bucket_index(hash)];
    stored_score = tt_score_to_storage(score, ply);

    /* Check if we already have an entry for this position in the bucket */
    for (i = 0; i < TT_BUCKET_SIZE; ++i) {
        if (bucket->entries[i].key == key && bucket->entries[i].flag != TT_FLAG_NONE) {
            /* Replace if new entry has greater or equal depth (preserve best move
               from deeper searches, but allow updating score/flag from same depth) */
            if (depth >= bucket->entries[i].depth) {
                bucket->entries[i].best_move = best_move;
                bucket->entries[i].score = (int16_t) stored_score;
                bucket->entries[i].static_eval = (int16_t) static_eval;
                bucket->entries[i].depth = depth < 0 ? 0 : (depth > 255 ? 255 : (uint8_t) depth);
                bucket->entries[i].flag = (uint8_t) flag;
                bucket->entries[i].age = tt_age;
            } else if (best_move != 0 && bucket->entries[i].best_move == 0) {
                /* Always update best move if the entry has none */
                bucket->entries[i].best_move = best_move;
            }
            return;
        }
    }

    /* Find the best replacement slot: prefer empty, then oldest+shallowest */
    replace_index = 0;
    {
        int worst_score = 0x7FFFFFFF;
        for (i = 0; i < TT_BUCKET_SIZE; ++i) {
            int entry_score;
            int age_diff;

            if (bucket->entries[i].flag == TT_FLAG_NONE) {
                replace_index = i;
                break;
            }

            /* Replacement score: prefer replacing entries that are old and shallow.
               Age difference gives a strong bias toward old entries. */
            age_diff = (int) tt_age - (int) bucket->entries[i].age;
            if (age_diff < 0) age_diff += 256;

            entry_score = bucket->entries[i].depth + (age_diff * 4);

            if (entry_score < worst_score) {
                worst_score = entry_score;
                replace_index = i;
            }
        }
    }

    bucket->entries[replace_index].key = key;
    bucket->entries[replace_index].best_move = best_move;
    bucket->entries[replace_index].score = (int16_t) stored_score;
    bucket->entries[replace_index].static_eval = (int16_t) static_eval;
    bucket->entries[replace_index].depth = depth < 0 ? 0 : (depth > 255 ? 255 : (uint8_t) depth);
    bucket->entries[replace_index].flag = (uint8_t) flag;
    bucket->entries[replace_index].age = tt_age;
    bucket->entries[replace_index].reserved = 0;
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
    return tt_allocated_buckets * TT_BUCKET_SIZE;
}

size_t tt_bucket_count(void) {
    return tt_allocated_buckets;
}

size_t tt_size_bytes(void) {
    return tt_allocated_buckets * sizeof(TTBucket);
}

uint8_t tt_current_age(void) {
    return tt_age;
}

void tt_prefetch(uint64_t hash) {
    if (tt_table != NULL && tt_bucket_count_actual > 0) {
        size_t idx = tt_bucket_index(hash);
#ifdef __GNUC__
        __builtin_prefetch(&tt_table[idx]);
#endif
    }
}
