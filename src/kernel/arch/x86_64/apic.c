#include "apic.h"
#include "vmm.h"
#include "kheap.h"
#include "task.h"
#include "serial.h"
#include "io.h"
#include "string.h"

static volatile uint32_t *lapic_base = NULL;
static uint64_t lapic_phys_base = 0;
static uint64_t ioapic_phys_base = 0;

uint8_t cpu_apic_ids[16] = {0};
int cpu_count = 0;

volatile uint64_t ap_pml4_val = 0;
volatile uint64_t ap_stack_val = 0;
volatile uint64_t ap_entry_val = 0;
volatile uint64_t ap_status = 0;

#include "gdt.h"

void ap_kernel_main(void) {
    uint32_t apic_id = lapic_read(LAPIC_ID) >> 24;
    int cpu_idx = 0;
    for (int i = 0; i < cpu_count; i++) {
        if (cpu_apic_ids[i] == apic_id) {
            cpu_idx = i;
            break;
        }
    }

    init_cpu_local(cpu_idx);
    init_gdt_cpu(cpu_idx); // Initialize local GDT/TSS for this core
    extern void tss_flush(void);
    tss_flush();

    serial_print("APIC: AP core (CPU ");
    char num_buf[4] = {'0' + cpu_idx, 0};
    serial_print(num_buf);
    serial_print(") initialized in 64-bit Long Mode successfully!\n");

    while (1) {
        asm volatile("hlt");
    }
}


// Read memory mapped LAPIC register
uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) return 0;
    return lapic_base[reg / 4];
}

// Write memory mapped LAPIC register
void lapic_write(uint32_t reg, uint32_t value) {
    if (!lapic_base) return;
    lapic_base[reg / 4] = value;
}

// Send End Of Interrupt (EOI) to LAPIC
void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

static uint8_t irq_overrides[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

void ioapic_write(uint32_t reg, uint32_t value) {
    if (ioapic_phys_base == 0) return;
    volatile uint32_t *ioapic = (volatile uint32_t *)ioapic_phys_base;
    ioapic[0] = reg & 0xFF;
    ioapic[4] = value;
}

uint32_t ioapic_read(uint32_t reg) {
    if (ioapic_phys_base == 0) return 0;
    volatile uint32_t *ioapic = (volatile uint32_t *)ioapic_phys_base;
    ioapic[0] = reg & 0xFF;
    return ioapic[4];
}

void ioapic_route(uint8_t irq, uint8_t vector, uint8_t cpu_apic_id) {
    uint32_t reg_low = 0x10 + 2 * irq;
    uint32_t reg_high = reg_low + 1;

    uint32_t value_low = vector & 0xFF;
    uint32_t value_high = ((uint32_t)cpu_apic_id) << 24;

    ioapic_write(reg_low, value_low);
    ioapic_write(reg_high, value_high);
}

// Helper to disable the legacy 8259 PIC
static void disable_pic(void) {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    serial_print("APIC: Legacy PIC disabled (masked all IRQs)\n");
}

// Validate ACPI table checksum
static bool validate_checksum(acpi_sdt_header_t *header) {
    uint8_t sum = 0;
    uint8_t *bytes = (uint8_t *)header;
    for (uint32_t i = 0; i < header->length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

// Find RSDP descriptor in physical memory
static rsdp_t *find_rsdp(void) {
    // 1. Scan EBDA segment (Extended BIOS Data Area)
    uint16_t ebda_segment = 0;
    asm volatile("movw (%%rbx), %0" : "=r"(ebda_segment) : "b"((uint64_t)0x40E) : "memory");
    uint64_t ebda_phys = ebda_segment << 4;
    if (ebda_phys > 0x400 && ebda_phys < 0xA0000) {
        for (uint64_t addr = ebda_phys; addr < ebda_phys + 1024; addr += 16) {
            rsdp_t *rsdp = (rsdp_t *)addr;
            if (memcmp(rsdp->signature, "RSD PTR ", 8) == 0) {
                uint8_t sum = 0;
                uint8_t *bytes = (uint8_t *)rsdp;
                for (int i = 0; i < 20; i++) sum += bytes[i];
                if (sum == 0) return rsdp;
            }
        }
    }

    // 2. Scan BIOS main read-only segment: 0xE0000 - 0xFFFFF
    for (uint64_t addr = 0xE0000; addr < 0xFFFFF; addr += 16) {
        rsdp_t *rsdp = (rsdp_t *)addr;
        if (memcmp(rsdp->signature, "RSD PTR ", 8) == 0) {
            uint8_t sum = 0;
            uint8_t *bytes = (uint8_t *)rsdp;
            for (int i = 0; i < 20; i++) sum += bytes[i];
            if (sum == 0) return rsdp;
        }
    }

    return NULL;
}

// Parse ACPI MADT structure
static void parse_madt(madt_t *madt) {
    lapic_phys_base = madt->local_apic_address;
    serial_print("APIC: Default LAPIC Physical Base: ");
    serial_print_hex(lapic_phys_base);
    serial_print("\n");

    uint64_t current = (uint64_t)madt + sizeof(madt_t);
    uint64_t end = (uint64_t)madt + madt->header.length;

    while (current < end) {
        madt_record_t *record = (madt_record_t *)current;
        if (record->length == 0) {
            serial_print("APIC: Error - invalid MADT record length 0\n");
            break;
        }

        if (record->type == 0) { // Processor Local APIC
            madt_lapic_record_t *lapic = (madt_lapic_record_t *)record;
            if (lapic->flags & 1) { // 1 = Enabled, or 2 = Online Capable
                if (cpu_count < 16) {
                    cpu_apic_ids[cpu_count] = lapic->apic_id;
                    serial_print("APIC: Found Processor Core ");
                    // Simple numeric trace
                    char core_buf[4] = {0};
                    core_buf[0] = '0' + cpu_count;
                    serial_print(core_buf);
                    serial_print(" (ACPI Proc ID: ");
                    core_buf[0] = '0' + lapic->acpi_processor_id;
                    serial_print(core_buf);
                    serial_print(", APIC ID: ");
                    core_buf[0] = '0' + lapic->apic_id;
                    serial_print(core_buf);
                    serial_print(")\n");
                    cpu_count++;
                }
            }
        } else if (record->type == 1) { // I/O APIC
            madt_ioapic_record_t *ioapic = (madt_ioapic_record_t *)record;
            ioapic_phys_base = ioapic->ioapic_address;
            serial_print("APIC: Found I/O APIC ID ");
            char id_buf[4] = {'0' + ioapic->ioapic_id, 0};
            serial_print(id_buf);
            serial_print(" Base: ");
            serial_print_hex(ioapic_phys_base);
            serial_print("\n");
        } else if (record->type == 2) { // Interrupt Source Override
            madt_iso_record_t *iso = (madt_iso_record_t *)record;
            if (iso->source < 16) {
                irq_overrides[iso->source] = iso->global_system_interrupt;
                serial_print("APIC: Found IRQ Override - Source ");
                char src_buf[4] = {'0' + iso->source, 0};
                serial_print(src_buf);
                serial_print(" -> GSI ");
                char gsi_buf[4] = {'0' + iso->global_system_interrupt, 0};
                serial_print(gsi_buf);
                serial_print("\n");
            }
        }

        current += record->length;
    }
}

// Master initialization routine
void init_apic(void) {
    serial_print("APIC: Starting ACPI search...\n");
    rsdp_t *rsdp = find_rsdp();
    if (!rsdp) {
        serial_print("APIC: Error - ACPI RSDP not found!\n");
        return;
    }

    serial_print("APIC: RSDP found at physical address: ");
    serial_print_hex((uint64_t)rsdp);
    serial_print("\n");

    acpi_sdt_header_t *rsdt = (acpi_sdt_header_t *)(uint64_t)rsdp->rsdt_address;
    if (!validate_checksum(rsdt)) {
        serial_print("APIC: Error - RSDT checksum validation failed!\n");
        return;
    }

    int entries = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
    uint32_t *table_pointers = (uint32_t *)((uint64_t)rsdt + sizeof(acpi_sdt_header_t));
    madt_t *madt = NULL;

    for (int i = 0; i < entries; i++) {
        acpi_sdt_header_t *header = (acpi_sdt_header_t *)(uint64_t)table_pointers[i];
        if (memcmp(header->signature, "APIC", 4) == 0) {
            if (validate_checksum(header)) {
                madt = (madt_t *)header;
                break;
            }
        }
    }

    if (!madt) {
        serial_print("APIC: Error - MADT table not found or corrupted!\n");
        return;
    }

    serial_print("APIC: MADT table found at: ");
    serial_print_hex((uint64_t)madt);
    serial_print("\n");

    // Parse structures
    parse_madt(madt);

    if (lapic_phys_base == 0) {
        serial_print("APIC: Error - LAPIC physical base not configured.\n");
        return;
    }

    // Disable legacy PIC
    disable_pic();

    // Map LAPIC MMIO and enable Local APIC
    lapic_base = (volatile uint32_t *)lapic_phys_base;
    vmm_map_page((void *)lapic_phys_base, (void *)lapic_phys_base, PTE_P | PTE_W | PTE_PCD);
    
    // Spurious Interrupt Vector Register (SIVR)
    // Bit 8 enables the LAPIC. Vector offset 0xFF maps spurious interrupts.
    lapic_write(LAPIC_SIVR, lapic_read(LAPIC_SIVR) | 0x1FF);

    // Map I/O APIC page
    if (ioapic_phys_base != 0) {
        vmm_map_page((void *)ioapic_phys_base, (void *)ioapic_phys_base, PTE_P | PTE_W | PTE_PCD);
        serial_print("APIC: Mapped I/O APIC page.\n");

        // Route IRQs
        ioapic_route(irq_overrides[0], 32, 0);  // PIT -> Vector 32
        ioapic_route(irq_overrides[1], 33, 0);  // Keyboard -> Vector 33
        ioapic_route(irq_overrides[11], 43, 0); // Network -> Vector 43
        ioapic_route(irq_overrides[12], 44, 0); // Mouse -> Vector 44
        serial_print("APIC: Routings set up in I/O APIC.\n");
    }

    serial_print("APIC: Local APIC successfully initialized and enabled!\n");

    // Boot other cores
    extern void boot_aps(void);
    boot_aps();
}

static void apic_delay(uint32_t count) {
    for (volatile uint32_t i = 0; i < count; i++) {
        asm volatile("pause");
    }
}

extern uint8_t trampoline_start[];
extern uint8_t trampoline_end[];

void boot_aps(void) {
    // 1. Copy trampoline code to 0x8000
    uint64_t trampoline_size = (uint64_t)trampoline_end - (uint64_t)trampoline_start;
    if (trampoline_size > 4096) {
        serial_print("APIC: Error - Trampoline size exceeds 4KB page!\n");
        return;
    }
    memcpy((void *)0x8000, trampoline_start, trampoline_size);
    serial_print("APIC: Copied trampoline to 0x8000\n");

    // 2. Set PML4 target address for APs
    uint64_t pml4;
    asm volatile("mov %%cr3, %0" : "=r"(pml4));
    ap_pml4_val = pml4;

    // 3. Waking APs
    uint32_t bsp_apic_id = lapic_read(LAPIC_ID) >> 24;

    for (int i = 0; i < cpu_count; i++) {
        uint8_t apic_id = cpu_apic_ids[i];
        if (apic_id == bsp_apic_id) {
            continue; // Skip the Bootstrap core (BSP)
        }

        serial_print("APIC: Sending INIT-SIPI sequence to AP core with APIC ID ");
        char id_buf[4] = {'0' + apic_id, 0};
        serial_print(id_buf);
        serial_print("...\n");

        // Allocate a dedicated kernel stack for the AP core
        uint64_t stack_size = 8192;
        void *stack_base = kmalloc(stack_size);
        uint64_t stack_top = (uint64_t)stack_base + stack_size;

        ap_stack_val = stack_top;
        ap_entry_val = (uint64_t)ap_kernel_main;
        ap_status = 0;

        // Send INIT IPI (Assert)
        lapic_write(LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
        lapic_write(LAPIC_ICR_LOW, 0x00004500); // Delivery Mode: INIT, Assert, Level

        apic_delay(20000000); // 10ms wait

        // Send STARTUP IPI (SIPI) pointing to page 0x08 (0x8000)
        lapic_write(LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
        lapic_write(LAPIC_ICR_LOW, 0x00004608); // Delivery Mode: Start Up, Vector: 0x08

        apic_delay(2000000); // 1ms wait

        // If core did not boot, send second SIPI as recommended by Intel manuals
        if (ap_status == 0) {
            lapic_write(LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
            lapic_write(LAPIC_ICR_LOW, 0x00004608);

            // Wait with a timeout for AP acknowledgment
            int timeout = 1000;
            while (ap_status == 0 && timeout > 0) {
                apic_delay(100000);
                timeout--;
            }
        }

        if (ap_status == 1) {
            serial_print("APIC: AP core (APIC ID ");
            serial_print(id_buf);
            serial_print(") booted and initialized successfully!\n");
        } else {
            serial_print("APIC: AP core (APIC ID ");
            serial_print(id_buf);
            serial_print(") FAILED to boot!\n");
            kfree(stack_base);
        }
    }
}
