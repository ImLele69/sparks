#include <stddef.h>
#include <stdint.h>

#include "tss.h"

#define KERNEL_STACK_SIZE (16 * 1024)
#define DF_STACK_SIZE (16 * 1024)

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss_t;

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;
} gdt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} tss_descriptor_t;

typedef struct __attribute__((packed)) {
    gdt_entry_t null_entry;
    gdt_entry_t code_entry;
    tss_descriptor_t tss_entry;
} gdt_table_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

static tss_t tss;
static gdt_table_t gdt;
static gdt_ptr_t gdt_ptr;

static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t df_stack[DF_STACK_SIZE] __attribute__((aligned(16)));

void tss_init(void) {
    uint64_t tss_base = (uint64_t)&tss;
    uint64_t tss_limit = sizeof(tss_t) - 1;

    for (size_t i = 0; i < sizeof(tss_t); i++) {
        ((uint8_t *)&tss)[i] = 0;
    }
    tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack));
    tss.ist1 = (uint64_t)(df_stack + sizeof(df_stack));
    tss.iomap_base = sizeof(tss_t);

    gdt.null_entry = (gdt_entry_t){0};

    gdt.code_entry = (gdt_entry_t){
        .limit_low = 0,
        .base_low = 0,
        .base_mid = 0,
        .access = 0x98,         // present, code, exec
        .limit_high_flags = 0x20, // L
        .base_high = 0,
    };

    gdt.tss_entry = (tss_descriptor_t){
        .limit_low = (uint16_t)(tss_limit & 0xFFFF),
        .base_low = (uint16_t)(tss_base & 0xFFFF),
        .base_mid = (uint8_t)((tss_base >> 16) & 0xFF),
        .access = 0x89, // present, available 64-bit TSS
        .limit_high_flags = (uint8_t)((tss_limit >> 16) & 0x0F),
        .base_high = (uint8_t)((tss_base >> 24) & 0xFF),
        .base_upper = (uint32_t)(tss_base >> 32),
        .reserved = 0,
    };

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    __asm__ volatile("lgdt (%0)" : : "r"(&gdt_ptr));

    uint16_t tss_selector = (uint16_t)offsetof(gdt_table_t, tss_entry);
    __asm__ volatile("ltr %0" : : "r"(tss_selector));
}
