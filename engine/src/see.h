#ifndef CLAUDEFISH_SEE_H
#define CLAUDEFISH_SEE_H

#include "movegen.h"

int see_evaluate(const Position *pos, Move move);
bool see_is_capture_bad(const Position *pos, Move move, int threshold);

#endif
