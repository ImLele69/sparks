#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

// one byte per row, bit 7 is the leftmost pixel
extern const uint8_t font_8x16[256][FONT_HEIGHT];

#endif
