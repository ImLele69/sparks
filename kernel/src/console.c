#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "fb.h"
#include "font.h"
#include "gfx.h"
#include "io.h"
#include "keyboard.h"
#include "kstring.h"
#include "sync.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define VGA_CRTC_PORT 0x3D4
#define VGA_CRTC_DATA 0x3D5

// enough for 1920x1200
#define MAX_COLS 240
#define MAX_ROWS 75

#define CURSOR_HEIGHT 2

static uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;
static const uint8_t COLOR = 0x0F; // white on black

// shadow of what's on screen. only cells that actually change get redrawn, so
// scrolling never reads pixels back and blank rows cost nothing
static char cells[MAX_ROWS][MAX_COLS];
static size_t rows = VGA_HEIGHT;
static size_t cols = VGA_WIDTH;
static int graphics = 0;
static uint32_t fg_color, bg_color;

// the screen before the desktop starts, the terminal window after. NULL means
// output is dropped (terminal closed)
static surface_t *target = NULL;
static console_mark_fn mark_fn = NULL;
static console_present_fn present_fn = NULL;
static console_input_fn input_fn = NULL;

static size_t cursor_row = 0;
static size_t cursor_col = 0;
static size_t drawn_cursor_row = 0;
static size_t drawn_cursor_col = 0;

static void render_cell(size_t row, size_t col) {
    char c = cells[row][col];

    if (!graphics) {
        VGA_MEMORY[row * VGA_WIDTH + col] = (uint16_t)(uint8_t)c | (uint16_t)(COLOR << 8);
        return;
    }
    if (target == NULL) {
        return;
    }

    int32_t x = (int32_t)(col * FONT_WIDTH);
    int32_t y = (int32_t)(row * FONT_HEIGHT);

    gfx_draw_char(target, x, y, c, 1, fg_color, bg_color);
    if (row == cursor_row && col == cursor_col) {
        gfx_fill_rect(target, x, y + FONT_HEIGHT - CURSOR_HEIGHT, FONT_WIDTH, CURSOR_HEIGHT, fg_color);
    }
    if (mark_fn) {
        mark_fn((uint32_t)x, (uint32_t)y, FONT_WIDTH, FONT_HEIGHT);
    }
}

static void present(void) {
    if (graphics && target != NULL && present_fn) {
        present_fn();
    }
}

static void set_cell(size_t row, size_t col, char c) {
    if (cells[row][col] == c) {
        return;
    }

    cells[row][col] = c;
    render_cell(row, col);
}

// text mode has a hardware cursor. in graphics mode it's an underline, so
// redraw the cell that loses it and the one that gets it
static void update_cursor(void) {
    if (!graphics) {
        uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);

        outb(VGA_CRTC_PORT, 0x0F);
        outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
        outb(VGA_CRTC_PORT, 0x0E);
        outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
        return;
    }

    if (drawn_cursor_row == cursor_row && drawn_cursor_col == cursor_col) {
        return;
    }

    size_t old_row = drawn_cursor_row;
    size_t old_col = drawn_cursor_col;
    drawn_cursor_row = cursor_row;
    drawn_cursor_col = cursor_col;
    if (old_row < rows) {
        render_cell(old_row, old_col);
    }
    render_cell(cursor_row, cursor_col);
}

// writes every cell even if the shadow says blank: the first time around the
// screen still has whatever GRUB left there
static void clear(void) {
    cursor_row = 0;
    cursor_col = 0;

    for (size_t row = 0; row < rows; row++) {
        for (size_t col = 0; col < cols; col++) {
            cells[row][col] = ' ';
            render_cell(row, col);
        }
    }

    drawn_cursor_row = 0;
    drawn_cursor_col = 0;
    update_cursor();
}

void console_init(void) {
    if (fb_available()) {
        surface_t *screen = fb_surface();

        fg_color = fb_rgb(0xFF, 0xFF, 0xFF);
        bg_color = fb_rgb(0x00, 0x00, 0x00);
        // covers the leftover pixels right and below the grid
        gfx_fill_rect(screen, 0, 0, (int32_t)screen->width, (int32_t)screen->height, bg_color);
        fb_mark_dirty(0, 0, screen->width, screen->height);
        console_attach(screen, fb_mark_dirty, fb_flush);
        return;
    }

    console_clear();
}

void console_attach(surface_t *surface, console_mark_fn mark, console_present_fn present_cb) {
    uint64_t flags = irq_save();

    graphics = 1;
    target = surface;
    mark_fn = mark;
    present_fn = present_cb;

    cols = surface->width / FONT_WIDTH;
    rows = surface->height / FONT_HEIGHT;
    if (cols > MAX_COLS) {
        cols = MAX_COLS;
    }
    if (rows > MAX_ROWS) {
        rows = MAX_ROWS;
    }

    clear();
    present();
    irq_restore(flags);
}

void console_detach(void) {
    uint64_t flags = irq_save();
    target = NULL;
    irq_restore(flags);
}

void console_set_input(console_input_fn fn) {
    input_fn = fn;
}

char console_getchar(void) {
    return input_fn ? input_fn() : keyboard_getchar();
}

void console_clear(void) {
    uint64_t flags = irq_save();
    clear();
    present();
    irq_restore(flags);
}

static void scroll_if_needed(void) {
    if (cursor_row < rows) {
        return;
    }

    // the diff doesn't know about the underline: if the cell under the cursor
    // stays blank it won't be repainted and the cursor ghosts. cursor_row is past
    // the grid here, so render_cell draws it without
    if (graphics && drawn_cursor_row < rows) {
        render_cell(drawn_cursor_row, drawn_cursor_col);
        drawn_cursor_row = rows;
    }

    for (size_t row = 1; row < rows; row++) {
        for (size_t col = 0; col < cols; col++) {
            set_cell(row - 1, col, cells[row][col]);
        }
    }

    for (size_t col = 0; col < cols; col++) {
        set_cell(rows - 1, col, ' ');
    }

    cursor_row = rows - 1;
}

static void put_char(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            set_cell(cursor_row, cursor_col, ' ');
        }
    } else if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7u;
    } else {
        set_cell(cursor_row, cursor_col, c);
        cursor_col++;
    }

    if (cursor_col >= cols) {
        cursor_col = 0;
        cursor_row++;
    }

    scroll_if_needed();
    update_cursor();
}

// irqs off: spawned tasks print while the shell does, and the terminal can be
// detached while someone is drawing into it
void console_putc(char c) {
    uint64_t flags = irq_save();
    put_char(c);
    present();
    irq_restore(flags);
}

// for line editing, moves without touching the text
void console_cursor_left(void) {
    uint64_t flags = irq_save();
    if (cursor_col > 0) {
        cursor_col--;
        update_cursor();
        present();
    }
    irq_restore(flags);
}

void console_cursor_right(void) {
    uint64_t flags = irq_save();
    if (cursor_col < cols - 1) {
        cursor_col++;
        update_cursor();
        present();
    }
    irq_restore(flags);
}

void console_write(const char *str) {
    uint64_t flags = irq_save();
    for (size_t i = 0; str[i] != '\0'; i++) {
        put_char(str[i]);
    }
    present();
    irq_restore(flags);
}

void console_write_hex(uint64_t val) {
    char buf[19];
    console_write(fmt_hex(buf, val));
}

void console_write_dec(uint64_t val) {
    char buf[21];
    console_write(fmt_dec(buf, val));
}
