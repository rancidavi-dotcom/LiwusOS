#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

// Structs representing ACPI tables
typedef struct rsdp_descriptor {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) rsdp_t;

typedef struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct madt {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed)) madt_t;

// MADT record entry header
typedef struct madt_record {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) madt_record_t;

// MADT Record 0: Processor Local APIC
typedef struct madt_lapic_record {
    madt_record_t header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) madt_lapic_record_t;

// MADT Record 1: I/O APIC
typedef struct madt_ioapic_record {
    madt_record_t header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) madt_ioapic_record_t;

// MADT Record 2: Interrupt Source Override
typedef struct madt_iso_record {
    madt_record_t header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed)) madt_iso_record_t;

// LAPIC registers offsets
#define LAPIC_ID            0x0020
#define LAPIC_VER           0x0030
#define LAPIC_TPR           0x0080
#define LAPIC_EOI           0x00B0
#define LAPIC_LDR           0x00D0
#define LAPIC_DFR           0x00E0
#define LAPIC_SIVR          0x00F0
#define LAPIC_ICR_LOW       0x0300
#define LAPIC_ICR_HIGH      0x0310
#define LAPIC_LVT_TIMER     0x0320
#define LAPIC_LVT_LINT0     0x0350
#define LAPIC_LVT_LINT1     0x0360
#define LAPIC_LVT_ERR       0x0370
#define LAPIC_TIMER_INIT    0x0380
#define LAPIC_TIMER_CUR     0x0390
#define LAPIC_TIMER_DIV     0x03E0

void init_apic(void);
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t value);
void lapic_eoi(void);

void ioapic_write(uint32_t reg, uint32_t value);
void ioapic_route(uint8_t irq, uint8_t vector, uint8_t cpu_apic_id);

#endif
