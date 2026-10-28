#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(uint64_t virt_start, size_t size);
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif
