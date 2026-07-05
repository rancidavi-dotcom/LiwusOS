#ifndef SDFS_H
#define SDFS_H

#include <stdint.h>
#include "vfs.h"

#define SDFS_MAGIC      "SDFS0010"
#define SDFS_BLOCK_SIZE  4096
#define SDFS_SECTOR_SIZE 512
#define SDFS_SECTORS_PER_BLOCK (SDFS_BLOCK_SIZE / SDFS_SECTOR_SIZE)  // 8

#define SDFS_TYPE_FREE   0
#define SDFS_TYPE_FILE   1
#define SDFS_TYPE_DIR    2
#define SDFS_TYPE_DEL    3

#define SDFS_MAX_NAME    255

typedef struct {
  uint8_t magic[8];
  uint32_t block_size;
  uint32_t total_blocks;
  uint32_t bitmap_block;
  uint32_t bitmap_blocks;
  uint32_t root_dir_block;
  uint8_t pad[4064];
} __attribute__((packed)) sdfs_super_t;

typedef struct {
  uint32_t next_block;
  uint16_t entry_count;
  uint8_t data[4090];
} __attribute__((packed)) sdfs_dir_block_t;

typedef struct {
  uint8_t type;
  uint8_t name_len;
  uint8_t name[255];
  uint32_t start_block;
  uint32_t file_size;
  uint32_t timestamp;
} __attribute__((packed)) sdfs_dirent_t;

typedef struct {
  uint32_t next_block;
  uint8_t data[4092];
} __attribute__((packed)) sdfs_data_block_t;

#define SDFS_DIRENT_SIZE(ent) ((uint32_t)(2 + (ent)->name_len + 12))

// Public API
fs_node_t *sdfs_mount(uint16_t bus, uint8_t drive, uint32_t partition_lba);
int sdfs_format(void);
int sdfs_is_mounted(void);

// Path-based operations (used by terminal)
int sdfs_create_file(const char *path);
uint32_t sdfs_write_file(const char *path, uint8_t *buffer, uint32_t size);
void *sdfs_read_file(const char *path, uint32_t *size_out);
int sdfs_create_dir(const char *path);
int sdfs_delete(const char *path);
int sdfs_rename(const char *old_path, const char *new_path);
int sdfs_path_info(const char *path, int *is_dir_out, uint32_t *size_out);
int sdfs_list_dir_entry(const char *path, int index, char *name_out,
                        int *is_dir_out, uint32_t *size_out);

#endif
void sdfs_enable_ramdisk(uint32_t mb);
