#include "gdt.h"
#include "string.h"

extern void gdt_flush(uint32_t);
extern void tss_flush(); // Assembly (definido em boot.s)

gdt_entry_t gdt_entries[6];
gdt_ptr_t gdt_ptr;
tss_entry_t tss_entry;

/* Configura uma entrada na GDT */
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access,
                  uint8_t gran) {
  gdt_entries[num].base_low = (base & 0xFFFF);
  gdt_entries[num].base_middle = (base >> 16) & 0xFF;
  gdt_entries[num].base_high = (base >> 24) & 0xFF;

  gdt_entries[num].limit_low = (limit & 0xFFFF);
  gdt_entries[num].granularity = (limit >> 16) & 0x0F;

  gdt_entries[num].granularity |= gran & 0xF0;
  gdt_entries[num].access = access;
}

/* Configura a entrada da TSS */
void write_tss(int32_t num, uint16_t ss0, uint32_t esp0) {
  uint32_t base = (uint32_t)&tss_entry;
  uint32_t limit =
      sizeof(tss_entry); // Tamanho exato, sem -1? GDT limit usually is size-1.

  /* Descriptor TSS na GDT */
  /* Access 0xE9 = Present(1), DPL(3), System(0), Type(1001=32bit avail TSS) */
  /* Nota: Se DPL=3, user mode pode ver? Mas Intel recomenda DPL=0 para TSS
     descriptor? Vamos testar 0xE9 (Ring 3) como planejado. Se der erro de
     proteção ao carregar LTR, mudamos. Mas LTR roda em Ring 0, então ele acessa
     qualquer DPL >= CPL.
  */
  gdt_set_gate(num, base, limit, 0xE9, 0x00);

  memset(&tss_entry, 0, sizeof(tss_entry));

  tss_entry.ss0 = ss0;
  tss_entry.esp0 = esp0;

  // Configuração de segmentos para quando estivermos na task (se fosse hardware
  // switching) Mas para software switching o importante é SS0/ESP0 para voltar
  // ao kernel.
  tss_entry.cs = 0x0b;
  tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs =
      0x13;
  tss_entry.iomap_base = sizeof(tss_entry);
}

void init_gdt() {
  gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
  gdt_ptr.base = (uint32_t)&gdt_entries;

  gdt_set_gate(0, 0, 0, 0, 0);                /* Null segment */
  gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Code segment (Kernel) */
  gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Data segment (Kernel) */
  gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* Code segment (User) */
  gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* Data segment (User) */

  /* Configura TSS na entrada 5 */
  /* Inicialmente ESP0 = 0. Será atualizado a cada troca de task */
  write_tss(5, 0x10, 0x0);

  gdt_flush((uint32_t)&gdt_ptr);
  tss_flush(); // Carrega TR
}
