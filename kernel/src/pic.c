#include <stdint.h>

#include "io.h"
#include "pic.h"

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_remap(void) {
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, 0x20); // irq 0-7 at 0x20
    outb(PIC2_DATA, 0x28); // irq 8-15 at 0x28

    outb(PIC1_DATA, 0x04); // slave on IRQ2
    outb(PIC2_DATA, 0x02); // cascade identity

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // everything masked until a driver asks for its line
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }

    outb(PIC1_CMD, 0x20);
}

void pic_set_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq < 8 ? irq : (uint8_t)(irq - 8);

    outb(port, (uint8_t)(inb(port) | (1 << bit)));
}

void pic_clear_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq < 8 ? irq : (uint8_t)(irq - 8);

    outb(port, (uint8_t)(inb(port) & ~(1 << bit)));
}
