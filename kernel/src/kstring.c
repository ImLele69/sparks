#include "kstring.h"

size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int kstreq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void kstrlcpy(char *dst, const char *src, size_t size) {
    size_t i = 0;

    if (size == 0) {
        return;
    }
    for (; src[i] && i + 1 < size; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

char *fmt_hex(char buf[19], uint64_t val) {
    static const char digits[] = "0123456789ABCDEF";

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(val >> ((15 - i) * 4)) & 0xF];
    }
    buf[18] = '\0';
    return buf;
}

char *fmt_dec(char buf[21], uint64_t val) {
    char *p = buf + 20;

    *p = '\0';
    do {
        *--p = (char)('0' + val % 10);
        val /= 10;
    } while (val);
    return p;
}
