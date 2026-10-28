#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_init(uint32_t frequency_hz);
uint64_t pit_get_ticks(void);
uint64_t pit_get_uptime_ms(void);
uint32_t pit_get_frequency(void);

#endif
