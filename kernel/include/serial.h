#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *str);
void serial_write_hex(uint64_t val);
void serial_write_dec(uint64_t val);

#endif
