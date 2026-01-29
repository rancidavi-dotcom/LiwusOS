#include "ata.h"
#include "string.h"

void format_fat32(uint32_t total_sectors) {
    (void)total_sectors;
    uint8_t sector[512];
    for(int i=0; i<512; i++) sector[i] = 0;
    
    const char* boot_msg = "LiwusOS: Instalado";
    for(size_t i=0; i<strlen(boot_msg); i++) sector[i] = boot_msg[i];

    sector[510] = 0x55; 
    sector[511] = 0xAA;

    /* Grava no Primary Master por padrão */
    ata_write_sector(ATA_PRIMARY, ATA_MASTER, 0, (uint16_t*)sector);
}
