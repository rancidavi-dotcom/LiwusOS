#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "vfs.h"

// Estrutura do Boot Sector do FAT32 (BIOS Parameter Block)
typedef struct {
    uint8_t  BS_JmpBoot[3];
    uint8_t  BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t  BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t  BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t  BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;
    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    uint8_t  BPB_Reserved[12];
    uint8_t  BS_DrvNum;
    uint8_t  BS_Reserved1;
    uint8_t  BS_BootSig;
    uint32_t BS_VolID;
    uint8_t  BS_VolLab[11];
    uint8_t  BS_FilSysType[8];
    uint8_t  BS_BootCode[420];
    uint16_t BS_SigA; // Assinatura 0xAA55
} __attribute__((packed)) fat32_boot_sector_t;

// Estrutura do FSInfo do FAT32
typedef struct {
    uint32_t FSI_LeadSig;
    uint8_t  FSI_Reserved1[480];
    uint32_t FSI_StrucSig;
    uint32_t FSI_Free_Clusters;
    uint32_t FSI_Nxt_Free;
    uint8_t  FSI_Reserved2[12];
    uint32_t FSI_TrailSig;
} __attribute__((packed)) fat32_fsinfo_t;

// Estrutura de uma entrada de diretório FAT32
typedef struct {
    char name[8];
    char ext[3];
    uint8_t attrib;
    uint8_t reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_cluster_low;
    uint32_t size;
} __attribute__((packed)) fat32_dir_entry_t;

// Atributos de arquivo
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

// Formata um drive com FAT32 (versão bloqueante que reporta progresso)
int fat32_format(int* progress);

// Monta um sistema de arquivos FAT32
fs_node_t* fat32_mount(uint16_t bus, uint8_t drive, uint32_t partition_lba_start);

#endif // FAT32_H

