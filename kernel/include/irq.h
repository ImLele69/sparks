#ifndef IRQ_H
#define IRQ_H

#include "isr.h"

typedef void (*irq_handler_fn)(registers_t *regs);

void irq_register_handler(uint8_t irq, irq_handler_fn handler);
void irq_handler(registers_t *regs);

#endif
