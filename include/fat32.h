#ifndef FAT32_H
#define FAT32_H

#include "vfs.h"
#include <stdint.h>

// FAT32 Boot Sector
typedef struct {
  uint8_t BS_JmpBoot[3];
  uint8_t BS_OEMName[8];
  uint16_t BPB_BytsPerSec;
  uint8_t BPB_SecPerClus;
  uint16_t BPB_RsvdSecCnt;
  uint8_t BPB_NumFATs;
  uint16_t BPB_RootEntCnt;
  uint16_t BPB_TotSec16;
  uint8_t BPB_Media;
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
  uint8_t BPB_Reserved[12];
  uint8_t BS_DrvNum;
  uint8_t BS_Reserved1;
  uint8_t BS_BootSig;
  uint32_t BS_VolID;
  uint8_t BS_VolLab[11];
  uint8_t BS_FilSysType[8];
  uint8_t BS_BootCode[420];
  uint16_t BS_SigA;
} __attribute__((packed)) fat32_boot_sector_t;

typedef struct {
  uint32_t FSI_LeadSig;
  uint8_t FSI_Reserved1[480];
  uint32_t FSI_StrucSig;
  uint32_t FSI_Free_Clusters;
  uint32_t FSI_Nxt_Free;
  uint8_t FSI_Reserved2[12];
  uint32_t FSI_TrailSig;
} __attribute__((packed)) fat32_fsinfo_t;

typedef struct {
  uint8_t DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t DIR_NTRes;
  uint8_t DIR_CrtTimeTenth;
  uint16_t DIR_CrtTime;
  uint16_t DIR_CrtDate;
  uint16_t DIR_LstAccDate;
  uint16_t DIR_FstClusHI;
  uint16_t DIR_WrtTime;
  uint16_t DIR_WrtDate;
  uint16_t DIR_FstClusLO;
  uint32_t DIR_FileSize;
} __attribute__((packed)) fat32_directory_entry_t;

fs_node_t *fat32_mount(uint16_t bus, uint8_t drive,
                       uint32_t partition_lba_start);
int fat32_format(int *progress);
int fat32_is_mounted(void);

// Write API
int fat32_create_file(const char *name); // Create in root
uint32_t fat32_write_file(const char *name, uint8_t *buffer, uint32_t size);
int fat32_root_list_entry(int index, char *name_out, int *is_dir_out,
                          uint32_t *size_out);
void *fat32_read_file(const char *name, uint32_t *size_out);
int fat32_delete_file(const char *name);
int fat32_rename_file(const char *old_name, const char *new_name);

// Path-based API
int fat32_list_dir_entry(const char *path, int index, char *name_out,
                         int *is_dir_out, uint32_t *size_out);
void *fat32_read_file_path(const char *path, uint32_t *size_out);
int fat32_create_file_path(const char *path);
uint32_t fat32_write_file_path(const char *path, uint8_t *buffer,
                               uint32_t size);
int fat32_delete_path(const char *path);
int fat32_rename_path(const char *old_path, const char *new_path);
int fat32_create_dir(const char *path);
int fat32_path_info(const char *path, int *is_dir_out, uint32_t *size_out);

#endif
