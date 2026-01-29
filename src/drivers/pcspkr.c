#include "pcspkr.h"
#include "io.h"
#include "timer.h"

void pcspkr_play_tone(uint32_t freq) {
    if (freq == 0) {
        pcspkr_stop();
        return;
    }
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6); // Command: Channel 2, LSB/MSB, Square Wave
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)(div >> 8));
    
    // Enable speaker
    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }
}

void pcspkr_stop(void) {
    uint8_t tmp = inb(0x61) & 0xFC;
    outb(0x61, tmp);
}

void pcspkr_beep(void) {
    pcspkr_play_tone(1000);
    uint32_t start = timer_ticks;
    while (timer_ticks < start + 10) { // 100ms
        asm volatile("hlt");
    }
    pcspkr_stop();
}

void pcspkr_play_melody(const note_t *notes, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        if (notes[i].frequency == NOTE_REST) {
            pcspkr_stop();
        } else {
            pcspkr_play_tone(notes[i].frequency);
        }
        
        uint32_t duration_ticks = notes[i].duration_ms / 10;
        if (duration_ticks == 0) duration_ticks = 1;
        
        uint32_t start = timer_ticks;
        while (timer_ticks < start + duration_ticks) {
            asm volatile("hlt");
        }
        
        // Pause between notes for articulation
        pcspkr_stop();
        start = timer_ticks;
        while (timer_ticks < start + 2) { // 20ms pause
            asm volatile("hlt");
        }
    }
}
