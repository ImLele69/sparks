#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>

#define RFLAGS_IF 0x200ULL

// single core, so a critical section is just cli. saving RFLAGS lets them nest
// without the inner one re-enabling irqs in the middle of the outer one
static inline uint64_t irq_save(void) {
    uint64_t flags;

    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & RFLAGS_IF) {
        __asm__ volatile("sti" : : : "memory");
    }
}

#endif
