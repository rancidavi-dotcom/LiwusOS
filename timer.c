#include "timer.h"
#include "io.h"

uint32_t timer_ticks = 0;

void timer_handler() {
    timer_ticks++;
    uint16_t* video = (uint16_t*) 0xB8000;
    char* symbols = "/-\\|";
    video[79] = (video[79] & 0xFF00) | symbols[(timer_ticks / 5) % 4];
}

void init_timer(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}