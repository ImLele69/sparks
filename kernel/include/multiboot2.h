#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include <stdint.h>

#define MULTIBOOT2_TAG_END 0
#define MULTIBOOT2_TAG_MMAP 6
#define MULTIBOOT2_TAG_FRAMEBUFFER 8

#define MULTIBOOT2_FRAMEBUFFER_RGB 1

#define MULTIBOOT2_MEMORY_AVAILABLE 1
#define MULTIBOOT2_MEMORY_RESERVED 2
#define MULTIBOOT2_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT2_MEMORY_NVS 4
#define MULTIBOOT2_MEMORY_BADRAM 5

typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t reserved;
} mb2_info_header_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

typedef struct __attribute__((packed)) {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} mb2_mmap_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    mb2_mmap_entry_t entries[];
} mb2_tag_mmap_t;

// the spec says reserved is 8 bits, GRUB uses 16. GRUB wins, color fields start at offset 32
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t fb_type;
    uint16_t reserved;
    uint8_t red_pos;
    uint8_t red_size;
    uint8_t green_pos;
    uint8_t green_size;
    uint8_t blue_pos;
    uint8_t blue_size;
} mb2_tag_framebuffer_t;

#endif
