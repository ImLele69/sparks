#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "kstring.h"
#include "multiboot2.h"
#include "pmm.h"
#include "serial.h"
#include "sync.h"

#define PAGE_SIZE 4096ULL

// static bitmap, 128KiB covers 4GiB of RAM
#define MAX_PHYS_MEMORY (4ULL * 1024 * 1024 * 1024)
#define BITMAP_BITS (MAX_PHYS_MEMORY / PAGE_SIZE)
#define BITMAP_BYTES (BITMAP_BITS / 8)

extern char _kernel_start[];
extern char _kernel_end[];

static uint8_t bitmap[BITMAP_BYTES];
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint32_t saved_mb_info_addr = 0;
// next-fit: without it every alloc rescans the already full low memory, and
// mapping a few MiB of back buffer took millions of bit tests
static uint64_t search_hint = 0;

static inline void bitmap_set(uint64_t frame) {
    bitmap[frame / 8] |= (uint8_t)(1u << (frame % 8));
}

static inline void bitmap_clear(uint64_t frame) {
    bitmap[frame / 8] &= (uint8_t)~(1u << (frame % 8));
}

static inline int bitmap_test(uint64_t frame) {
    return (bitmap[frame / 8] >> (frame % 8)) & 1;
}

// rounds inwards, a partially available frame is not available
static void mark_free(uint64_t addr, uint64_t len) {
    uint64_t start = (addr + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t end = (addr + len) / PAGE_SIZE;

    if (end > BITMAP_BITS) {
        end = BITMAP_BITS;
    }

    for (uint64_t frame = start; frame < end; frame++) {
        bitmap_clear(frame);
    }
}

// rounds outwards
static void mark_used(uint64_t addr, uint64_t len) {
    uint64_t start = addr / PAGE_SIZE;
    uint64_t end = (addr + len + PAGE_SIZE - 1) / PAGE_SIZE;

    if (end > BITMAP_BITS) {
        end = BITMAP_BITS;
    }

    for (uint64_t frame = start; frame < end; frame++) {
        bitmap_set(frame);
    }
}

void pmm_init(uint32_t mb_info_addr) {
    saved_mb_info_addr = mb_info_addr;

    mb2_info_header_t *info = (mb2_info_header_t *)(uintptr_t)mb_info_addr;
    uint8_t *ptr = (uint8_t *)info + sizeof(mb2_info_header_t);
    uint8_t *info_end = (uint8_t *)info + info->total_size;
    uint64_t highest_addr = 0;

    // start with everything used and free what the mmap says is available
    for (uint64_t i = 0; i < BITMAP_BYTES; i++) {
        bitmap[i] = 0xFF;
    }

    while (ptr < info_end) {
        mb2_tag_t *tag = (mb2_tag_t *)ptr;

        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }

        if (tag->type == MULTIBOOT2_TAG_MMAP) {
            mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
            uint8_t *entry_ptr = (uint8_t *)mmap->entries;
            uint8_t *mmap_end = (uint8_t *)tag + tag->size;

            while (entry_ptr < mmap_end) {
                mb2_mmap_entry_t *entry = (mb2_mmap_entry_t *)entry_ptr;

                if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
                    mark_free(entry->base_addr, entry->length);

                    uint64_t region_end = entry->base_addr + entry->length;
                    if (region_end > highest_addr) {
                        highest_addr = region_end;
                    }
                }

                entry_ptr += mmap->entry_size;
            }
        }

        // tags are 8-byte aligned
        ptr += (tag->size + 7) & ~7u;
    }

    // low 1MiB: IVT, BDA, VGA memory
    mark_used(0, 1024 * 1024);

    mark_used((uint64_t)(uintptr_t)_kernel_start, (uint64_t)(uintptr_t)(_kernel_end - _kernel_start));

    // the mb2 info too, fb_init and the memmap command read it again later
    mark_used(mb_info_addr, info->total_size);

    total_pages = highest_addr / PAGE_SIZE;
    if (total_pages > BITMAP_BITS) {
        total_pages = BITMAP_BITS;
    }

    free_pages = 0;
    for (uint64_t frame = 0; frame < total_pages; frame++) {
        if (!bitmap_test(frame)) {
            free_pages++;
        }
    }

    serial_write("pmm: ");
    serial_write_dec(highest_addr >> 20);
    serial_write(" MiB, ");
    serial_write_dec(free_pages);
    serial_write("/");
    serial_write_dec(total_pages);
    serial_write(" pages free\n");
}

uint64_t pmm_alloc_page(void) {
    uint64_t flags = irq_save();

    for (uint64_t i = 0; i < total_pages; i++) {
        uint64_t frame = (search_hint + i) % total_pages;
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            free_pages--;
            search_hint = frame + 1;
            irq_restore(flags);
            return frame * PAGE_SIZE;
        }
    }

    irq_restore(flags);
    return 0; // frame 0 is never free, so it doubles as "out of memory"
}

void pmm_free_page(uint64_t addr) {
    uint64_t frame = addr / PAGE_SIZE;
    uint64_t flags = irq_save();

    if (frame < total_pages && bitmap_test(frame)) {
        bitmap_clear(frame);
        free_pages++;
        if (frame < search_hint) {
            search_hint = frame;
        }
    }

    irq_restore(flags);
}

uint64_t pmm_total_pages(void) {
    return total_pages;
}

uint64_t pmm_free_pages(void) {
    return free_pages;
}

static const char *mmap_type_name(uint32_t type) {
    switch (type) {
        case MULTIBOOT2_MEMORY_AVAILABLE: return "available";
        case MULTIBOOT2_MEMORY_RESERVED: return "reserved";
        case MULTIBOOT2_MEMORY_ACPI_RECLAIMABLE: return "ACPI reclaimable";
        case MULTIBOOT2_MEMORY_NVS: return "ACPI NVS";
        case MULTIBOOT2_MEMORY_BADRAM: return "bad";
        default: return "unknown";
    }
}

void pmm_print_mmap(void) {
    mb2_info_header_t *info = (mb2_info_header_t *)(uintptr_t)saved_mb_info_addr;
    uint8_t *ptr = (uint8_t *)info + sizeof(mb2_info_header_t);
    uint8_t *info_end = (uint8_t *)info + info->total_size;

    while (ptr < info_end) {
        mb2_tag_t *tag = (mb2_tag_t *)ptr;

        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }

        if (tag->type == MULTIBOOT2_TAG_MMAP) {
            mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
            uint8_t *entry_ptr = (uint8_t *)mmap->entries;
            uint8_t *mmap_end = (uint8_t *)tag + tag->size;

            while (entry_ptr < mmap_end) {
                mb2_mmap_entry_t *entry = (mb2_mmap_entry_t *)entry_ptr;

                console_write("  ");
                console_write_hex(entry->base_addr);
                console_write(" - ");
                console_write_hex(entry->base_addr + entry->length);
                console_write("  ");
                console_write(mmap_type_name(entry->type));
                console_write("\n");

                entry_ptr += mmap->entry_size;
            }
        }

        ptr += (tag->size + 7) & ~7u;
    }
}
