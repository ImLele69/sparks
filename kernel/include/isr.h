#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss; // pushed by the cpu, always in long mode
} registers_t;

void isr_handler(registers_t *regs);

#endif
