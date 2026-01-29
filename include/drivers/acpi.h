#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>
#include "apic.h" /* reutiliza rsdp_t e acpi_sdt_header_t */

/* RSDP (v2) completo, com XSDT estendido */
typedef struct {
  rsdp_t v1;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} __attribute__((packed)) rsdp2_t;

typedef struct {
  acpi_sdt_header_t header;
  uint64_t reserved;
} __attribute__((packed)) xsdt_t;

/* MCFG (PCI Express Enhanced Config) */
typedef struct {
  acpi_sdt_header_t header;
  uint64_t base_address;
  uint16_t pci_segment;
  uint8_t start_bus;
  uint8_t end_bus;
  uint32_t reserved;
} __attribute__((packed)) mcfg_t;

/* Fornece o RSDP copiado pelo Multiboot2 (tags 14/15). */
void acpi_set_rsdp(uint64_t phys);

/* Descobre o RSDP (se não fornecido) e prepara XSDT/RSDT. */
int acpi_init(void);

/* Procura uma tabela pela assinatura de 4 caracteres. */
acpi_sdt_header_t *acpi_find_table(const char *signature);

/* Power management (Fase 3). */
bool acpi_poweroff(void);
bool acpi_reboot(void);

/* Diagnóstico */
bool acpi_available(void);

#endif
