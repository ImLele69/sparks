#include <stddef.h>
#include <stdint.h>

#include "fb.h"
#include "font.h"
#include "gfx.h"
#include "heap.h"
#include "keyboard.h"
#include "kstring.h"
#include "mouse.h"
#include "pit.h"
#include "serial.h"
#include "sync.h"
#include "task.h"
#include "wm.h"

#define MAX_WINDOWS 16
#define MAX_LAUNCHERS 8
#define EVENT_QUEUE_SIZE 32
#define TITLE_MAX 32

#define TITLE_H 24
#define BORDER 1
#define CLOSE_SIZE 18
#define SHADOW 4
#define TASKBAR_H 32
#define TASKBAR_BUTTON_X 112
#define TASKBAR_BUTTON_W 160
#define ICON_CELL_W 96
#define ICON_CELL_H 96
// new windows open right of the icon column
#define DESKTOP_LEFT (16 + ICON_CELL_W + 16)
// how much of a window must stay on screen while dragging
#define KEEP_VISIBLE 48

#define FRAME_MS 15

struct window {
    int32_t x; // client area origin, screen coords
    int32_t y;
    char title[TITLE_MAX];
    surface_t surface;

    wm_event_t events[EVENT_QUEUE_SIZE];
    int event_head;
    int event_tail;
    int64_t waiter_pid;

    uint32_t owner_pid;
    // set by app tasks, consumed by the wm each frame
    volatile int dirty;
    volatile int closing;
    volatile int focus_request;
};

typedef struct {
    char label[TITLE_MAX];
    wm_icon_fn icon;
    wm_launch_fn launch;
} launcher_t;

// only the wm task touches any of this, except pending, which is how new
// windows get in and is only used with irqs off. no locks needed anywhere
static window_t *zorder[MAX_WINDOWS]; // 0 is the bottom
// same windows in creation order, so taskbar buttons don't shuffle on focus
static window_t *by_age[MAX_WINDOWS];
static int window_count = 0;
static window_t *pending[MAX_WINDOWS];
static int pending_count = 0;
static window_t *dragging = NULL;
static int32_t drag_dx, drag_dy;
static window_t *pressed = NULL;
static int need_redraw = 1;
static int cascade = 0;
static uint64_t last_second = (uint64_t)-1;
static int32_t last_mouse_x, last_mouse_y;

static launcher_t launchers[MAX_LAUNCHERS];
static int launcher_count = 0;

static uint32_t screen_w, screen_h;

static uint32_t col_watermark;
static uint32_t col_shadow, col_border;
static uint32_t col_title_on, col_title_off, col_title_text;
static uint32_t col_close_on, col_close_off;
static uint32_t col_bar, col_bar_line, col_bar_button, col_bar_button_on, col_bar_text, col_spark;
static uint32_t col_label, col_label_shadow;

static window_t *top_window(void) {
    return window_count > 0 ? zorder[window_count - 1] : NULL;
}

static void push_event(window_t *win, int type, int32_t x, int32_t y, char key) {
    uint64_t flags = irq_save();

    int next = (win->event_tail + 1) % EVENT_QUEUE_SIZE;
    if (next != win->event_head) {
        wm_event_t *ev = &win->events[win->event_tail];
        ev->type = type;
        ev->x = x;
        ev->y = y;
        ev->key = key;
        win->event_tail = next;
    }
    if (win->waiter_pid >= 0) {
        task_wake((uint32_t)win->waiter_pid);
    }

    irq_restore(flags);
}

static void raise_window(window_t *win) {
    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (zorder[i] == win) {
            idx = i;
        }
    }
    if (idx < 0 || idx == window_count - 1) {
        return;
    }

    for (int i = idx; i < window_count - 1; i++) {
        zorder[i] = zorder[i + 1];
    }
    zorder[window_count - 1] = win;
    need_redraw = 1;
}

static void remove_from(window_t **list, window_t *win) {
    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (list[i] == win) {
            idx = i;
        }
    }
    if (idx < 0) {
        return;
    }
    for (int i = idx; i < window_count - 1; i++) {
        list[i] = list[i + 1];
    }
}

static void free_window(window_t *win) {
    remove_from(zorder, win);
    remove_from(by_age, win);
    window_count--;

    if (dragging == win) {
        dragging = NULL;
    }
    if (pressed == win) {
        pressed = NULL;
    }

    kfree(win->surface.pixels);
    kfree(win);
    need_redraw = 1;
}

static void clamp_position(window_t *win) {
    int32_t w = (int32_t)win->surface.width;
    int32_t min_x = KEEP_VISIBLE - w;
    int32_t max_x = (int32_t)screen_w - KEEP_VISIBLE;
    int32_t min_y = TITLE_H + BORDER;
    int32_t max_y = (int32_t)screen_h - TASKBAR_H;

    if (win->x < min_x) win->x = min_x;
    if (win->x > max_x) win->x = max_x;
    if (win->y < min_y) win->y = min_y;
    if (win->y > max_y) win->y = max_y;
}

static void adopt_pending(void) {
    uint64_t flags = irq_save();
    window_t *incoming[MAX_WINDOWS];
    int count = pending_count;
    for (int i = 0; i < count; i++) {
        incoming[i] = pending[i];
    }
    pending_count = 0;
    irq_restore(flags);

    for (int i = 0; i < count; i++) {
        window_t *win = incoming[i];

        // cascade around the center, 32px per window
        int32_t w = (int32_t)win->surface.width;
        int32_t h = (int32_t)win->surface.height;
        win->x = ((int32_t)screen_w - w) / 2 - 96 + (cascade % 6) * 32;
        win->y = ((int32_t)screen_h - TASKBAR_H - h) / 2 - 64 + (cascade % 6) * 32;
        cascade++;
        if (win->x - BORDER < DESKTOP_LEFT) {
            win->x = DESKTOP_LEFT + BORDER;
        }
        clamp_position(win);

        zorder[window_count] = win;
        by_age[window_count] = win;
        window_count++;
        need_redraw = 1;
    }
}

static void reap_windows(void) {
    for (int i = window_count - 1; i >= 0; i--) {
        window_t *win = zorder[i];
        if (win->closing || !task_alive(win->owner_pid)) {
            free_window(win);
        }
    }
}

static int point_in(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void frame_rect(window_t *win, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    *x = win->x - BORDER;
    *y = win->y - TITLE_H - BORDER;
    *w = (int32_t)win->surface.width + 2 * BORDER;
    *h = (int32_t)win->surface.height + TITLE_H + 2 * BORDER;
}

static void close_rect(window_t *win, int32_t *x, int32_t *y) {
    *x = win->x + (int32_t)win->surface.width - CLOSE_SIZE - 3;
    *y = win->y - TITLE_H + (TITLE_H - CLOSE_SIZE) / 2;
}

static void launcher_cell(int i, int32_t *x, int32_t *y) {
    *x = 16;
    *y = 16 + i * (ICON_CELL_H + 8);
}

static void taskbar_button(int i, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    *x = TASKBAR_BUTTON_X + i * (TASKBAR_BUTTON_W + 6);
    *y = (int32_t)screen_h - TASKBAR_H + 4;
    *w = TASKBAR_BUTTON_W;
    *h = TASKBAR_H - 8;
}

static void handle_press(int32_t mx, int32_t my) {
    if (my >= (int32_t)screen_h - TASKBAR_H) {
        for (int i = 0; i < window_count; i++) {
            int32_t x, y, w, h;
            taskbar_button(i, &x, &y, &w, &h);
            if (point_in(mx, my, x, y, w, h)) {
                raise_window(by_age[i]);
                return;
            }
        }
        return;
    }

    for (int i = window_count - 1; i >= 0; i--) {
        window_t *win = zorder[i];
        int32_t fx, fy, fw, fh;
        frame_rect(win, &fx, &fy, &fw, &fh);
        if (!point_in(mx, my, fx, fy, fw, fh)) {
            continue;
        }

        raise_window(win);

        int32_t cx, cy;
        close_rect(win, &cx, &cy);
        if (point_in(mx, my, cx, cy, CLOSE_SIZE, CLOSE_SIZE)) {
            push_event(win, WM_EVENT_CLOSE, 0, 0, 0);
        } else if (my < win->y) {
            dragging = win;
            drag_dx = mx - win->x;
            drag_dy = my - win->y;
        } else {
            pressed = win;
            push_event(win, WM_EVENT_MOUSE_DOWN, mx - win->x, my - win->y, 0);
        }
        return;
    }

    for (int i = 0; i < launcher_count; i++) {
        int32_t x, y;
        launcher_cell(i, &x, &y);
        if (point_in(mx, my, x, y, ICON_CELL_W, ICON_CELL_H)) {
            launchers[i].launch();
            return;
        }
    }
}

static void handle_release(int32_t mx, int32_t my) {
    dragging = NULL;
    if (pressed) {
        push_event(pressed, WM_EVENT_MOUSE_UP, mx - pressed->x, my - pressed->y, 0);
        pressed = NULL;
    }
}

static void process_input(void) {
    mouse_event_t ev;
    static uint8_t buttons = 0;

    while (mouse_poll_event(&ev)) {
        int was_down = buttons & MOUSE_LEFT;
        int is_down = ev.buttons & MOUSE_LEFT;
        buttons = ev.buttons;

        if (is_down && !was_down) {
            handle_press(ev.x, ev.y);
        } else if (!is_down && was_down) {
            handle_release(ev.x, ev.y);
        }
    }

    int32_t mx = mouse_x();
    int32_t my = mouse_y();
    if (dragging && (mx != last_mouse_x || my != last_mouse_y)) {
        dragging->x = mx - drag_dx;
        dragging->y = my - drag_dy;
        clamp_position(dragging);
        need_redraw = 1;
    }
    last_mouse_x = mx;
    last_mouse_y = my;

    window_t *top = top_window();
    while (keyboard_has_char()) {
        char c = keyboard_getchar();
        if (top) {
            push_event(top, WM_EVENT_KEY, 0, 0, c);
        }
    }

    for (int i = 0; i < window_count; i++) {
        if (zorder[i]->focus_request) {
            zorder[i]->focus_request = 0;
            raise_window(zorder[i]);
        }
    }
}

static uint32_t blend(uint32_t a_r, uint32_t a_g, uint32_t a_b, uint32_t b_r, uint32_t b_g, uint32_t b_b,
                      uint32_t num, uint32_t den) {
    return fb_rgb((uint8_t)(a_r + (b_r - a_r) * num / den), (uint8_t)(a_g + (b_g - a_g) * num / den),
                  (uint8_t)(a_b + (b_b - a_b) * num / den));
}

static void draw_desktop(surface_t *s) {
    uint32_t desk_h = screen_h - TASKBAR_H;

    // integer blend per row, no FPU in here
    for (uint32_t y = 0; y < desk_h; y++) {
        uint32_t color = blend(0x1E, 0x3A, 0x5F, 0x0B, 0x16, 0x22, y, desk_h);
        gfx_fill_rect(s, 0, (int32_t)y, (int32_t)screen_w, 1, color);
    }

    const char *mark = "Sparks";
    int32_t mark_w = gfx_text_width(mark, 4);
    gfx_draw_text(s, (int32_t)screen_w - mark_w - 24, (int32_t)desk_h - 16 * 4 - 16, mark, 4, col_watermark,
                  GFX_TRANSPARENT);

    for (int i = 0; i < launcher_count; i++) {
        int32_t x, y;
        launcher_cell(i, &x, &y);
        launchers[i].icon(s, x + (ICON_CELL_W - WM_ICON_SIZE) / 2, y + 4);

        int32_t lw = gfx_text_width(launchers[i].label, 1);
        int32_t lx = x + (ICON_CELL_W - lw) / 2;
        int32_t ly = y + WM_ICON_SIZE + 12;
        gfx_draw_text(s, lx + 1, ly + 1, launchers[i].label, 1, col_label_shadow, GFX_TRANSPARENT);
        gfx_draw_text(s, lx, ly, launchers[i].label, 1, col_label, GFX_TRANSPARENT);
    }
}

static void draw_close_glyph(surface_t *s, int32_t x, int32_t y, uint32_t color) {
    for (int32_t i = 0; i < 8; i++) {
        gfx_fill_rect(s, x + 5 + i, y + 5 + i, 2, 2, color);
        gfx_fill_rect(s, x + 12 - i, y + 5 + i, 2, 2, color);
    }
}

static void draw_window(surface_t *s, window_t *win, int focused) {
    int32_t fx, fy, fw, fh;
    frame_rect(win, &fx, &fy, &fw, &fh);

    gfx_fill_rect(s, fx + SHADOW, fy + SHADOW, fw, fh, col_shadow);
    gfx_draw_rect(s, fx, fy, fw, fh, col_border);
    gfx_fill_rect(s, win->x, win->y - TITLE_H, (int32_t)win->surface.width, TITLE_H,
                  focused ? col_title_on : col_title_off);
    gfx_draw_text(s, win->x + 8, win->y - TITLE_H + (TITLE_H - FONT_HEIGHT) / 2, win->title, 1, col_title_text,
                  GFX_TRANSPARENT);

    int32_t cx, cy;
    close_rect(win, &cx, &cy);
    gfx_fill_rect(s, cx, cy, CLOSE_SIZE, CLOSE_SIZE, focused ? col_close_on : col_close_off);
    draw_close_glyph(s, cx, cy, col_title_text);

    gfx_blit(s, win->x, win->y, &win->surface);
}

static void format_uptime(char *buf) {
    uint64_t secs = pit_get_uptime_ms() / 1000;
    uint64_t parts[3] = {secs / 3600 % 100, secs / 60 % 60, secs % 60};

    for (int i = 0; i < 3; i++) {
        buf[i * 3] = (char)('0' + parts[i] / 10);
        buf[i * 3 + 1] = (char)('0' + parts[i] % 10);
        buf[i * 3 + 2] = i < 2 ? ':' : '\0';
    }
}

static void draw_taskbar(surface_t *s) {
    int32_t y = (int32_t)screen_h - TASKBAR_H;

    gfx_fill_rect(s, 0, y, (int32_t)screen_w, TASKBAR_H, col_bar);
    gfx_fill_rect(s, 0, y, (int32_t)screen_w, 1, col_bar_line);

    gfx_fill_rect(s, 12, y + 10, 12, 12, col_spark);
    gfx_draw_text(s, 32, y + (TASKBAR_H - FONT_HEIGHT) / 2, "Sparks", 1, col_bar_text, GFX_TRANSPARENT);

    window_t *top = top_window();
    for (int i = 0; i < window_count; i++) {
        int32_t bx, by, bw, bh;
        taskbar_button(i, &bx, &by, &bw, &bh);
        if (bx + bw > (int32_t)screen_w - 96) {
            break;
        }

        gfx_fill_rect(s, bx, by, bw, bh, by_age[i] == top ? col_bar_button_on : col_bar_button);

        char label[(TASKBAR_BUTTON_W - 16) / FONT_WIDTH + 1];
        kstrlcpy(label, by_age[i]->title, sizeof(label));
        gfx_draw_text(s, bx + 8, by + (bh - FONT_HEIGHT) / 2, label, 1, col_bar_text, GFX_TRANSPARENT);
    }

    char clock[9];
    format_uptime(clock);
    gfx_draw_text(s, (int32_t)screen_w - gfx_text_width(clock, 1) - 16, y + (TASKBAR_H - FONT_HEIGHT) / 2, clock, 1,
                  col_bar_text, GFX_TRANSPARENT);
}

static void compose(void) {
    surface_t *s = fb_surface();
    window_t *top = top_window();

    // clear before drawing, so an invalidate that lands mid-compose triggers
    // another frame instead of getting lost
    for (int i = 0; i < window_count; i++) {
        zorder[i]->dirty = 0;
    }
    need_redraw = 0;

    draw_desktop(s);
    for (int i = 0; i < window_count; i++) {
        draw_window(s, zorder[i], zorder[i] == top);
    }
    draw_taskbar(s);

    fb_mark_dirty(0, 0, screen_w, screen_h);
    fb_flush();
}

static void wm_loop(void) {
    for (;;) {
        adopt_pending();
        reap_windows();
        process_input();

        uint64_t second = pit_get_uptime_ms() / 1000;
        if (second != last_second) {
            last_second = second;
            need_redraw = 1;
        }

        for (int i = 0; i < window_count; i++) {
            if (zorder[i]->dirty) {
                need_redraw = 1;
            }
        }

        if (need_redraw) {
            compose();
        }

        task_sleep_ms(FRAME_MS);
    }
}

int wm_init(void) {
    if (!fb_double_buffered()) {
        return 0;
    }

    screen_w = fb_width();
    screen_h = fb_height();

    col_watermark = fb_rgb(0x1C, 0x33, 0x4D);
    col_shadow = fb_rgb(0x06, 0x0D, 0x14);
    col_border = fb_rgb(0x0F, 0x17, 0x2A);
    col_title_on = fb_rgb(0x25, 0x63, 0xEB);
    col_title_off = fb_rgb(0x47, 0x55, 0x69);
    col_title_text = fb_rgb(0xFF, 0xFF, 0xFF);
    col_close_on = fb_rgb(0xDC, 0x26, 0x26);
    col_close_off = fb_rgb(0x64, 0x74, 0x8B);
    col_bar = fb_rgb(0x0F, 0x17, 0x2A);
    col_bar_line = fb_rgb(0x33, 0x41, 0x55);
    col_bar_button = fb_rgb(0x1E, 0x29, 0x3B);
    col_bar_button_on = fb_rgb(0x25, 0x63, 0xEB);
    col_bar_text = fb_rgb(0xE2, 0xE8, 0xF0);
    col_spark = fb_rgb(0xF5, 0x9E, 0x0B);
    col_label = fb_rgb(0xFF, 0xFF, 0xFF);
    col_label_shadow = fb_rgb(0x00, 0x00, 0x00);

    last_mouse_x = mouse_x();
    last_mouse_y = mouse_y();

    int pid = task_create("wm", wm_loop);
    if (pid < 0) {
        return 0;
    }
    task_protect((uint32_t)pid);

    serial_write("wm: desktop up\n");
    return 1;
}

void wm_add_launcher(const char *label, wm_icon_fn icon, wm_launch_fn launch) {
    if (launcher_count == MAX_LAUNCHERS) {
        return;
    }

    kstrlcpy(launchers[launcher_count].label, label, TITLE_MAX);
    launchers[launcher_count].icon = icon;
    launchers[launcher_count].launch = launch;
    launcher_count++;
    need_redraw = 1;
}

window_t *wm_create_window(const char *title, uint32_t width, uint32_t height) {
    window_t *win = kmalloc(sizeof(window_t));
    if (win == NULL) {
        return NULL;
    }

    uint32_t *pixels = kmalloc((uint64_t)width * height * sizeof(uint32_t));
    if (pixels == NULL) {
        kfree(win);
        return NULL;
    }

    kstrlcpy(win->title, title, TITLE_MAX);
    win->surface.pixels = pixels;
    win->surface.width = width;
    win->surface.height = height;
    win->surface.pitch = width;
    gfx_fill_rect(&win->surface, 0, 0, (int32_t)width, (int32_t)height, fb_rgb(0, 0, 0));

    win->event_head = 0;
    win->event_tail = 0;
    win->waiter_pid = -1;
    win->owner_pid = task_current_pid();
    win->dirty = 1;
    win->closing = 0;
    win->focus_request = 0;
    win->x = 0;
    win->y = 0;

    // window_count is only written by the wm. a racy read here means at worst we
    // refuse a window that would have fit one frame later
    uint64_t flags = irq_save();
    if (window_count + pending_count >= MAX_WINDOWS) {
        irq_restore(flags);
        kfree(pixels);
        kfree(win);
        return NULL;
    }
    pending[pending_count++] = win;
    irq_restore(flags);

    return win;
}

surface_t *wm_window_surface(window_t *win) {
    return &win->surface;
}

void wm_invalidate(window_t *win) {
    win->dirty = 1;
}

void wm_focus(window_t *win) {
    win->focus_request = 1;
}

void wm_destroy_window(window_t *win) {
    win->closing = 1;
}

void wm_wait_event(window_t *win, wm_event_t *ev) {
    for (;;) {
        uint64_t flags = irq_save();

        if (win->event_head != win->event_tail) {
            *ev = win->events[win->event_head];
            win->event_head = (win->event_head + 1) % EVENT_QUEUE_SIZE;
            win->waiter_pid = -1;
            irq_restore(flags);
            return;
        }

        win->waiter_pid = task_current_pid();
        task_block();
        irq_restore(flags);
    }
}
