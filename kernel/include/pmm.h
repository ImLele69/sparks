#ifndef PMM_H
#define PMM_H

#include <stdint.h>

void pmm_init(uint32_t mb_info_addr);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t addr);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
void pmm_print_mmap(void);

#endif
