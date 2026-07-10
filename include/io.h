#ifndef IO_H
#define IO_H

#include <stdint.h>

/* Envia um byte para uma porta de I/O */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

/* Lê um byte de uma porta de I/O */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ( "outw %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ( "inw %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ( "outl %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ( "inl %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void sys_reboot() {
    /* Tenta o reset amigável via 8042 */
    outb(0x64, 0xFE);
    
    /* Se falhar, força um Triple Fault (Carrega uma IDT vazia e gera interrupção) */
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idt_ptr = {0, 0};
    asm volatile("lidt %0; int $3" : : "m"(idt_ptr));
}

static inline void sys_shutdown() {
    /* Tenta desligamento via portas QEMU / Bochs */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    
    /* Tenta desligamento via VirtualBox */
    outw(0x4004, 0x3400);
    
    /* Se tudo falhar, reinicia a máquina */
    sys_reboot();
}

/* Pequeno atraso para portas de hardware lentas (como o teclado antigo) */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif
