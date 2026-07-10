#include "io.h"
#include <stdarg.h>

#define PORT 0x3f8          // COM1

int init_serial() {
   outb(PORT + 1, 0x00);    // Disable all interrupts
   outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(PORT + 1, 0x00);    //                  (hi byte)
   outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
   return 0;
}

int is_transmit_empty() {
   return inb(PORT + 5) & 0x20;
}

int serial_received() {
   return inb(PORT + 5) & 1;
}

char read_serial() {
   while (serial_received() == 0);
   return inb(PORT);
}

int serial_pop_char(char *c) {
   if (serial_received()) {
       *c = inb(PORT);
       return 1;
   }
   return 0;
}

void write_serial(char a) {
   while (is_transmit_empty() == 0);
   outb(PORT, a);
}

void serial_print(const char *str) {
    while (*str) {
        write_serial(*str++);
    }
}

// Simple helper for hex debug
void serial_print_hex(uint64_t n) {
    char *digits = "0123456789ABCDEF";
    serial_print("0x");
    for (int i = 60; i >= 0; i -= 4) {
        write_serial(digits[(n >> i) & 0xF]);
    }
}
