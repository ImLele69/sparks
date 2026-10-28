#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

#include "gfx.h"

typedef void (*console_mark_fn)(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
typedef void (*console_present_fn)(void);
typedef char (*console_input_fn)(void);

// call after fb_init, falls back to VGA text mode if there's no framebuffer
void console_init(void);

// redirect output to another surface and start from a clean screen. mark gets
// every redrawn cell, present runs after each write. both can be NULL
void console_attach(surface_t *surface, console_mark_fn mark, console_present_fn present);
void console_detach(void);

// where console_getchar reads from, the keyboard if unset
void console_set_input(console_input_fn fn);
char console_getchar(void);

void console_clear(void);
void console_putc(char c);
void console_cursor_left(void);
void console_cursor_right(void);
void console_write(const char *str);
void console_write_hex(uint64_t val);
void console_write_dec(uint64_t val);

#endif
