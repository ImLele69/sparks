#include <stddef.h>

#include "io.h"
#include "kstring.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); // no interrupts
    outb(COM1 + 3, 0x80); // DLAB on to set the divisor
    outb(COM1 + 0, 0x03); // 38400 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); // 8N1, DLAB off
    outb(COM1 + 2, 0xC7); // FIFO on, cleared, 14 byte threshold
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) {
    }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *str) {
    for (; *str; str++) {
        if (*str == '\n') {
            serial_putc('\r');
        }
        serial_putc(*str);
    }
}

void serial_write_hex(uint64_t val) {
    char buf[19];
    serial_write(fmt_hex(buf, val));
}

void serial_write_dec(uint64_t val) {
    char buf[21];
    serial_write(fmt_dec(buf, val));
}
