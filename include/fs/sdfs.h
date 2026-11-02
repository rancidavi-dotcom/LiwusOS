#ifndef SDFS_H
#define SDFS_H

#include <stdint.h>
#include "vfs.h"

#define SDFS_MAGIC_V1    "SDFS0010"
#define SDFS_MAGIC_V2    "SDFS0020"
#define SDFS_MAGIC       SDFS_MAGIC_V2
#define SDFS_FORMAT_VER  2

#define SDFS_BLOCK_SIZE  4096
#define SDFS_SECTOR_SIZE 512
#define SDFS_SECTORS_PER_BLOCK (SDFS_BLOCK_SIZE / SDFS_SECTOR_SIZE)

#define SDFS_TYPE_FREE   0
#define SDFS_TYPE_FILE   1
#define SDFS_TYPE_DIR    2
#define SDFS_TYPE_DEL    3

#define SDFS_MAX_NAME    255

/* Default journal: 16 blocks = 64KB */
#define SDFS_DEFAULT_JOURNAL_BLOCKS 16

/* Default max mounts before forced fsck */
#define SDFS_DEFAULT_MAX_MOUNTS 100

/* Hard cap on addressable filesystem size. Keeps the in-RAM bitmap small
 * (4M blocks * 4096 = 16 GiB, bitmap = 512 KiB = 128 blocks) so that mount
 * and first-boot install stay fast and memory-bounded on real disks. */
#define SDFS_MAX_BLOCKS (4u * 1024u * 1024u)

/* Permission bits use the conventional 9-bit owner/group/other layout. */
#define SDFS_PERM_READ   0004
#define SDFS_PERM_WRITE  0002
#define SDFS_PERM_EXEC   0001
#define SDFS_PERM_RUSR   0400
#define SDFS_PERM_WUSR   0200
#define SDFS_PERM_XUSR   0100
#define SDFS_PERM_RGRP   0040
#define SDFS_PERM_WGRP   0020
#define SDFS_PERM_XGRP   0010
#define SDFS_PERM_ROTH   0004
#define SDFS_PERM_WOTH   0002
#define SDFS_PERM_XOTH   0001

#define SDFS_DEFAULT_PERM 0644
#define SDFS_DEFAULT_DIR_PERM 0755

/* Superblock V2 layout (block 0) */
typedef struct {
    uint8_t  magic[8];          /* "SDFS0020" */
    uint32_t format_version;    /* 2 */
    uint32_t block_size;        /* 4096 */
    uint32_t total_blocks;
    uint32_t bitmap_block;      /* first bitmap block */
    uint32_t bitmap_blocks;     /* how many blocks for bitmap */
    uint32_t journal_start;     /* first journal block */
    uint32_t journal_blocks;    /* number of journal blocks */
    uint32_t root_dir_block;
    uint32_t mount_count;
    uint32_t max_mounts;        /* trigger fsck after this */
    uint32_t created_time;      /* timer_ticks at format */
    uint32_t superblock_crc;    /* CRC32 of everything before this field */
    uint8_t  pad[4048];
} __attribute__((packed)) sdfs_super_t;

/* Superblock V1 layout (backward compat for read-only) */
typedef struct {
    uint8_t  magic[8];
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t bitmap_block;
    uint32_t bitmap_blocks;
    uint32_t root_dir_block;
    uint8_t  pad[4064];
} __attribute__((packed)) sdfs_super_v1_t;

typedef struct {
    uint32_t next_block;
    uint16_t entry_count;
    uint8_t  data[4090];
} __attribute__((packed)) sdfs_dir_block_t;

/* Directory entry V2: with uid/gid/permissions/timestamps */
typedef struct {
    uint8_t  type;
    uint8_t  name_len;
    uint8_t  name[255];
    uint32_t start_block;
    uint32_t file_size;
    uint32_t created_time;
    uint32_t modified_time;
    uint16_t uid;
    uint16_t gid;
    uint16_t permissions;
    uint16_t reserved;
} __attribute__((packed)) sdfs_entry_v2_t;

/* Legacy entry for V1 compat */
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

/* Internal entry used by sdfs.c (matches V2) */
typedef struct {
    uint8_t  type;
    uint8_t  name_len;
    char     name[256];
    uint32_t start_block;
    uint32_t file_size;
    uint32_t created_time;
    uint32_t modified_time;
    uint16_t uid;
    uint16_t gid;
    uint16_t permissions;
    uint32_t block;          /* which block contains this entry */
    uint16_t block_offset;   /* offset within the block */
} sdfs_entry_t;

#define SDFS_DIRENT_SIZE(ent) ((uint32_t)(2 + (ent)->name_len + 24))

/* Public API */
fs_node_t *sdfs_mount(uint16_t bus, uint8_t drive, uint32_t partition_lba);
int sdfs_mount_failed_corrupt(void);
int sdfs_format(void);
int sdfs_is_mounted(void);

/* Path-based operations */
int sdfs_create_file(const char *path);
int sdfs_create_file_as(const char *path, uint16_t uid, uint16_t gid,
                        uint16_t permissions);
uint32_t sdfs_write_file(const char *path, uint8_t *buffer, uint32_t size);
void *sdfs_read_file(const char *path, uint32_t *size_out);
int sdfs_create_dir(const char *path);
int sdfs_create_dir_as(const char *path, uint16_t uid, uint16_t gid,
                       uint16_t permissions);
int sdfs_delete(const char *path);
int sdfs_rename(const char *old_path, const char *new_path);
int sdfs_path_info(const char *path, int *is_dir_out, uint32_t *size_out);
int sdfs_list_dir_entry(const char *path, int index, char *name_out,
                        int *is_dir_out, uint32_t *size_out);
void sdfs_get_usage(uint32_t *total_blocks, uint32_t *used_blocks);

/* V2: permissions */
int sdfs_set_permissions(const char *path, uint16_t perm);
int sdfs_get_permissions(const char *path, uint16_t *perm);
int sdfs_get_metadata(const char *path, uint16_t *uid, uint16_t *gid,
                      uint16_t *permissions);

/* V2: fsck */
int sdfs_fsck(void);

/* V2: journal stats */
int sdfs_journal_dirty(void);
int sdfs_journal_entry_count(void);

/* Raw block I/O (used by tests and fsck) */
int sdfs_read_raw_block(uint32_t block, uint8_t *buffer);
int sdfs_write_raw_block(uint32_t block, uint8_t *buffer);

/* Flush the pending in-RAM allocation bitmap to disk */
int sdfs_flush_bitmap(void);

#ifdef KERNEL_TEST
/* Fail one physical SDFS block write to simulate an I/O interruption. */
void sdfs_test_fail_next_write(uint32_t block);
#endif

#endif
void sdfs_enable_ramdisk(uint32_t mb);
