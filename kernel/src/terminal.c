#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "fb.h"
#include "font.h"
#include "gfx.h"
#include "keyboard.h"
#include "sync.h"
#include "task.h"
#include "terminal.h"
#include "wm.h"

#define TERM_COLS 80
#define TERM_ROWS 25

// the window belongs to the shell task. closing it doesn't kill the shell, it
// just blocks until the launcher reopens it
static window_t *term = NULL;
static uint32_t shell_pid;
static volatile int open_requested = 0;

static void term_present(void) {
    if (term) {
        wm_invalidate(term);
    }
}

static void open_window(void) {
    term = wm_create_window("Terminal", TERM_COLS * FONT_WIDTH, TERM_ROWS * FONT_HEIGHT);
    if (term) {
        console_attach(wm_window_surface(term), NULL, term_present);
    }
}

// clear term before asking for destruction: terminal_launch runs on the wm task
// and must never see a pointer the wm may already have freed
static void close_window(void) {
    window_t *win = term;

    term = NULL;
    console_detach();
    wm_destroy_window(win);
}

static char terminal_getchar(void) {
    for (;;) {
        if (term == NULL) {
            uint64_t flags = irq_save();
            if (!open_requested) {
                task_block();
                irq_restore(flags);
                continue;
            }
            open_requested = 0;
            irq_restore(flags);

            open_window();
            if (term) {
                // fresh screen, make the shell drop its line and reprint the prompt
                return KEY_CANCEL;
            }
            continue;
        }

        wm_event_t ev;
        wm_wait_event(term, &ev);

        if (ev.type == WM_EVENT_KEY) {
            return ev.key;
        }
        if (ev.type == WM_EVENT_CLOSE) {
            close_window();
        }
    }
}

void terminal_init(void) {
    shell_pid = task_current_pid();
    console_set_input(terminal_getchar);
    open_window();
}

void terminal_launch(void) {
    window_t *win = term;

    if (win) {
        wm_focus(win);
        return;
    }

    open_requested = 1;
    task_wake(shell_pid);
}

void terminal_draw_icon(surface_t *s, int32_t x, int32_t y) {
    gfx_fill_rect(s, x, y, WM_ICON_SIZE, WM_ICON_SIZE, fb_rgb(0x9C, 0xA3, 0xAF));
    gfx_fill_rect(s, x + 2, y + 2, WM_ICON_SIZE - 4, WM_ICON_SIZE - 4, fb_rgb(0x11, 0x18, 0x27));
    gfx_fill_rect(s, x + 2, y + 2, WM_ICON_SIZE - 4, 8, fb_rgb(0x37, 0x41, 0x51));
    gfx_draw_text(s, x + 8, y + 22, ">_", 2, fb_rgb(0x22, 0xC5, 0x5E), GFX_TRANSPARENT);
}
