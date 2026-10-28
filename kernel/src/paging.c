#include <stdint.h>

#include "paging.h"
#include "pmm.h"

#define ENTRIES_PER_TABLE 512
#define PAGE_ADDR_MASK 0xFFFFFFFFFFFFF000ULL

// from boot.S, already in CR3
extern uint64_t p4_table[ENTRIES_PER_TABLE];

static uint64_t *table_at(uint64_t entry) {
    return (uint64_t *)(entry & PAGE_ADDR_MASK);
}

// XXX: new tables are touched through the identity map, so a frame above 1GiB
// from the PMM would be unreachable. fine with qemu's default 128M
static uint64_t *get_or_alloc_table(uint64_t *table, uint64_t index) {
    if (!(table[index] & PAGE_PRESENT)) {
        uint64_t frame = pmm_alloc_page();
        uint64_t *new_table = (uint64_t *)frame;

        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            new_table[i] = 0;
        }

        table[index] = frame | PAGE_PRESENT | PAGE_WRITABLE;
    }

    return table_at(table[index]);
}

void paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t p4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t p3_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t p2_idx = (virt_addr >> 21) & 0x1FF;
    uint64_t p1_idx = (virt_addr >> 12) & 0x1FF;

    uint64_t *p3 = get_or_alloc_table(p4_table, p4_idx);
    uint64_t *p2 = get_or_alloc_table(p3, p3_idx);
    uint64_t *p1 = get_or_alloc_table(p2, p2_idx);

    p1[p1_idx] = (phys_addr & PAGE_ADDR_MASK) | flags | PAGE_PRESENT;

    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}
