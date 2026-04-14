#include <stdint.h>
#include "isr.h"

// Stubs e Redirecionamentos para o Linker
int graphics_exclusive_active(void) {
    return 0; // Por padrão, não exclusivo
}

void graphics_exclusive_release(int pid) {
    (void)pid;
}

// O load_elf deve estar no elf.c, vou garantir que ele não seja static lá.
// O register_interrupt_handler deve estar no idt.c ou isr.c.

// Vou declarar aqui apenas o que realmente está falhando como undefined.
