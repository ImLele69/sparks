#include <stddef.h>
#include <stdint.h>

#include "calc.h"
#include "fb.h"
#include "font.h"
#include "gfx.h"
#include "kstring.h"
#include "task.h"
#include "wm.h"

#define BTN_W 56
#define BTN_H 44
#define GAP 8
#define DISPLAY_H 72
#define GRID_COLS 4
#define GRID_ROWS 5

#define WIN_W (GRID_COLS * BTN_W + (GRID_COLS + 1) * GAP)
#define WIN_H (GAP + DISPLAY_H + GAP + GRID_ROWS * BTN_H + GRID_ROWS * GAP)

// 14 digits per operand. two of those can still overflow when multiplied, which
// the overflow builtins catch, rather than capping input at 9 digits
#define MAX_ENTRY 99999999999999LL

typedef enum { KIND_DIGIT, KIND_OP, KIND_EQUALS, KIND_CLEAR, KIND_BACK, KIND_NEGATE } button_kind_t;

typedef struct {
    const char *label;
    int col;
    int row;
    int span;
    button_kind_t kind;
    char value;
} button_t;

static const button_t buttons[] = {
    {"C", 0, 0, 1, KIND_CLEAR, 0},   {"<-", 1, 0, 1, KIND_BACK, 0},  {"+/-", 2, 0, 1, KIND_NEGATE, 0},
    {"/", 3, 0, 1, KIND_OP, '/'},    {"7", 0, 1, 1, KIND_DIGIT, 7},  {"8", 1, 1, 1, KIND_DIGIT, 8},
    {"9", 2, 1, 1, KIND_DIGIT, 9},   {"*", 3, 1, 1, KIND_OP, '*'},   {"4", 0, 2, 1, KIND_DIGIT, 4},
    {"5", 1, 2, 1, KIND_DIGIT, 5},   {"6", 2, 2, 1, KIND_DIGIT, 6},  {"-", 3, 2, 1, KIND_OP, '-'},
    {"1", 0, 3, 1, KIND_DIGIT, 1},   {"2", 1, 3, 1, KIND_DIGIT, 2},  {"3", 2, 3, 1, KIND_DIGIT, 3},
    {"+", 3, 3, 1, KIND_OP, '+'},    {"0", 0, 4, 2, KIND_DIGIT, 0},  {"=", 2, 4, 2, KIND_EQUALS, 0},
};

#define BUTTON_COUNT ((int)(sizeof(buttons) / sizeof(buttons[0])))

// int64 only, the kernel is built without SSE
typedef struct {
    int64_t acc;
    int64_t cur;
    char op;
    int entering; // typing into cur, otherwise the display shows acc
    int has_acc;
    int error;
    int pressed; // button held down with the mouse, -1 if none
} calc_t;

static void button_rect(const button_t *b, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    *x = GAP + b->col * (BTN_W + GAP);
    *y = GAP + DISPLAY_H + GAP + b->row * (BTN_H + GAP);
    *w = b->span * BTN_W + (b->span - 1) * GAP;
    *h = BTN_H;
}

static int button_at(int32_t px, int32_t py) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        int32_t x, y, w, h;
        button_rect(&buttons[i], &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            return i;
        }
    }
    return -1;
}

static void format_int(int64_t v, char *buf) {
    char tmp[21];
    int len = 0;
    uint64_t mag = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;

    do {
        tmp[len++] = (char)('0' + mag % 10);
        mag /= 10;
    } while (mag > 0);

    int i = 0;
    if (v < 0) {
        buf[i++] = '-';
    }
    while (len > 0) {
        buf[i++] = tmp[--len];
    }
    buf[i] = '\0';
}

static int apply(int64_t a, char op, int64_t b, int64_t *out) {
    switch (op) {
        case '+': return !__builtin_add_overflow(a, b, out);
        case '-': return !__builtin_sub_overflow(a, b, out);
        case '*': return !__builtin_mul_overflow(a, b, out);
        case '/':
            // INT64_MIN / -1 doesn't fit and idiv faults on it
            if (b == 0 || (a == INT64_MIN && b == -1)) {
                return 0;
            }
            *out = a / b;
            return 1;
        default: return 0;
    }
}

static void press_digit(calc_t *c, int d) {
    if (!c->entering) {
        c->cur = 0;
        c->entering = 1;
    }
    if (c->cur > MAX_ENTRY / 10 || c->cur < -(MAX_ENTRY / 10)) {
        return;
    }
    c->cur = c->cur * 10 + (c->cur < 0 ? -d : d);
}

static void press_op(calc_t *c, char op) {
    if (c->entering || !c->has_acc) {
        int64_t value = c->entering ? c->cur : 0;
        if (c->has_acc && c->op) {
            if (!apply(c->acc, c->op, value, &c->acc)) {
                c->error = 1;
                return;
            }
        } else {
            c->acc = value;
        }
        c->has_acc = 1;
        c->entering = 0;
    }
    c->op = op;
}

// "5 + =" gives 10 like a real calculator: no second operand means reuse acc
static void press_equals(calc_t *c) {
    if (c->op) {
        int64_t operand = c->entering ? c->cur : c->acc;
        if (!apply(c->acc, c->op, operand, &c->acc)) {
            c->error = 1;
            return;
        }
        c->op = 0;
    } else if (c->entering) {
        c->acc = c->cur;
    }
    c->has_acc = 1;
    c->entering = 0;
}

static void press(calc_t *c, const button_t *b) {
    if (b->kind == KIND_CLEAR) {
        c->acc = 0;
        c->cur = 0;
        c->op = 0;
        c->entering = 0;
        c->has_acc = 0;
        c->error = 0;
        return;
    }
    if (c->error) {
        return;
    }

    switch (b->kind) {
        case KIND_DIGIT: press_digit(c, b->value); break;
        case KIND_OP: press_op(c, b->value); break;
        case KIND_EQUALS: press_equals(c); break;
        case KIND_BACK:
            if (c->entering) {
                c->cur /= 10;
            }
            break;
        case KIND_NEGATE:
            if (c->entering) {
                c->cur = -c->cur;
            } else if (c->has_acc && c->acc != INT64_MIN) {
                c->acc = -c->acc;
            }
            break;
        default: break;
    }
}

static int button_for_key(char key) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        const button_t *b = &buttons[i];
        if (b->kind == KIND_DIGIT && key == '0' + b->value) return i;
        if (b->kind == KIND_OP && key == b->value) return i;
        if (b->kind == KIND_EQUALS && (key == '=' || key == '\n')) return i;
        if (b->kind == KIND_BACK && key == '\b') return i;
        if (b->kind == KIND_CLEAR && (key == 'c' || key == 'C' || key == 27)) return i;
    }
    return -1;
}

static void draw(surface_t *s, const calc_t *c) {
    uint32_t bg = fb_rgb(0x1F, 0x24, 0x30);
    uint32_t display_bg = fb_rgb(0x11, 0x15, 0x1C);
    uint32_t text = fb_rgb(0xF8, 0xFA, 0xFC);
    uint32_t dim = fb_rgb(0x94, 0xA3, 0xB8);

    gfx_fill_rect(s, 0, 0, WIN_W, WIN_H, bg);
    gfx_fill_rect(s, GAP, GAP, WIN_W - 2 * GAP, DISPLAY_H, display_bg);

    char line[48];
    if (c->has_acc && c->op) {
        format_int(c->acc, line);
        size_t len = kstrlen(line);
        line[len] = ' ';
        line[len + 1] = c->op;
        line[len + 2] = '\0';
        gfx_draw_text(s, GAP + 8, GAP + 6, line, 1, dim, GFX_TRANSPARENT);
    }

    const char *shown = line;
    if (c->error) {
        shown = "Error";
    } else {
        format_int(c->entering ? c->cur : (c->has_acc ? c->acc : 0), line);
    }

    // biggest scale that fits
    int32_t room = WIN_W - 2 * GAP - 16;
    uint32_t scale = 3;
    while (scale > 1 && gfx_text_width(shown, scale) > room) {
        scale--;
    }
    int32_t tw = gfx_text_width(shown, scale);
    int32_t ty = GAP + DISPLAY_H - FONT_HEIGHT * (int32_t)scale - 8;
    gfx_draw_text(s, WIN_W - GAP - 8 - tw, ty, shown, scale, text, GFX_TRANSPARENT);

    for (int i = 0; i < BUTTON_COUNT; i++) {
        const button_t *b = &buttons[i];
        int32_t x, y, w, h;
        button_rect(b, &x, &y, &w, &h);

        uint32_t color;
        switch (b->kind) {
            case KIND_OP: color = fb_rgb(0xF5, 0x9E, 0x0B); break;
            case KIND_EQUALS: color = fb_rgb(0x25, 0x63, 0xEB); break;
            case KIND_CLEAR: color = fb_rgb(0xDC, 0x26, 0x26); break;
            case KIND_DIGIT: color = fb_rgb(0x37, 0x41, 0x51); break;
            default: color = fb_rgb(0x4B, 0x55, 0x63); break;
        }
        if (i == c->pressed) {
            color = fb_rgb(0xCB, 0xD5, 0xE1);
        }

        gfx_fill_rect(s, x, y, w, h, color);
        int32_t lw = gfx_text_width(b->label, 2);
        gfx_draw_text(s, x + (w - lw) / 2, y + (h - FONT_HEIGHT * 2) / 2, b->label, 2, text, GFX_TRANSPARENT);
    }
}

static void calc_main(void) {
    window_t *win = wm_create_window("Calculator", WIN_W, WIN_H);
    if (win == NULL) {
        return;
    }

    surface_t *s = wm_window_surface(win);
    calc_t c = {0, 0, 0, 0, 0, 0, -1};

    draw(s, &c);
    wm_invalidate(win);

    for (;;) {
        wm_event_t ev;
        wm_wait_event(win, &ev);

        if (ev.type == WM_EVENT_CLOSE) {
            wm_destroy_window(win);
            return;
        }

        if (ev.type == WM_EVENT_MOUSE_DOWN) {
            c.pressed = button_at(ev.x, ev.y);
        } else if (ev.type == WM_EVENT_MOUSE_UP) {
            // only counts if released on the same button, dragging off cancels
            int b = button_at(ev.x, ev.y);
            if (b >= 0 && b == c.pressed) {
                press(&c, &buttons[b]);
            }
            c.pressed = -1;
        } else if (ev.type == WM_EVENT_KEY) {
            int b = button_for_key(ev.key);
            if (b >= 0) {
                press(&c, &buttons[b]);
            }
        }

        draw(s, &c);
        wm_invalidate(win);
    }
}

void calc_launch(void) {
    task_create("calc", calc_main);
}

void calc_draw_icon(surface_t *s, int32_t x, int32_t y) {
    gfx_fill_rect(s, x + 6, y, WM_ICON_SIZE - 12, WM_ICON_SIZE, fb_rgb(0x9C, 0xA3, 0xAF));
    gfx_fill_rect(s, x + 8, y + 2, WM_ICON_SIZE - 16, WM_ICON_SIZE - 4, fb_rgb(0x1F, 0x24, 0x30));
    gfx_fill_rect(s, x + 12, y + 6, WM_ICON_SIZE - 24, 10, fb_rgb(0x86, 0xEF, 0xAC));

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            uint32_t color = col == 2 ? fb_rgb(0xF5, 0x9E, 0x0B) : fb_rgb(0x4B, 0x55, 0x63);
            gfx_fill_rect(s, x + 12 + col * 11, y + 22 + row * 10, 9, 8, color);
        }
    }
}
