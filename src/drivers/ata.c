#include "ata.h"
#include "io.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"

/* Retorna 0 se OK, -1 se houver erro ou timeout */
int ata_wait_bsy(uint16_t bus) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (!(inb(bus + ATA_REG_STATUS) & 0x80)) return 0;
    }
    return -1;
}

int ata_wait_drq(uint16_t bus) {
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t status = inb(bus + ATA_REG_STATUS);
        if (status & 0x08) return 0;
        if (status & 0x01) return -1; /* Erro no hardware */
    }
    return -1;
}

static void ata_io_delay(uint16_t bus) {
    for (int i = 0; i < 4; i++) inb(bus + ATA_REG_STATUS);
}

int ata_probe(uint16_t bus, uint8_t drive) {
    outb(bus + 6, drive);
    ata_io_delay(bus);

    outb(bus + 2, 0);
    outb(bus + 3, 0);
    outb(bus + 4, 0);
    outb(bus + 5, 0);
    outb(bus + 7, ATA_CMD_IDENTIFY);
    ata_io_delay(bus);

    uint8_t status = inb(bus + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) return -1;

    uint32_t timeout = 100000;
    while (timeout--) {
        status = inb(bus + ATA_REG_STATUS);
        if (status & 0x01) return -1;
        if (status & 0x08) {
            uint16_t discard[256];
            for (int i = 0; i < 256; i++) discard[i] = inw(bus);
            return 0;
        }
    }
    return -1;
}

int ata_find_first(uint16_t *bus_out, uint8_t *drive_out) {
    static const uint16_t buses[] = { ATA_PRIMARY, ATA_SECONDARY };
    static const uint8_t drives[] = { ATA_MASTER, ATA_SLAVE };

    for (int b = 0; b < 2; b++) {
        for (int d = 0; d < 2; d++) {
            if (ata_probe(buses[b], drives[d]) == 0) {
                if (bus_out) *bus_out = buses[b];
                if (drive_out) *drive_out = drives[d];
                serial_print("ata: found PIO disk bus=0x");
                serial_print_hex(buses[b]);
                serial_print(" drive=0x");
                serial_print_hex(drives[d]);
                serial_print("\n");
                return 0;
            }
        }
    }
    serial_print("ata: no legacy PIO disk found\n");
    return -1;
}

int ata_read_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer) {
    /* Seleciona o drive */
    outb(bus + 6, 0x40 | drive | ((lba >> 24) & 0x0F));
    
    /* Pequeno atraso para o hardware reagir */
    for(int i=0; i<100; i++) inb(bus + ATA_REG_STATUS);

    outb(bus + 2, 1);
    outb(bus + 3, (uint8_t)lba);
    outb(bus + 4, (uint8_t)(lba >> 8));
    outb(bus + 5, (uint8_t)(lba >> 16));
    outb(bus + 7, ATA_CMD_READ);

    if (ata_wait_bsy(bus) < 0) return -1;
    if (ata_wait_drq(bus) < 0) return -1;

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(bus);
    }
    return 0;
}

int ata_write_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer) {
    outb(bus + 6, 0x40 | drive | ((lba >> 24) & 0x0F));
    outb(bus + 2, 1);
    outb(bus + 3, (uint8_t)lba);
    outb(bus + 4, (uint8_t)(lba >> 8));
    outb(bus + 5, (uint8_t)(lba >> 16));
    outb(bus + 7, ATA_CMD_WRITE);

    if (ata_wait_bsy(bus) < 0) return -1;
    if (ata_wait_drq(bus) < 0) return -1;

    for (int i = 0; i < 256; i++) {
        outw(bus, buffer[i]);
    }

    /* Flush cache to persist writes before the next read/mount */
    outb(bus + 7, 0xE7);
    if (ata_wait_bsy(bus) < 0) return -1;
    return 0;
}

// ================================================================
// BM-IDE (Bus Master IDE) DMA for PIIX3
// ================================================================
#define PIIX3_VENDOR 0x8086
#define PIIX3_DEVICE 0x7000

#define BM_CMD_REG    0
#define BM_STATUS_REG 2
#define BM_PRDT_REG   4

#define BM_CMD_START  0x01
#define BM_CMD_READ   0x00
#define BM_CMD_WRITE  0x08

#define BM_STATUS_INT 0x01
#define BM_STATUS_ERR 0x02
#define BM_STATUS_DMA 0x04

#define ATA_CMD_READ_DMA  0xC8
#define ATA_CMD_WRITE_DMA 0xCA

typedef struct {
    uint32_t buf_addr;
    uint16_t byte_count;
    uint16_t eot;
} __attribute__((packed)) prdt_entry_t;

static int ata_bmide_present = 0;
static uint16_t ata_bmide_base = 0;
static prdt_entry_t *ata_prdt = NULL;
static uint8_t *ata_dma_buf = NULL;

int ata_bmide_init(void) {
    pci_device_t *dev = pci_get_device(PIIX3_VENDOR, PIIX3_DEVICE);
    if (!dev) {
        serial_print("ata: PIIX3 not found, BM-IDE unavailable\n");
        return -1;
    }

    uint32_t bar4 = pci_read_config(dev->bus, dev->device, dev->function, 0x20);
    if (!(bar4 & 1)) {
        serial_print("ata: BAR4 not I/O space\n");
        return -1;
    }
    ata_bmide_base = bar4 & 0xFFF0;

    uint32_t cmd = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x04; // Bus Master enable
    cmd |= 0x01; // I/O space enable
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, cmd);

    ata_prdt = (prdt_entry_t *)pmm_alloc_block();
    ata_dma_buf = (uint8_t *)pmm_alloc_block();
    if (!ata_prdt || !ata_dma_buf) {
        serial_print("ata: failed to alloc DMA pages\n");
        if (ata_prdt) pmm_free_block(ata_prdt);
        if (ata_dma_buf) pmm_free_block(ata_dma_buf);
        return -1;
    }

    ata_bmide_present = 1;
    serial_print("ata: BM-IDE DMA at I/O 0x");
    serial_print_hex(ata_bmide_base);
    serial_print("\n");
    return 0;
}

int ata_bmide_available(void) {
    return ata_bmide_present;
}

static int ata_bmide_transfer(uint32_t lba, uint8_t count, uint16_t *buffer, int write) {
    if (!ata_bmide_present) return -1;

    uint32_t size = (uint32_t)count * 512;

    if (write)
        memcpy(ata_dma_buf, buffer, size);

    ata_prdt[0].buf_addr = (uint32_t)ata_dma_buf;
    ata_prdt[0].byte_count = size - 1;
    ata_prdt[0].eot = 0x8000;

    outb(ATA_PRIMARY + 6, 0x40 | ATA_MASTER | ((lba >> 24) & 0x0F));
    for (int i = 0; i < 100; i++) inb(ATA_PRIMARY + ATA_REG_STATUS);

    outb(ATA_PRIMARY + 2, count);
    outb(ATA_PRIMARY + 3, (uint8_t)lba);
    outb(ATA_PRIMARY + 4, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY + 5, (uint8_t)(lba >> 16));

    outb(ata_bmide_base + BM_STATUS_REG, 0x04);
    outl(ata_bmide_base + BM_PRDT_REG, (uint32_t)ata_prdt);

    outb(ATA_PRIMARY + 7, write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);
    for (int i = 0; i < 100; i++) inb(ATA_PRIMARY + ATA_REG_STATUS);

    uint8_t bm_cmd = BM_CMD_START | (write ? BM_CMD_WRITE : BM_CMD_READ);
    outb(ata_bmide_base + BM_CMD_REG, bm_cmd);

    uint32_t timeout = 2000000;
    while (timeout--) {
        uint8_t s = inb(ata_bmide_base + BM_STATUS_REG);
        if (s & BM_STATUS_INT) break;
        if (s & BM_STATUS_ERR) {
            outb(ata_bmide_base + BM_CMD_REG, 0);
            return -1;
        }
    }

    outb(ata_bmide_base + BM_CMD_REG, 0);

    if (timeout == 0) return -1;
    if (inb(ATA_PRIMARY + ATA_REG_STATUS) & 0x01) return -1;

    if (!write)
        memcpy(buffer, ata_dma_buf, size);

    if (write) {
        outb(ATA_PRIMARY + 7, 0xE7);  // ATA CACHE FLUSH
        ata_wait_bsy(ATA_PRIMARY);
    }

    return 0;
}

int ata_bmide_read(uint32_t lba, uint8_t count, uint16_t *buffer) {
    return ata_bmide_transfer(lba, count, buffer, 0);
}

int ata_bmide_write(uint32_t lba, uint8_t count, uint16_t *buffer) {
    return ata_bmide_transfer(lba, count, buffer, 1);
}
