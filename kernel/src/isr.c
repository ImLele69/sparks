#include "isr.h"
#include "serial.h"

static const char *const exception_messages[32] = {
    "Divide-by-zero Error",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved",
    "Reserved",
};

void isr_handler(registers_t *regs) {
    serial_write("\n*** ");
    serial_write(exception_messages[regs->vector]);
    serial_write(" ***\n  vector ");
    serial_write_dec(regs->vector);
    serial_write("  err ");
    serial_write_hex(regs->error_code);
    serial_write("\n  rip ");
    serial_write_hex(regs->rip);
    serial_write("  rsp ");
    serial_write_hex(regs->rsp);
    serial_write("\n");

    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
