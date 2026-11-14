#include "ata.h"
#include "io.h"
#include "kernel_layout.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"

static inline uint16_t ata_ctrl_port(uint16_t bus) {
    if (bus == ATA_PRIMARY) return 0x3F6;
    if (bus == ATA_SECONDARY) return 0x376;
    return 0;
}

static inline void ata_disable_irq(uint16_t bus) {
    uint16_t ctrl = ata_ctrl_port(bus);
    if (ctrl) outb(ctrl, 0x02); /* nIEN = 1: disable IDE IRQ 14/15 */
}

static void ata_io_delay(uint16_t bus) {
    for (int i = 0; i < 4; i++) (void)inb(bus + ATA_REG_STATUS);
}

/* Retorna 0 se OK, -1 se houver erro ou timeout */
int ata_wait_bsy(uint16_t bus) {
    uint32_t timeout = 300000;
    while (timeout--) {
        uint8_t st = inb(bus + ATA_REG_STATUS);
        if (st == 0xFF || st == 0x7F) return -1;
        if (!(st & 0x80)) return 0;
    }
    return -1;
}

int ata_wait_drq(uint16_t bus) {
    uint32_t timeout = 300000;
    while (timeout--) {
        uint8_t status = inb(bus + ATA_REG_STATUS);
        if (status == 0xFF || status == 0x7F) return -1;
        if (status & (0x01 | 0x20)) return -1; /* ERR or DF */
        if (!(status & 0x80) && (status & 0x08)) return 0;
    }
    return -1;
}

/* When set, writes defer the ATA CACHE FLUSH until ata_flush_cache() is
 * called. This avoids a flush per sector during bulk installs. */
static int ata_defer_flush = 0;
static int ata_flush_pending = 0;
static uint16_t ata_flush_bus = ATA_PRIMARY;

void ata_set_writeback(int enable) {
    ata_defer_flush = enable ? 1 : 0;
}

int ata_get_writeback(void) {
    return ata_defer_flush;
}

int ata_flush_cache(void) {
    if (!ata_flush_pending) return 0;
    uint16_t bus = ata_flush_bus ? ata_flush_bus : ATA_PRIMARY;
    ata_disable_irq(bus);
    if (ata_wait_bsy(bus) == 0) {
        outb(bus + ATA_REG_COMMAND, 0xE7);
        ata_io_delay(bus);
        ata_wait_bsy(bus);
    }
    ata_flush_pending = 0;
    return 0;
}

int ata_probe(uint16_t bus, uint8_t drive) {
    ata_disable_irq(bus);
    outb(bus + 6, drive);
    ata_io_delay(bus);

    uint8_t init_st = inb(bus + ATA_REG_STATUS);
    if (init_st == 0xFF || init_st == 0x7F) return -1;
    if (init_st & 0x80) {
        if (ata_wait_bsy(bus) < 0) return -1;
    }

    outb(bus + 2, 0);
    outb(bus + 3, 0);
    outb(bus + 4, 0);
    outb(bus + 5, 0);
    outb(bus + 7, ATA_CMD_IDENTIFY);
    ata_io_delay(bus);

    uint8_t status = inb(bus + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF || status == 0x7F) return -1;

    uint32_t timeout = 200000;
    while (timeout--) {
        status = inb(bus + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF) return -1;
        if (!(status & 0x80)) break;
    }
    if (status & 0x80) return -1;

    /* Reject ATAPI (0x14/0xEB), SATAPI (0x69/0x96), or floating bus (0xFF/0xFF) */
    uint8_t cl = inb(bus + 4);
    uint8_t ch = inb(bus + 5);
    if ((cl == 0x14 && ch == 0xEB) || (cl == 0x69 && ch == 0x96) ||
        (cl == 0xFF && ch == 0xFF)) {
        return -1;
    }

    timeout = 200000;
    while (timeout--) {
        status = inb(bus + ATA_REG_STATUS);
        if (status & (0x01 | 0x20)) return -1;
        if (!(status & 0x80) && (status & 0x08)) {
            uint16_t w0 = 0;
            for (int i = 0; i < 256; i++) {
                uint16_t w = inw(bus);
                if (i == 0) w0 = w;
            }
            if (w0 == 0xFFFF || w0 == 0x7F7F) return -1;
            return 0;
        }
    }
    return -1;
}

uint32_t ata_identify_sectors(uint16_t bus, uint8_t drive) {
    ata_disable_irq(bus);
    outb(bus + 6, drive);
    ata_io_delay(bus);
    if (ata_wait_bsy(bus) < 0) return 0;

    outb(bus + 2, 0);
    outb(bus + 3, 0);
    outb(bus + 4, 0);
    outb(bus + 5, 0);
    outb(bus + 7, ATA_CMD_IDENTIFY);
    ata_io_delay(bus);

    uint8_t status = inb(bus + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF || status == 0x7F) return 0;

    uint32_t timeout = 200000;
    while (timeout--) {
        status = inb(bus + ATA_REG_STATUS);
        if (status & (0x01 | 0x20)) return 0;
        if (!(status & 0x80) && (status & 0x08)) {
            uint16_t id[256];
            for (int i = 0; i < 256; i++) id[i] = inw(bus);
            if (id[0] == 0xFFFF || id[0] == 0x7F7F) return 0;
            uint32_t lba28 = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
            if (lba28 == 0 && (id[83] & (1 << 10))) {
                uint64_t lba48 = (uint64_t)id[100] |
                                 ((uint64_t)id[101] << 16) |
                                 ((uint64_t)id[102] << 32) |
                                 ((uint64_t)id[103] << 48);
                lba28 = (lba48 > 0x0FFFFFFFULL) ? 0x0FFFFFFFU : (uint32_t)lba48;
            }
            return lba28;
        }
    }
    return 0;
}

int ata_find_first(uint16_t *bus_out, uint8_t *drive_out) {
    /* Ensure any PCI IDE controller (e.g. Intel H61 0x8086:0x1c00) has I/O space
     * enabled and legacy PCI INTx disabled so PIO polling cannot cause an IRQ storm. */
    int n_pci = pci_get_device_count();
    uint16_t extra_buses[4] = {0, 0, 0, 0};
    int n_extra = 0;
    for (int i = 0; i < n_pci; i++) {
        pci_device_t *d = pci_get_device_by_index(i);
        if (d && d->class_id == 0x01 && d->subclass_id == 0x01) {
            uint32_t cmd = pci_read_config(d->bus, d->device, d->function, 0x04);
            cmd |= (1u << 0) | (1u << 2) | (1u << 10); /* I/O + BusMaster + INTx Disable */
            pci_write_config(d->bus, d->device, d->function, 0x04, cmd);
            uint32_t bar0 = pci_read_config(d->bus, d->device, d->function, 0x10);
            uint32_t bar2 = pci_read_config(d->bus, d->device, d->function, 0x18);
            if ((bar0 & 1) && (bar0 & 0xFFFC) > 0x100 && (bar0 & 0xFFFC) != ATA_PRIMARY && n_extra < 4)
                extra_buses[n_extra++] = (uint16_t)(bar0 & 0xFFFC);
            if ((bar2 & 1) && (bar2 & 0xFFFC) > 0x100 && (bar2 & 0xFFFC) != ATA_SECONDARY && n_extra < 4)
                extra_buses[n_extra++] = (uint16_t)(bar2 & 0xFFFC);
        }
    }

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
    for (int b = 0; b < n_extra; b++) {
        for (int d = 0; d < 2; d++) {
            if (ata_probe(extra_buses[b], drives[d]) == 0) {
                if (bus_out) *bus_out = extra_buses[b];
                if (drive_out) *drive_out = drives[d];
                return 0;
            }
        }
    }
    serial_print("ata: no legacy PIO disk found\n");
    return -1;
}

int ata_read_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer) {
    ata_disable_irq(bus);
    if (ata_wait_bsy(bus) < 0) return -1;

    /* Seleciona o drive */
    outb(bus + 6, 0x40 | drive | ((lba >> 24) & 0x0F));
    ata_io_delay(bus);
    if (ata_wait_bsy(bus) < 0) return -1;

    outb(bus + 2, 1);
    outb(bus + 3, (uint8_t)lba);
    outb(bus + 4, (uint8_t)(lba >> 8));
    outb(bus + 5, (uint8_t)(lba >> 16));
    outb(bus + 7, ATA_CMD_READ);
    ata_io_delay(bus);

    if (ata_wait_bsy(bus) < 0) return -1;
    if (ata_wait_drq(bus) < 0) return -1;

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(bus);
    }
    ata_io_delay(bus);
    return 0;
}

int ata_write_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer) {
    ata_disable_irq(bus);
    if (ata_wait_bsy(bus) < 0) return -1;

    outb(bus + 6, 0x40 | drive | ((lba >> 24) & 0x0F));
    ata_io_delay(bus);
    if (ata_wait_bsy(bus) < 0) return -1;

    outb(bus + 2, 1);
    outb(bus + 3, (uint8_t)lba);
    outb(bus + 4, (uint8_t)(lba >> 8));
    outb(bus + 5, (uint8_t)(lba >> 16));
    outb(bus + 7, ATA_CMD_WRITE);
    ata_io_delay(bus);

    if (ata_wait_bsy(bus) < 0) return -1;
    if (ata_wait_drq(bus) < 0) return -1;

    for (int i = 0; i < 256; i++) {
        outw(bus, buffer[i]);
    }

    /* CRITICAL on real hardware: wait 400ns and wait for BSY=0 after the 256
     * data words so the controller finishes draining the PIO buffer before the
     * next sector's command registers are written! */
    ata_io_delay(bus);
    if (ata_wait_bsy(bus) < 0) return -1;

    /* Flush cache to persist writes before the next read/mount */
    if (ata_defer_flush) {
        ata_flush_pending = 1;
        ata_flush_bus = bus;
    } else {
        outb(bus + 7, 0xE7);
        ata_io_delay(bus);
        if (ata_wait_bsy(bus) < 0) return -1;
    }
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

    ata_prdt[0].buf_addr = (uint32_t)virt_to_phys(ata_dma_buf);
    ata_prdt[0].byte_count = size - 1;
    ata_prdt[0].eot = 0x8000;

    outb(ATA_PRIMARY + 6, 0x40 | ATA_MASTER | ((lba >> 24) & 0x0F));
    for (int i = 0; i < 100; i++) inb(ATA_PRIMARY + ATA_REG_STATUS);

    outb(ATA_PRIMARY + 2, count);
    outb(ATA_PRIMARY + 3, (uint8_t)lba);
    outb(ATA_PRIMARY + 4, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY + 5, (uint8_t)(lba >> 16));

    outb(ata_bmide_base + BM_STATUS_REG, 0x04);
    outl(ata_bmide_base + BM_PRDT_REG, (uint32_t)virt_to_phys(ata_prdt));

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
        if (ata_defer_flush) {
            ata_flush_pending = 1;
            ata_flush_bus = ATA_PRIMARY;
        } else {
            outb(ATA_PRIMARY + 7, 0xE7);  // ATA CACHE FLUSH
            ata_wait_bsy(ATA_PRIMARY);
        }
    }

    return 0;
}

int ata_bmide_read(uint32_t lba, uint8_t count, uint16_t *buffer) {
    return ata_bmide_transfer(lba, count, buffer, 0);
}

int ata_bmide_write(uint32_t lba, uint8_t count, uint16_t *buffer) {
    return ata_bmide_transfer(lba, count, buffer, 1);
}

int ata_write_sectors_raw(uint16_t bus, uint8_t drive, uint32_t lba, uint32_t count, const uint8_t *buf) {
    int prev_defer = ata_defer_flush;
    ata_set_writeback(1);
    uint32_t offset = 0;
    while (count > 0) {
        if (ata_bmide_present && bus == ATA_PRIMARY && drive == ATA_MASTER && count >= 8) {
            uint8_t chunk = (count > 8) ? 8 : (uint8_t)count;
            if (ata_bmide_write(lba, chunk, (uint16_t *)(buf + offset)) == 0) {
                lba += chunk;
                offset += (uint32_t)chunk * 512;
                count -= chunk;
                continue;
            }
        }
        if (ata_write_sector(bus, drive, lba, (uint16_t *)(buf + offset)) != 0) {
            ata_set_writeback(prev_defer);
            return -1;
        }
        lba++;
        offset += 512;
        count--;
    }
    ata_flush_cache();
    ata_set_writeback(prev_defer);
    return 0;
}
