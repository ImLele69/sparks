#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#define MOUSE_LEFT 0x01
#define MOUSE_RIGHT 0x02
#define MOUSE_MIDDLE 0x04

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t buttons; // state after the change
} mouse_event_t;

int mouse_init(void);
// returns 0 when no button changes are queued
int mouse_poll_event(mouse_event_t *ev);
int32_t mouse_x(void);
int32_t mouse_y(void);
uint8_t mouse_buttons(void);

#endif
