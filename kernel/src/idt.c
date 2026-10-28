#include <stdint.h>

#include "idt.h"

#define IDT_ENTRIES 256
#define IDT_INT_GATE 0x8E
#define KERNEL_CODE_SELECTOR 0x08
#define VEC_DOUBLE_FAULT 8

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

// both tables live in isr.S
extern void (*const isr_stub_table[32])(void);
extern void (*const irq_stub_table[16])(void);

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

static void set_gate(uint8_t vector, void (*handler)(void), uint8_t ist) {
    uint64_t addr = (uint64_t)handler;

    idt[vector] = (idt_entry_t){
        .offset_low = (uint16_t)addr,
        .selector = KERNEL_CODE_SELECTOR,
        .ist = ist,
        .type_attr = IDT_INT_GATE,
        .offset_mid = (uint16_t)(addr >> 16),
        .offset_high = (uint32_t)(addr >> 32),
    };
}

void idt_init(void) {
    for (int i = 0; i < 32; i++) {
        // double fault gets its own stack (IST1), the current one may be what broke
        set_gate((uint8_t)i, isr_stub_table[i], i == VEC_DOUBLE_FAULT ? 1 : 0);
    }
    for (int i = 0; i < 16; i++) {
        set_gate((uint8_t)(32 + i), irq_stub_table[i], 0);
    }

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;
    __asm__ volatile("lidt (%0)" : : "r"(&idt_ptr));
}
