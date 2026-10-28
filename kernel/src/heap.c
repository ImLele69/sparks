#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "sync.h"

#define PAGE_SIZE 4096ULL
#define ALIGNMENT 8ULL

// grows in 256KiB steps, up to 512MiB of address space
#define HEAP_MAX_SIZE (512ULL * 1024 * 1024)
#define HEAP_GROW_STEP (256ULL * 1024)

// every block, free or not, is on the list in address order
typedef struct block_header {
    size_t size; // payload, header not included
    int free;
    struct block_header *next;
    struct block_header *prev;
} block_header_t;

static block_header_t *heap_head = NULL;
static uint64_t heap_end = 0;
static uint64_t heap_limit = 0;

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

void heap_init(uint64_t virt_start, size_t size) {
    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        uint64_t frame = pmm_alloc_page();
        paging_map(virt_start + off, frame, PAGE_WRITABLE);
    }

    heap_head = (block_header_t *)virt_start;
    heap_head->size = size - sizeof(block_header_t);
    heap_head->free = 1;
    heap_head->next = NULL;
    heap_head->prev = NULL;

    heap_end = virt_start + size;
    heap_limit = virt_start + HEAP_MAX_SIZE;
}

// blocks are contiguous and the last one ends at heap_end, so new pages either
// extend it (if free) or become a new free block at the tail
static int heap_grow(size_t min_size) {
    uint64_t bytes = align_up(min_size + sizeof(block_header_t), HEAP_GROW_STEP);
    uint64_t added = 0;

    if (heap_end + bytes > heap_limit) {
        return 0;
    }

    while (added < bytes) {
        uint64_t frame = pmm_alloc_page();
        if (frame == 0) {
            break;
        }
        paging_map(heap_end + added, frame, PAGE_WRITABLE);
        added += PAGE_SIZE;
    }

    if (added == 0) {
        return 0;
    }

    block_header_t *tail = heap_head;
    while (tail->next != NULL) {
        tail = tail->next;
    }

    if (tail->free) {
        tail->size += added;
    } else {
        block_header_t *blk = (block_header_t *)heap_end;
        blk->size = added - sizeof(block_header_t);
        blk->free = 1;
        blk->next = NULL;
        blk->prev = tail;
        tail->next = blk;
    }

    heap_end += added;
    return 1;
}

static void *alloc_block(size_t size) {
    for (block_header_t *cur = heap_head; cur != NULL; cur = cur->next) {
        if (!cur->free || cur->size < size) {
            continue;
        }

        // split only if the rest can hold a header and something useful
        if (cur->size >= size + sizeof(block_header_t) + ALIGNMENT) {
            block_header_t *rest = (block_header_t *)((uint8_t *)(cur + 1) + size);
            rest->size = cur->size - size - sizeof(block_header_t);
            rest->free = 1;
            rest->next = cur->next;
            rest->prev = cur;
            if (rest->next) {
                rest->next->prev = rest;
            }

            cur->next = rest;
            cur->size = size;
        }

        cur->free = 0;
        return (void *)(cur + 1);
    }

    return NULL;
}

// irqs off: the task reaper calls kfree from IRQ0 and could find the list in
// the middle of a split
void *kmalloc(size_t size) {
    size = align_up(size, ALIGNMENT);

    uint64_t flags = irq_save();

    void *ptr = alloc_block(size);
    if (ptr == NULL && heap_grow(size)) {
        ptr = alloc_block(size);
    }

    irq_restore(flags);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }

    uint64_t flags = irq_save();

    block_header_t *blk = (block_header_t *)ptr - 1;
    blk->free = 1;

    if (blk->next && blk->next->free) {
        blk->size += sizeof(block_header_t) + blk->next->size;
        blk->next = blk->next->next;
        if (blk->next) {
            blk->next->prev = blk;
        }
    }

    if (blk->prev && blk->prev->free) {
        blk->prev->size += sizeof(block_header_t) + blk->size;
        blk->prev->next = blk->next;
        if (blk->next) {
            blk->next->prev = blk->prev;
        }
    }

    irq_restore(flags);
}
