#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Inicializa o timer com a frequência em Hz */
void init_timer(uint32_t frequency);

extern uint32_t timer_ticks;
/* Monotonic 64-bit tick count, incremented by the BSP timer interrupt. */
extern volatile uint64_t timer_monotonic_ticks;

#endif
