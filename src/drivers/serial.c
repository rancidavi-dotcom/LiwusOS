#include "io.h"
#include "spinlock.h"
#include <stdarg.h>

#define PORT 0x3f8          // COM1

static spinlock_t serial_lock = {0};
static int serial_present = 0;
static int serial_input_enabled = 1;

/* Serial output is also used by the scheduler and exception paths. On the
 * single CPU, do not let a timer IRQ switch to another task while that task
 * owns serial_lock; the scheduler may itself print before the owner resumes. */
static inline uint64_t serial_irq_save(void) {
   uint64_t flags;
   __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
   return flags;
}

static inline void serial_irq_restore(uint64_t flags) {
   if (flags & (1ULL << 9))
      __asm__ volatile("sti" ::: "memory");
}

int init_serial() {
   serial_present = 0;
   outb(PORT + 1, 0x00);    // Disable all interrupts
   outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(PORT + 1, 0x00);    //                  (hi byte)
   outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   /* A missing legacy UART commonly reads as 0xff on real hardware. Verify
    * the 16550 internal loopback before accepting COM1 as a stdin source. */
   outb(PORT + 4, 0x1E);    // Loopback, RTS/OUT1/OUT2 asserted
   outb(PORT + 0, 0xAE);
   if (inb(PORT + 0) != 0xAE) {
      outb(PORT + 4, 0x00);
      return -1;
   }
   outb(PORT + 4, 0x0B);    // Normal mode, IRQ gate/RTS/DTR set
   serial_present = 1;
   return 0;
}

void serial_set_input_enabled(int enabled) {
   serial_input_enabled = enabled != 0;
}

int is_transmit_empty() {
   return inb(PORT + 5) & 0x20;
}

int serial_received() {
   return serial_present && serial_input_enabled && (inb(PORT + 5) & 1);
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
   if (!serial_present) return;
   while (is_transmit_empty() == 0);
   outb(PORT, a);
}

void serial_print(const char *str) {
    uint64_t flags = serial_irq_save();
    spinlock_acquire(&serial_lock);
    while (*str) {
        write_serial(*str++);
    }
    spinlock_release(&serial_lock);
    serial_irq_restore(flags);
}

// Simple helper for hex debug
void serial_print_hex(uint64_t n) {
    uint64_t flags = serial_irq_save();
    spinlock_acquire(&serial_lock);
    char *digits = "0123456789ABCDEF";
    write_serial('0');
    write_serial('x');
    for (int i = 60; i >= 0; i -= 4) {
        write_serial(digits[(n >> i) & 0xF]);
    }
    spinlock_release(&serial_lock);
    serial_irq_restore(flags);
}

void serial_printf(const char *fmt, ...) {
    uint64_t flags = serial_irq_save();
    spinlock_acquire(&serial_lock);

    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s) write_serial(*s++);
            } else if (*fmt == 'd' || *fmt == 'i') {
                int val = va_arg(args, int);
                if (val < 0) {
                    write_serial('-');
                    val = -val;
                }
                char buf[12];
                int i = 0;
                do {
                    buf[i++] = (char)('0' + (val % 10));
                    val /= 10;
                } while (val && i < 11);
                while (i > 0) write_serial(buf[--i]);
            } else if (*fmt == 'u') {
                unsigned int val = va_arg(args, unsigned int);
                char buf[12];
                int i = 0;
                do {
                    buf[i++] = (char)('0' + (val % 10));
                    val /= 10;
                } while (val && i < 11);
                while (i > 0) write_serial(buf[--i]);
            } else if (*fmt == 'x' || *fmt == 'X') {
                unsigned int val = va_arg(args, unsigned int);
                char buf[10];
                int i = 0;
                char *hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                do {
                    buf[i++] = hex[val & 0xF];
                    val >>= 4;
                } while (val && i < 9);
                while (i > 0) write_serial(buf[--i]);
            } else if (*fmt == 'c') {
                char c = (char)va_arg(args, int);
                write_serial(c);
            } else if (*fmt == '%') {
                write_serial('%');
            } else {
                write_serial('%');
                write_serial(*fmt);
            }
        } else {
            write_serial(*fmt);
        }
        fmt++;
    }

    va_end(args);
    spinlock_release(&serial_lock);
    serial_irq_restore(flags);
}
