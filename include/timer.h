#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Inicializa o timer com a frequência em Hz */
void init_timer(uint32_t frequency);

extern uint32_t timer_ticks;

#endif
