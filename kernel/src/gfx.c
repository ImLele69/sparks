#include <stdint.h>

#include "font.h"
#include "gfx.h"
#include "kstring.h"

// 0 if nothing is left after clipping
static int clip(const surface_t *s, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > (int32_t)s->width) {
        *w = (int32_t)s->width - *x;
    }
    if (*y + *h > (int32_t)s->height) {
        *h = (int32_t)s->height - *y;
    }
    return *w > 0 && *h > 0;
}

void gfx_fill_rect(surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!clip(s, &x, &y, &w, &h)) {
        return;
    }

    for (int32_t row = 0; row < h; row++) {
        uint32_t *line = s->pixels + (uint64_t)(y + row) * s->pitch + x;
        for (int32_t col = 0; col < w; col++) {
            line[col] = color;
        }
    }
}

void gfx_draw_rect(surface_t *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    gfx_fill_rect(s, x, y, w, 1, color);
    gfx_fill_rect(s, x, y + h - 1, w, 1, color);
    gfx_fill_rect(s, x, y, 1, h, color);
    gfx_fill_rect(s, x + w - 1, y, 1, h, color);
}

void gfx_draw_char(surface_t *s, int32_t x, int32_t y, char c, uint32_t scale, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font_8x16[(uint8_t)c];

    for (int32_t row = 0; row < FONT_HEIGHT * (int32_t)scale; row++) {
        int32_t py = y + row;
        if (py < 0 || py >= (int32_t)s->height) {
            continue;
        }

        uint8_t bits = glyph[row / scale];
        uint32_t *line = s->pixels + (uint64_t)py * s->pitch;

        for (int32_t col = 0; col < FONT_WIDTH * (int32_t)scale; col++) {
            int32_t px = x + col;
            if (px < 0 || px >= (int32_t)s->width) {
                continue;
            }

            uint32_t color = (bits & (0x80 >> (col / scale))) ? fg : bg;
            if (color != GFX_TRANSPARENT) {
                line[px] = color;
            }
        }
    }
}

void gfx_draw_text(surface_t *s, int32_t x, int32_t y, const char *text, uint32_t scale, uint32_t fg, uint32_t bg) {
    for (int32_t i = 0; text[i] != '\0'; i++) {
        gfx_draw_char(s, x + i * FONT_WIDTH * (int32_t)scale, y, text[i], scale, fg, bg);
    }
}

int32_t gfx_text_width(const char *text, uint32_t scale) {
    return (int32_t)kstrlen(text) * FONT_WIDTH * (int32_t)scale;
}

void gfx_blit(surface_t *dst, int32_t x, int32_t y, const surface_t *src) {
    int32_t w = (int32_t)src->width;
    int32_t h = (int32_t)src->height;
    int32_t src_x = x < 0 ? -x : 0;
    int32_t src_y = y < 0 ? -y : 0;

    if (!clip(dst, &x, &y, &w, &h)) {
        return;
    }

    for (int32_t row = 0; row < h; row++) {
        uint32_t *d = dst->pixels + (uint64_t)(y + row) * dst->pitch + x;
        const uint32_t *sp = src->pixels + (uint64_t)(src_y + row) * src->pitch + src_x;
        uint64_t count = (uint64_t)w;
        __asm__ volatile("rep movsl" : "+D"(d), "+S"(sp), "+c"(count) : : "memory");
    }
}
