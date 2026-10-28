#include "irq.h"
#include "pic.h"

static irq_handler_fn handlers[16];

void irq_register_handler(uint8_t irq, irq_handler_fn handler) {
    handlers[irq] = handler;
}

void irq_handler(registers_t *regs) {
    uint8_t irq = (uint8_t)(regs->vector - 32);

    // EOI before the handler, not after: IRQ0 may switch tasks and not come
    // back here until some later tick, which the PIC would never deliver while
    // it's still waiting for this EOI
    pic_send_eoi(irq);

    if (handlers[irq]) {
        handlers[irq](regs);
    }
}
