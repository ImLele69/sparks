#ifndef CALC_H
#define CALC_H

#include <stdint.h>

#include "gfx.h"

// desktop launcher: every click opens a new calculator in its own task
void calc_launch(void);
void calc_draw_icon(surface_t *s, int32_t x, int32_t y);

#endif
