#ifndef CLAUDEFISH_DRAW_H
#define CLAUDEFISH_DRAW_H

#include "position.h"

bool draw_is_threefold_repetition(const Position *pos);
bool draw_is_fifty_move_rule(const Position *pos);
bool draw_has_insufficient_material(const Position *pos);
bool draw_is_draw(const Position *pos);
int draw_score(const Position *pos);

#endif
