#ifndef CLAUDEFISH_BOOK_H
#define CLAUDEFISH_BOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "movegen.h"

typedef struct PolyglotEntry {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
} PolyglotEntry;

void book_init(void);
void book_set_seed(uint32_t seed);
bool book_load_default(void);
bool book_load_file(const char *path);
bool book_load_memory(const unsigned char *data, size_t size);
void book_clear(void);
bool book_is_loaded(void);
size_t book_entry_count(void);
uint64_t book_polyglot_hash(const Position *pos);
bool book_probe_move(Position *pos, Move *out_move);

#endif
