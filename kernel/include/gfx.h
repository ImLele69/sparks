#ifndef GFX_H
#define GFX_H

#include <stdint.h>

// pitch is in pixels, not bytes. everything is 32bpp
typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} surface_t;

// as a text background: leave whatever is underneath. none of the 32bpp modes
// GRUB hands out use the top byte, so it can't clash with a real color
#define GFX_TRANSPARENT 0xFFFFFFFFu

// signed coordinates so a window dragged half off screen just gets clipped
void gfx_fill_rect(surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_rect(surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void gfx_draw_char(surface_t *s, int32_t x, int32_t y, char c, uint32_t scale, uint32_t fg, uint32_t bg);
void gfx_draw_text(surface_t *s, int32_t x, int32_t y, const char *text, uint32_t scale, uint32_t fg, uint32_t bg);
int32_t gfx_text_width(const char *text, uint32_t scale);
void gfx_blit(surface_t *dst, int32_t x, int32_t y, const surface_t *src);

#endif
