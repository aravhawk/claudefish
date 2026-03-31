#ifndef CLAUDEFISH_ENGINE_H
#define CLAUDEFISH_ENGINE_H

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

EMSCRIPTEN_KEEPALIVE int init_engine(void);
EMSCRIPTEN_KEEPALIVE int set_position(const char *fen);
EMSCRIPTEN_KEEPALIVE const char *search_best_move(int depth, int time_ms);
EMSCRIPTEN_KEEPALIVE int evaluate_position(void);
EMSCRIPTEN_KEEPALIVE const char *get_legal_moves(void);

#endif
