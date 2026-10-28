#include <stddef.h>
#include <stdint.h>

#include "calc.h"
#include "console.h"
#include "fb.h"
#include "heap.h"
#include "idt.h"
#include "keyboard.h"
#include "mouse.h"
#include "paging.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "serial.h"
#include "shell.h"
#include "task.h"
#include "terminal.h"
#include "tss.h"
#include "wm.h"

#define HEAP_BASE 0x200000000ULL // 8GiB, well clear of everything else

// these return after a few rounds, which exercises task_exit and the reaper
// on every boot
static void demo(const char *name, uint64_t delay_ms) {
    for (uint64_t i = 0; i < 3; i++) {
        serial_write(name);
        serial_write(": round ");
        serial_write_dec(i);
        serial_write("\n");
        task_sleep_ms(delay_ms);
    }
}

static void demo_a(void) {
    demo("demo_a", 60);
}

static void demo_b(void) {
    demo("demo_b", 90);
}

static void selftest_paging(void) {
    uint64_t phys = pmm_alloc_page();
    uint64_t virt = 0x40000000; // just past the boot identity map
    paging_map(virt, phys, PAGE_WRITABLE);

    *(volatile uint64_t *)virt = 0xDEADBEEFCAFEBABE;
    serial_write("paging: wrote at ");
    serial_write_hex(virt);
    serial_write(", read back ");
    serial_write_hex(*(volatile uint64_t *)phys);
    serial_write(" through the identity map\n");
}

static void selftest_heap(void) {
    void *a = kmalloc(64);
    void *b = kmalloc(128);
    kfree(a);
    void *c = kmalloc(32);

    // c should reuse a's block
    serial_write("heap: a=");
    serial_write_hex((uint64_t)a);
    serial_write(" c=");
    serial_write_hex((uint64_t)c);
    serial_write(a == c ? " ok\n" : " NOT reused\n");
    kfree(b);
    kfree(c);
}

void kernel_main(uint32_t mb_info_addr) {
    serial_init();
    serial_write("sparks: serial up\n");

    idt_init();
    tss_init();
    pmm_init(mb_info_addr);
    selftest_paging();

    heap_init(HEAP_BASE, 1024 * 1024);
    selftest_heap();

    // after the heap: mapping the framebuffer pulls page tables from the PMM
    fb_init(mb_info_addr);
    console_init();

    pic_remap();
    keyboard_init();
    mouse_init();
    pit_init(100);
    __asm__ volatile("sti");

    task_init();
    task_create("demo_a", demo_a);
    task_create("demo_b", demo_b);
    task_sleep_ms(400);
    serial_write("sched: kernel_main is back\n");

    // no back buffer means no desktop. otherwise the shell keeps running in this
    // task, just inside the terminal window
    if (wm_init()) {
        wm_add_launcher("Terminal", terminal_draw_icon, terminal_launch);
        wm_add_launcher("Calculator", calc_draw_icon, calc_launch);
        terminal_init();
    } else {
        console_clear();
    }

    shell_run();
}
