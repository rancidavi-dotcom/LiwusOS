#include "ata.h"
#include "io.h"

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

void ata_write_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer) {
    outb(bus + 6, 0x40 | drive | ((lba >> 24) & 0x0F));
    outb(bus + 2, 1);
    outb(bus + 3, (uint8_t)lba);
    outb(bus + 4, (uint8_t)(lba >> 8));
    outb(bus + 5, (uint8_t)(lba >> 16));
    outb(bus + 7, ATA_CMD_WRITE);

    if (ata_wait_bsy(bus) < 0) return;
    if (ata_wait_drq(bus) < 0) return;

    for (int i = 0; i < 256; i++) {
        outw(bus, buffer[i]);
    }

    /* Flush cache to persist writes before the next read/mount */
    outb(bus + 7, 0xE7);
    ata_wait_bsy(bus);
}
