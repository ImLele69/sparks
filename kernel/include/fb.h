#ifndef FB_H
#define FB_H

#include <stdint.h>

#include "gfx.h"

// 0 if GRUB didn't give us a 32bpp RGB framebuffer
int fb_init(uint32_t mb_info_addr);
int fb_available(void);
uint32_t fb_width(void);
uint32_t fb_height(void);

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

// back buffer if we have one, the real framebuffer otherwise. nothing shows up
// on screen until fb_mark_dirty + fb_flush
surface_t *fb_surface(void);
int fb_double_buffered(void);
void fb_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void fb_flush(void);
// pointer appears on the first call, and only with a back buffer
void fb_cursor_move(uint32_t x, uint32_t y);

#endif
