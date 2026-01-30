#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* Barramentos IDE */
#define ATA_PRIMARY      0x1F0
#define ATA_SECONDARY    0x170

/* Seleção de Drive */
#define ATA_MASTER       0xA0
#define ATA_SLAVE        0xB0

/* Comandos e Status */
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_REG_STATUS   0x07
#define ATA_REG_COMMAND  0x07

void ata_identify(uint16_t bus, uint8_t drive);
int  ata_read_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer);
void ata_write_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer);

#endif