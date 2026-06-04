#include <stdint.h>
#include "isr.h"

// Stubs e Redirecionamentos para o Linker
static int graphics_exclusive_owner = -1;

int graphics_exclusive_active(void) {
    return graphics_exclusive_owner >= 0;
}

void graphics_exclusive_acquire(int pid) {
    graphics_exclusive_owner = pid;
}

void graphics_exclusive_release(int pid) {
    if (graphics_exclusive_owner == pid || pid < 0) {
        graphics_exclusive_owner = -1;
    }
}

// O load_elf deve estar no elf.c, vou garantir que ele não seja static lá.
// O register_interrupt_handler deve estar no idt.c ou isr.c.

// Vou declarar aqui apenas o que realmente está falhando como undefined.
