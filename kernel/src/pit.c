#include <stdint.h>

#include "io.h"
#include "irq.h"
#include "pic.h"
#include "pit.h"
#include "task.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_FREQUENCY 1193182u

static volatile uint64_t ticks = 0;
static uint32_t pit_frequency = 1;

static void pit_irq_handler(registers_t *regs) {
    (void)regs;
    ticks++;
    task_yield();
}

void pit_init(uint32_t frequency_hz) {
    pit_frequency = frequency_hz;

    uint16_t divisor = (uint16_t)(PIT_BASE_FREQUENCY / frequency_hz);

    outb(PIT_COMMAND, 0x36); // ch0, lo/hi byte, mode 3
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register_handler(0, pit_irq_handler);
    pic_clear_mask(0);
}

uint64_t pit_get_ticks(void) {
    return ticks;
}

uint64_t pit_get_uptime_ms(void) {
    return (ticks * 1000) / pit_frequency;
}

uint32_t pit_get_frequency(void) {
    return pit_frequency;
}
