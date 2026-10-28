#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

#include "gfx.h"

// called from the shell task: opens the window and moves console + input there
void terminal_init(void);
// desktop launcher: reopen if closed, raise otherwise
void terminal_launch(void);
void terminal_draw_icon(surface_t *s, int32_t x, int32_t y);

#endif
