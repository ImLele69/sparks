#ifndef WM_H
#define WM_H

#include <stdint.h>

#include "gfx.h"

#define WM_EVENT_MOUSE_DOWN 1
#define WM_EVENT_MOUSE_UP 2
#define WM_EVENT_KEY 3
#define WM_EVENT_CLOSE 4

typedef struct {
    int type;
    int32_t x; // client area coords, can be outside it on MOUSE_UP
    int32_t y;
    char key;
} wm_event_t;

typedef struct window window_t;

typedef void (*wm_icon_fn)(surface_t *s, int32_t x, int32_t y);
typedef void (*wm_launch_fn)(void);

// 0 without a double buffered framebuffer, we stay on the plain console
int wm_init(void);

// icons are WM_ICON_SIZE squares. launch runs on the wm task, so it may start
// something or post a request but must never block
#define WM_ICON_SIZE 56
void wm_add_launcher(const char *label, wm_icon_fn icon, wm_launch_fn launch);

// callable from any task. shows up on the next frame, on top. NULL if the
// pixel buffer doesn't fit in the heap
window_t *wm_create_window(const char *title, uint32_t width, uint32_t height);
surface_t *wm_window_surface(window_t *win);
// surface changed, recompose on the next frame
void wm_invalidate(window_t *win);
void wm_focus(window_t *win);
// freed by the wm on the next frame, don't touch win after this
void wm_destroy_window(window_t *win);
// blocks until the window gets an event
void wm_wait_event(window_t *win, wm_event_t *ev);

#endif
