#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_PRESENT 0x1ULL
#define PAGE_WRITABLE 0x2ULL
#define PAGE_WRITE_THROUGH 0x8ULL

// not for addresses in the first GiB: boot.S mapped that with 2MiB pages, there's no P1 to walk
void paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

#endif
