#ifndef KSTRING_H
#define KSTRING_H

#include <stddef.h>
#include <stdint.h>

size_t kstrlen(const char *s);
int kstreq(const char *a, const char *b);
// always terminates dst, truncating if needed
void kstrlcpy(char *dst, const char *src, size_t size);

// both format into buf and return a pointer somewhere inside it
char *fmt_hex(char buf[19], uint64_t val);
char *fmt_dec(char buf[21], uint64_t val);

#endif
