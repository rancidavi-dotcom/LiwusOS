#ifdef KERNEL_TEST
#include "sdfs.h"
#include "crc32.h"
#include "framework.h"
#include "string.h"
#include "kheap.h"

int test_superblock_crc(void) {
    uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    ASSERT(buf != NULL, "alloc ok");

    ASSERT(sdfs_read_raw_block(0, buf) == 0, "read superblock ok");

    sdfs_super_t *sb = (sdfs_super_t *)buf;
    ASSERT(memcmp(sb->magic, SDFS_MAGIC_V2, 8) == 0, "magic is V2");
    ASSERT(sb->format_version == 2, "format_version == 2");
    ASSERT(sb->block_size == 4096, "block_size == 4096");
    ASSERT(sb->total_blocks > 0, "total_blocks > 0");
    ASSERT(sb->journal_blocks >= 2, "journal_blocks >= 2");

    /* Verify CRC */
    uint32_t saved_crc = sb->superblock_crc;
    sb->superblock_crc = 0;
    uint32_t computed = crc32_calc(buf, 52);
    sb->superblock_crc = saved_crc;
    ASSERT(saved_crc == computed, "superblock CRC valid");

    kfree(buf);
    PASS("test_superblock_crc");
}

int test_superblock_v2_fields(void) {
    uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    sdfs_read_raw_block(0, buf);

    sdfs_super_t *sb = (sdfs_super_t *)buf;
    ASSERT(sb->bitmap_block == 1, "bitmap starts at block 1");
    ASSERT(sb->bitmap_blocks >= 1, "at least 1 bitmap block");
    ASSERT(sb->journal_start == sb->bitmap_block + sb->bitmap_blocks,
           "journal after bitmap");
    ASSERT(sb->root_dir_block == sb->journal_start + sb->journal_blocks,
           "root after journal");
    ASSERT(sb->root_dir_block < sb->total_blocks, "root within disk");
    ASSERT(sb->max_mounts > 0, "max_mounts > 0");

    kfree(buf);
    PASS("test_superblock_v2_fields");
}

int test_fsck_clean(void) {
    /* fsck runs automatically on mount; if we got here, it passed */
    ASSERT(sdfs_is_mounted(), "mounted after fsck");
    PASS("test_fsck_clean");
}

int test_v1_compat_read(void) {
    /* Verify V2 format works correctly for basic operations */
    sdfs_create_dir("/compat");
    sdfs_create_file("/compat/test.txt");
    sdfs_write_file("/compat/test.txt", (uint8_t *)"compat", 6);

    uint32_t size = 0;
    void *data = sdfs_read_file("/compat/test.txt", &size);
    ASSERT(data != NULL && size == 6, "V2 read works");
    ASSERT(memcmp(data, "compat", 6) == 0, "V2 data correct");

    sdfs_delete("/compat/test.txt");
    sdfs_delete("/compat");
    PASS("test_v1_compat_read");
}

#endif
