#include "timer.h"
#include "io.h"

uint32_t timer_ticks = 0;

extern void keyboard_update_mouse(void);

void timer_handler() {
    timer_ticks++;
    keyboard_update_mouse();
}

void init_timer(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}
