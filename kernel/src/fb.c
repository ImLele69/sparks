#include <stddef.h>
#include <stdint.h>

#include "fb.h"
#include "gfx.h"
#include "multiboot2.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "sync.h"

#define PAGE_SIZE 4096ULL

// not identity mapped: a framebuffer below 1GiB would land inside the boot.S
// huge pages, which paging_map can't split
#define FB_VIRT_BASE 0x400000000ULL
#define BACK_VIRT_BASE 0x500000000ULL

#define MSR_PAT 0x277
#define PAT_WRITE_COMBINING 0x01ULL

#define CURSOR_W 12
#define CURSOR_H 20

static uint8_t *fb_base = NULL;
static uint32_t fb_pitch = 0;
static uint32_t fb_w = 0;
static uint32_t fb_h = 0;
static uint8_t red_pos, green_pos, blue_pos;

// all drawing goes here and fb_flush copies the dirty rect out. the mouse
// pointer is never in the back buffer, only on the real framebuffer, so erasing
// it is just copying that area again
static uint32_t *back = NULL;
static surface_t screen;
static uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;

static int cursor_visible = 0;
static uint32_t cursor_x, cursor_y;

// X black, . white, space transparent
static const char *const cursor_sprite[CURSOR_H] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X..........X",
    "X......XXXXX",
    "X...X..X    ",
    "X..XX..X    ",
    "X.X  X..X   ",
    "XX   X..X   ",
    "X     X..X  ",
    "      X..X  ",
    "       XX   ",
};

static int cpu_has_pat(void) {
    uint32_t eax = 1, ebx, ecx, edx;

    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx >> 16) & 1;
}

// PAT entry 1 defaults to write-through and nothing uses it. make it WC so
// PWT on the framebuffer pages means write-combining. without it every pixel is
// its own bus cycle and scrolling on real hw is visibly slow
static void pat_enable_wc(void) {
    uint32_t lo, hi;

    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_PAT));
    uint64_t pat = ((uint64_t)hi << 32) | lo;
    pat = (pat & ~(0xFFULL << 8)) | (PAT_WRITE_COMBINING << 8);
    __asm__ volatile("wrmsr" : : "a"((uint32_t)pat), "d"((uint32_t)(pat >> 32)), "c"(MSR_PAT));
}

static mb2_tag_framebuffer_t *find_framebuffer_tag(uint32_t mb_info_addr) {
    mb2_info_header_t *info = (mb2_info_header_t *)(uintptr_t)mb_info_addr;
    uint8_t *ptr = (uint8_t *)info + sizeof(mb2_info_header_t);
    uint8_t *info_end = (uint8_t *)info + info->total_size;

    while (ptr < info_end) {
        mb2_tag_t *tag = (mb2_tag_t *)ptr;

        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }
        if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER) {
            return (mb2_tag_framebuffer_t *)tag;
        }

        ptr += (tag->size + 7) & ~7u;
    }

    return NULL;
}

static uint32_t *front_row(uint32_t y) {
    return (uint32_t *)(fb_base + (uint64_t)y * fb_pitch);
}

static void copy_pixels(uint32_t *dst, const uint32_t *src, uint64_t count) {
    __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(count) : : "memory");
}

void fb_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (x >= fb_w || y >= fb_h) {
        return;
    }
    if (w > fb_w - x) {
        w = fb_w - x;
    }
    if (h > fb_h - y) {
        h = fb_h - y;
    }

    uint64_t flags = irq_save();

    if (dirty_x0 >= dirty_x1) {
        dirty_x0 = x;
        dirty_y0 = y;
        dirty_x1 = x + w;
        dirty_y1 = y + h;
    } else {
        if (x < dirty_x0) dirty_x0 = x;
        if (y < dirty_y0) dirty_y0 = y;
        if (x + w > dirty_x1) dirty_x1 = x + w;
        if (y + h > dirty_y1) dirty_y1 = y + h;
    }

    irq_restore(flags);
}

static void copy_to_front(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (x >= fb_w || y >= fb_h) {
        return;
    }
    if (w > fb_w - x) {
        w = fb_w - x;
    }
    if (h > fb_h - y) {
        h = fb_h - y;
    }

    for (uint32_t row = y; row < y + h; row++) {
        copy_pixels(front_row(row) + x, back + (uint64_t)row * fb_w + x, w);
    }
}

static void draw_cursor(void) {
    uint32_t black = fb_rgb(0x00, 0x00, 0x00);
    uint32_t white = fb_rgb(0xFF, 0xFF, 0xFF);

    for (uint32_t row = 0; row < CURSOR_H && cursor_y + row < fb_h; row++) {
        uint32_t *line = front_row(cursor_y + row);
        for (uint32_t col = 0; col < CURSOR_W && cursor_x + col < fb_w; col++) {
            char p = cursor_sprite[row][col];
            if (p == 'X') {
                line[cursor_x + col] = black;
            } else if (p == '.') {
                line[cursor_x + col] = white;
            }
        }
    }
}

static void alloc_back_buffer(void) {
    uint64_t bytes = (uint64_t)fb_w * fb_h * sizeof(uint32_t);
    uint64_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    // no unmap, so bail out before starting rather than halfway through
    if (pmm_free_pages() < pages + 64) {
        serial_write("fb: not enough memory for a back buffer, drawing direct\n");
        return;
    }

    for (uint64_t i = 0; i < pages; i++) {
        paging_map(BACK_VIRT_BASE + i * PAGE_SIZE, pmm_alloc_page(), PAGE_WRITABLE);
    }

    back = (uint32_t *)(uintptr_t)BACK_VIRT_BASE;
    serial_write("fb: back buffer, ");
    serial_write_dec(pages);
    serial_write(" pages\n");
}

int fb_init(uint32_t mb_info_addr) {
    mb2_tag_framebuffer_t *tag = find_framebuffer_tag(mb_info_addr);

    if (tag == NULL) {
        serial_write("fb: no framebuffer tag, staying in text mode\n");
        return 0;
    }
    if (tag->fb_type != MULTIBOOT2_FRAMEBUFFER_RGB || tag->bpp != 32) {
        serial_write("fb: unsupported mode (type ");
        serial_write_dec(tag->fb_type);
        serial_write(", ");
        serial_write_dec(tag->bpp);
        serial_write(" bpp), staying in text mode\n");
        return 0;
    }

    uint64_t flags = PAGE_WRITABLE;
    if (cpu_has_pat()) {
        pat_enable_wc();
        flags |= PAGE_WRITE_THROUGH;
    }

    uint64_t phys_start = tag->addr & ~(PAGE_SIZE - 1);
    uint64_t phys_end = tag->addr + (uint64_t)tag->pitch * tag->height;
    for (uint64_t phys = phys_start; phys < phys_end; phys += PAGE_SIZE) {
        paging_map(FB_VIRT_BASE + (phys - phys_start), phys, flags);
    }

    fb_base = (uint8_t *)(uintptr_t)(FB_VIRT_BASE + (tag->addr - phys_start));
    fb_pitch = tag->pitch;
    fb_w = tag->width;
    fb_h = tag->height;
    red_pos = tag->red_pos;
    green_pos = tag->green_pos;
    blue_pos = tag->blue_pos;

    serial_write("fb: ");
    serial_write_dec(fb_w);
    serial_write("x");
    serial_write_dec(fb_h);
    serial_write(" at ");
    serial_write_hex(tag->addr);
    serial_write("\n");

    alloc_back_buffer();

    // no back buffer: draw straight to the screen, and no mouse pointer
    if (back) {
        screen.pixels = back;
        screen.pitch = fb_w;
    } else {
        screen.pixels = (uint32_t *)fb_base;
        screen.pitch = fb_pitch / sizeof(uint32_t);
    }
    screen.width = fb_w;
    screen.height = fb_h;

    return 1;
}

int fb_available(void) {
    return fb_base != NULL;
}

uint32_t fb_width(void) {
    return fb_w;
}

uint32_t fb_height(void) {
    return fb_h;
}

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << red_pos) | ((uint32_t)g << green_pos) | ((uint32_t)b << blue_pos);
}

surface_t *fb_surface(void) {
    return &screen;
}

int fb_double_buffered(void) {
    return back != NULL;
}

// irqs off for the whole copy: IRQ12 moves the pointer, and if it redrew it
// halfway through, the next rows we copy would paint over it
void fb_flush(void) {
    if (back == NULL) {
        return;
    }

    uint64_t flags = irq_save();

    if (dirty_x0 < dirty_x1) {
        copy_to_front(dirty_x0, dirty_y0, dirty_x1 - dirty_x0, dirty_y1 - dirty_y0);

        int overlaps = cursor_visible && cursor_x < dirty_x1 && cursor_x + CURSOR_W > dirty_x0 &&
                       cursor_y < dirty_y1 && cursor_y + CURSOR_H > dirty_y0;
        if (overlaps) {
            draw_cursor();
        }

        dirty_x0 = dirty_x1 = 0;
    }

    irq_restore(flags);
}

void fb_cursor_move(uint32_t x, uint32_t y) {
    if (back == NULL) {
        return;
    }

    uint64_t flags = irq_save();

    if (cursor_visible) {
        copy_to_front(cursor_x, cursor_y, CURSOR_W, CURSOR_H);
    }

    cursor_x = x;
    cursor_y = y;
    cursor_visible = 1;
    draw_cursor();

    irq_restore(flags);
}
