#include "sdfs.h"
#include "journal.h"
#include "crc32.h"
#include "vfs.h"
#include "ata.h"
#include "ahci.h"
#include "usb_msc.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "spinlock.h"
#include "sleep_mutex.h"

static int sdfs_mounted = 0;
static int sdfs_corrupt_mount = 0;
static const char *sdfs_mount_err_msg = "OK";
static uint16_t ata_bus;
static uint8_t ata_drive;
static uint32_t sdfs_partition_lba = 0;
static uint32_t sdfs_partition_total_sectors = 0;
static int sdfs_is_usb_msc = 0;

const char *sdfs_last_mount_error(void) {
    return sdfs_mount_err_msg ? sdfs_mount_err_msg : "erro desconhecido";
}

void sdfs_set_usb_partition(uint32_t start_lba, uint32_t total_sectors) {
    sdfs_partition_lba = start_lba;
    sdfs_partition_total_sectors = total_sectors;
    sdfs_is_usb_msc = 1;
}

uint32_t sdfs_get_partition_lba(void) {
    return sdfs_partition_lba;
}

static uint32_t disk_total_blocks;
static uint32_t disk_bitmap_block;
static uint32_t disk_bitmap_blocks;
static uint32_t disk_root_block;
static uint32_t disk_journal_start;
static uint32_t disk_journal_blocks;
static uint32_t disk_mount_count;
static uint32_t disk_max_mounts;
static uint32_t disk_format_version;
static uint8_t *bitmap_cache = NULL;
static uint32_t bitmap_bytes;
static uint32_t bitmap_dirty_ops = 0;
static sleep_mutex_t metadata_write_lock = SLEEP_MUTEX_INITIALIZER;
/* SDFS currently has one allocation bitmap and one journal. Serialize its
 * metadata and data-chain mutations across sleeping block I/O. */
static sleep_mutex_t sdfs_mutation_lock = SLEEP_MUTEX_INITIALIZER;

#ifdef KERNEL_TEST
static uint32_t sdfs_fail_write_block = UINT32_MAX;

void sdfs_test_fail_next_write(uint32_t block) {
    sdfs_fail_write_block = block;
}
#endif

/* Flush the allocation bitmap only every N block allocations/frees instead
 * of on every single operation. Each flush writes the whole bitmap, so doing
 * it per-block is catastrophic on a large disk. */
#define SDFS_BITMAP_FLUSH_OPS 256

static void sdfs_free_block(uint32_t block);
static uint32_t sdfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t sdfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static struct dirent *sdfs_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *sdfs_finddir(fs_node_t *node, const char *name);
static fs_node_t *sdfs_vfs_create(fs_node_t *dir_node, const char *name, uint32_t flags);
static fs_node_t *sdfs_vfs_create_as(fs_node_t *dir_node, const char *name,
                                    uint32_t flags, uint16_t uid, uint16_t gid,
                                    uint16_t mode);
static fs_node_t *sdfs_vfs_mkdir(fs_node_t *dir_node, const char *name,
                                uint16_t uid, uint16_t gid, uint16_t mode);
static int sdfs_vfs_unlink(fs_node_t *dir_node, const char *name);
static int sdfs_vfs_rmdir(fs_node_t *dir_node, const char *name);
static int sdfs_vfs_rename(fs_node_t *old_dir, const char *old_name,
                           fs_node_t *new_dir, const char *new_name);
static int sdfs_sync(fs_node_t *node, int data_only);
static struct dirent sdfs_dirent;

static uint8_t sdfs_ahci_port = 0xFF;
static uint8_t *sdfs_ramdisk = NULL;

/* The installer reserves LBA 0..65535 for GRUB and its boot filesystem.
 * Keep these raw operations anchored to the device selected by sdfs_mount(),
 * so an OTA never guesses which disk to overwrite. */
int sdfs_read_hd_boot_region(uint32_t lba, uint32_t sectors, uint8_t *buffer) {
    return sdfs_read_hd_boot_region_progress(lba, sectors, buffer, NULL, NULL);
}

int sdfs_read_hd_boot_region_progress(uint32_t lba, uint32_t sectors, uint8_t *buffer,
                                      sdfs_region_progress_fn progress_cb, void *user_data) {
    if (!buffer || sectors == 0 || sdfs_ramdisk || sdfs_is_usb_msc ||
        !sdfs_mounted || sdfs_partition_lba != SDFS_HD_PARTITION_LBA ||
        lba >= SDFS_HD_PARTITION_LBA || sectors > SDFS_HD_PARTITION_LBA - lba) {
        return -1;
    }
    uint32_t total = sectors;
    uint32_t completed = 0;
    while (sectors) {
        uint32_t chunk = sectors > 8 ? 8 : sectors;
        if (sdfs_ahci_port != 0xFF) {
            if (!ahci_read_sector(sdfs_ahci_port, lba, chunk, buffer)) return -1;
        } else {
            for (uint32_t i = 0; i < chunk; i++) {
                if (ata_read_sector(ata_bus, ata_drive, lba + i,
                                    (uint16_t *)(buffer + (size_t)i * SDFS_SECTOR_SIZE)) != 0) return -1;
            }
        }
        lba += chunk;
        sectors -= chunk;
        completed += chunk;
        buffer += (size_t)chunk * SDFS_SECTOR_SIZE;
        if (progress_cb) progress_cb(completed, total, user_data);
    }
    return 0;
}

int sdfs_write_hd_boot_region(uint32_t lba, uint32_t sectors, const uint8_t *buffer) {
    return sdfs_write_hd_boot_region_progress(lba, sectors, buffer, NULL, NULL);
}

int sdfs_write_hd_boot_region_progress(uint32_t lba, uint32_t sectors, const uint8_t *buffer,
                                       sdfs_region_progress_fn progress_cb, void *user_data) {
    if (!buffer || sectors == 0 || sdfs_ramdisk || sdfs_is_usb_msc ||
        !sdfs_mounted || sdfs_partition_lba != SDFS_HD_PARTITION_LBA ||
        lba >= SDFS_HD_PARTITION_LBA || sectors > SDFS_HD_PARTITION_LBA - lba) {
        return -1;
    }
    uint32_t total = sectors;
    uint32_t completed = 0;
    if (sdfs_ahci_port != 0xFF) {
        while (sectors) {
            uint32_t chunk = sectors > 8 ? 8 : sectors;
            if (!ahci_write_sector(sdfs_ahci_port, lba, chunk, (uint8_t *)buffer)) return -1;
            lba += chunk;
            sectors -= chunk;
            completed += chunk;
            buffer += (size_t)chunk * SDFS_SECTOR_SIZE;
            if (progress_cb) progress_cb(completed, total, user_data);
        }
        return ahci_flush_cache(sdfs_ahci_port) ? 0 : -1;
    }
    ata_set_writeback(1);
    for (uint32_t i = 0; i < sectors; i++) {
        if (ata_write_sector(ata_bus, ata_drive, lba + i,
                             (uint16_t *)(buffer + (size_t)i * SDFS_SECTOR_SIZE)) != 0) {
            ata_set_writeback(0);
            return -1;
        }
        completed = i + 1;
        if (progress_cb) progress_cb(completed, total, user_data);
    }
    ata_set_writeback(0);
    return ata_flush_cache();
}

typedef struct sdfs_open_ref {
    uint32_t start_block;
    uint32_t size;
    uint32_t references;
    uint8_t is_dir;
    uint8_t unlinked;
    struct sdfs_open_ref *next;
} sdfs_open_ref_t;

static sdfs_open_ref_t *sdfs_open_refs;
static spinlock_t sdfs_open_ref_lock = {0};

static uint32_t sdfs_chain_cache_start = 0;
static uint32_t sdfs_chain_cache_idx = 0;
static uint32_t sdfs_chain_cache_phys = 0;
static uint8_t *sdfs_chain_cache_ramdisk = NULL;

static inline void sdfs_chain_cache_invalidate(void) {
    sdfs_chain_cache_start = 0;
    sdfs_chain_cache_idx = 0;
    sdfs_chain_cache_phys = 0;
    sdfs_chain_cache_ramdisk = NULL;
}

static int sdfs_open(fs_node_t *node);
static void sdfs_close(fs_node_t *node);
static int sdfs_mark_open_unlinked(uint32_t start_block, uint32_t size,
                                   int is_dir);
static void sdfs_free_unlinked_chain(uint32_t start_block, uint32_t size,
                                     int is_dir);

void sdfs_enable_ramdisk(uint32_t mb) {
    sdfs_chain_cache_invalidate();
    uint64_t bytes = mb * 1024 * 1024;
    sdfs_ramdisk = (uint8_t *)kmalloc_a(bytes);
    if (!sdfs_ramdisk) {
        serial_print("SDFS: Failed to allocate ramdisk!\n");
        return;
    }
    disk_total_blocks = bytes / SDFS_BLOCK_SIZE;
    serial_print("SDFS: Ramdisk enabled!\n");
}

int sdfs_switch_to_hd_partition(uint16_t bus, uint8_t drive, uint8_t ahci_port,
                                uint32_t start_lba, uint32_t total_sectors) {
    sdfs_chain_cache_invalidate();
    if (bitmap_cache) {
        kfree(bitmap_cache);
        bitmap_cache = NULL;
    }
    sdfs_ramdisk = NULL;
    sdfs_is_usb_msc = 0;
    sdfs_mounted = 0;
    sdfs_corrupt_mount = 0;
    ata_bus = bus;
    ata_drive = drive;
    sdfs_ahci_port = ahci_port;
    sdfs_partition_lba = start_lba;
    sdfs_partition_total_sectors = total_sectors;
    return 0;
}

static void sdfs_check_ahci() {
    if (sdfs_ramdisk) return;
    if (sdfs_ahci_port == 0xFF) {
        if (ahci_find_first(&sdfs_ahci_port) == 0) {
            serial_print("SDFS: Using AHCI port\n");
        }
    }
}

/* ============================================================
 * Low-level block I/O
 * ============================================================ */

static int sdfs_read_block(uint32_t block, uint8_t *buffer) {
    if (sdfs_ramdisk) {
        if (block >= disk_total_blocks) return -1;
        memcpy(buffer, sdfs_ramdisk + block * SDFS_BLOCK_SIZE, SDFS_BLOCK_SIZE);
        return 0;
    }
    uint32_t lba = sdfs_partition_lba + block * SDFS_SECTORS_PER_BLOCK;
    if (sdfs_is_usb_msc) {
        return (usb_msc_read_sectors(lba, SDFS_SECTORS_PER_BLOCK, buffer) == 0) ? 0 : -1;
    }
    if (sdfs_ahci_port != 0xFF) {
        return ahci_read_sector(sdfs_ahci_port, lba, SDFS_SECTORS_PER_BLOCK, buffer) ? 0 : -1;
    }
    if (ata_bmide_available() && ata_bus == ATA_PRIMARY && ata_drive == ATA_MASTER) {
        return ata_bmide_read(lba, SDFS_SECTORS_PER_BLOCK, (uint16_t *)buffer);
    } else {
        for (int i = 0; i < SDFS_SECTORS_PER_BLOCK; i++) {
            if (ata_read_sector(ata_bus, ata_drive, lba + i, (uint16_t *)(buffer + i * SDFS_SECTOR_SIZE)) != 0) return -1;
        }
    }
    return 0;
}

static int sdfs_flush_device(void) {
    if (sdfs_ramdisk) return 0;
    if (sdfs_is_usb_msc) return 0;
    if (ata_get_writeback()) return 0;
    if (sdfs_ahci_port != 0xFF)
        return ahci_flush_cache(sdfs_ahci_port) ? 0 : -1;
    return ata_flush_cache();
}

static int sdfs_write_block(uint32_t block, uint8_t *buffer) {
    sdfs_chain_cache_invalidate();
#ifdef KERNEL_TEST
    if (block == sdfs_fail_write_block) {
        sdfs_fail_write_block = UINT32_MAX;
        return -1;
    }
#endif
    if (sdfs_ramdisk) {
        if (block >= disk_total_blocks) return -1;
        memcpy(sdfs_ramdisk + block * SDFS_BLOCK_SIZE, buffer, SDFS_BLOCK_SIZE);
        return 0;
    }
    uint32_t lba = sdfs_partition_lba + block * SDFS_SECTORS_PER_BLOCK;
    if (sdfs_is_usb_msc) {
        return (usb_msc_write_sectors(lba, SDFS_SECTORS_PER_BLOCK, buffer) == 0) ? 0 : -1;
    }
    if (sdfs_ahci_port != 0xFF) {
        return ahci_write_sector(sdfs_ahci_port, lba, SDFS_SECTORS_PER_BLOCK, buffer) ? 0 : -1;
    }
    if (ata_bmide_available() && ata_bus == ATA_PRIMARY && ata_drive == ATA_MASTER) {
        return ata_bmide_write(lba, SDFS_SECTORS_PER_BLOCK, (uint16_t *)buffer);
    } else {
        for (int i = 0; i < SDFS_SECTORS_PER_BLOCK; i++) {
            if (ata_write_sector(ata_bus, ata_drive, lba + i, (uint16_t *)(buffer + i * SDFS_SECTOR_SIZE)) != 0) return -1;
        }
    }
    return 0;
}

static int sdfs_zero_block(uint32_t block) {
    uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!buf) return -1;
    memset(buf, 0, SDFS_BLOCK_SIZE);
    int ret = sdfs_write_block(block, buf);
    kfree(buf);
    return ret;
}

static int sdfs_metadata_write_blocks(uint32_t *blocks, uint8_t **buffers,
                                      uint32_t count) {
    if (!blocks || !buffers || count == 0 ||
        count > SDFS_JOURNAL_MAX_ENTRIES) return -1;

    if (sleep_mutex_lock(&metadata_write_lock) != 0) return -1;
    int ret = -1;
    if (sdfs_journal_begin() != 0) goto out;
    for (uint32_t i = 0; i < count; i++) {
        if (!buffers[i] ||
            sdfs_journal_write_block(blocks[i], buffers[i]) != 0) {
            sdfs_journal_abort();
            goto out;
        }
    }
    if (sdfs_journal_commit() != 0) goto out;

    for (uint32_t i = 0; i < count; i++) {
        if (sdfs_write_block(blocks[i], buffers[i]) != 0) goto out;
    }
    if (sdfs_flush_device() != 0 || sdfs_journal_clean() != 0) goto out;
    ret = 0;
out:
    sleep_mutex_unlock(&metadata_write_lock);
    return ret;
}

/*
 * Journal-aware metadata write.
 * Journal the intended block content first, then write to disk.
 * After a successful flush to disk, clear the journal so a subsequent
 * mount does not re-apply already-persisted blocks.
 */
static int sdfs_metadata_write(uint32_t block, uint8_t *buffer) {
    uint32_t blocks[1] = { block };
    uint8_t *buffers[1] = { buffer };
    return sdfs_metadata_write_blocks(blocks, buffers, 1);
}

/* ============================================================
 * Bitmap operations
 * ============================================================ */

static uint8_t bitmap_block_dirty[128];

static int sdfs_load_bitmap(void) {
    if (bitmap_cache) { kfree(bitmap_cache); bitmap_cache = NULL; }
    bitmap_bytes = disk_bitmap_blocks * SDFS_BLOCK_SIZE;
    bitmap_cache = (uint8_t *)kmalloc(bitmap_bytes);
    if (!bitmap_cache) return -1;
    for (uint32_t i = 0; i < disk_bitmap_blocks; i++) {
        if (sdfs_read_block(disk_bitmap_block + i,
                            bitmap_cache + i * SDFS_BLOCK_SIZE) != 0) {
            kfree(bitmap_cache);
            bitmap_cache = NULL;
            return -1;
        }
    }
    memset(bitmap_block_dirty, 0, sizeof(bitmap_block_dirty));
    bitmap_dirty_ops = 0;
    return 0;
}

static int sdfs_save_bitmap(void) {
    if (!bitmap_cache) return -1;
    for (uint32_t i = 0; i < disk_bitmap_blocks; i++) {
        if (i < sizeof(bitmap_block_dirty) && !bitmap_block_dirty[i]) continue;
        if (sdfs_write_block(disk_bitmap_block + i,
                             bitmap_cache + i * SDFS_BLOCK_SIZE) != 0)
            return -1;
        if (i < sizeof(bitmap_block_dirty)) bitmap_block_dirty[i] = 0;
    }
    if (sdfs_flush_device() != 0) return -1;
    bitmap_dirty_ops = 0;
    return 0;
}

int sdfs_flush_bitmap(void) {
    if (!bitmap_cache) return -1;
    if (bitmap_dirty_ops == 0) return sdfs_flush_device();
    return sdfs_save_bitmap();
}

static void sdfs_bitmap_touch(void) {
    if (++bitmap_dirty_ops >= SDFS_BITMAP_FLUSH_OPS) {
        sdfs_save_bitmap();
    }
}

static void sdfs_bitmap_set(uint32_t block, int used) {
    if (!bitmap_cache) return;
    uint32_t byte_index = block / 8;
    uint8_t bit_mask = 1 << (block % 8);
    if (byte_index >= bitmap_bytes) return;
    if (used)
        bitmap_cache[byte_index] |= bit_mask;
    else
        bitmap_cache[byte_index] &= ~bit_mask;
    uint32_t bm_idx = byte_index / SDFS_BLOCK_SIZE;
    if (bm_idx < sizeof(bitmap_block_dirty)) bitmap_block_dirty[bm_idx] = 1;
}

static int sdfs_bitmap_test(uint32_t block) {
    if (!bitmap_cache) return 0;
    uint32_t byte_index = block / 8;
    uint8_t bit_mask = 1 << (block % 8);
    if (byte_index >= bitmap_bytes) return 0;
    return (bitmap_cache[byte_index] & bit_mask) != 0;
}

void sdfs_get_usage(uint32_t *total_blocks, uint32_t *used_blocks) {
    if (!sdfs_mounted || !bitmap_cache) {
        if (total_blocks) *total_blocks = 0;
        if (used_blocks) *used_blocks = 0;
        return;
    }
    if (total_blocks) *total_blocks = disk_total_blocks;
    if (used_blocks) {
        uint32_t used = 0;
        for (uint32_t b = 0; b < disk_total_blocks; b++) {
            if (sdfs_bitmap_test(b)) used++;
        }
        *used_blocks = used;
    }
}

static uint32_t sdfs_alloc_block_raw(void) {
    for (uint32_t b = 0; b < disk_total_blocks; b++) {
        if (!sdfs_bitmap_test(b)) {
            sdfs_bitmap_set(b, 1);
            sdfs_bitmap_touch();
            return b;
        }
    }
    return 0;
}

static uint32_t sdfs_alloc_block(void) {
    uint32_t b = sdfs_alloc_block_raw();
    if (b == 0) return 0;
    if (sdfs_zero_block(b) != 0) {
        sdfs_free_block(b);
        return 0;
    }
    return b;
}

static void sdfs_free_block(uint32_t block) {
    if (block == 0 || block >= disk_total_blocks) return;
    sdfs_bitmap_set(block, 0);
    sdfs_bitmap_touch();
}

/* ============================================================
 * Block chain helpers (for files)
 * ============================================================ */

static uint32_t sdfs_get_next_block(uint32_t block) {
    uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!buf) return 0;
    sdfs_read_block(block, buf);
    uint32_t next;
    memcpy(&next, buf, 4);
    kfree(buf);
    return next;
}

static void sdfs_set_next_block(uint32_t block, uint32_t next) {
    uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!buf) return;
    sdfs_read_block(block, buf);
    memcpy(buf, &next, 4);
    sdfs_metadata_write(block, buf);
    kfree(buf);
}

static uint32_t sdfs_append_block(uint32_t chain_start) {
    uint32_t new_block = sdfs_alloc_block();
    if (new_block == 0) return 0;

    uint32_t cur = chain_start;
    while (1) {
        uint32_t next = sdfs_get_next_block(cur);
        if (next == 0) break;
        cur = next;
    }
    sdfs_set_next_block(cur, new_block);
    return new_block;
}

static int sdfs_allocate_file_chain(uint8_t *buffer, uint32_t size,
                                    uint32_t *start_out, uint32_t **blocks_out,
                                    uint32_t *count_out) {
    uint32_t count = size ? (uint32_t)(((uint64_t)size + 4091u) / 4092u) : 1u;
    if (count > UINT32_MAX / sizeof(uint32_t)) return -1;
    uint32_t *blocks = (uint32_t *)kmalloc((size_t)count * sizeof(uint32_t));
    if (!blocks) return -1;
    memset(blocks, 0, (size_t)count * sizeof(uint32_t));

    uint32_t allocated = 0;
    while (allocated < count) {
        uint32_t block = sdfs_alloc_block_raw();
        if (block == 0) break;
        blocks[allocated++] = block;
    }
    if (allocated != count) {
        for (uint32_t i = 0; i < allocated; i++) sdfs_free_block(blocks[i]);
        sdfs_flush_bitmap();
        kfree(blocks);
        return -1;
    }

    uint8_t *block_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block_buf) {
        for (uint32_t i = 0; i < count; i++) sdfs_free_block(blocks[i]);
        sdfs_flush_bitmap();
        kfree(blocks);
        return -1;
    }

    uint32_t copied = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t chunk = size - copied;
        if (chunk > 4092) chunk = 4092;
        uint32_t next = (i + 1 < count) ? blocks[i + 1] : 0;
        memset(block_buf, 0, SDFS_BLOCK_SIZE);
        memcpy(block_buf, &next, sizeof(next));
        if (chunk) memcpy(block_buf + 4, buffer + copied, chunk);
        if (sdfs_write_block(blocks[i], block_buf) != 0) {
            kfree(block_buf);
            for (uint32_t j = 0; j < count; j++) sdfs_free_block(blocks[j]);
            sdfs_flush_bitmap();
            kfree(blocks);
            return -1;
        }
        copied += chunk;
    }
    kfree(block_buf);

    *start_out = blocks[0];
    *blocks_out = blocks;
    *count_out = count;
    return 0;
}

static int sdfs_free_block_list(uint32_t *blocks, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) sdfs_free_block(blocks[i]);
    return sdfs_flush_bitmap();
}

static int sdfs_free_file_chain(uint32_t first, uint32_t size) {
    uint32_t count = size ? (uint32_t)(((uint64_t)size + 4091u) / 4092u) : 1u;
    uint8_t *buffer = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!buffer) return -1;
    uint32_t block = first;
    for (uint32_t i = 0; i < count; i++) {
        if (block == 0 || block >= disk_total_blocks ||
            sdfs_read_block(block, buffer) != 0) {
            kfree(buffer);
            return -1;
        }
        uint32_t next;
        memcpy(&next, buffer, sizeof(next));
        if (i + 1 == count && next != 0) {
            kfree(buffer);
            return -1;
        }
        block = next;
    }
    block = first;
    for (uint32_t i = 0; i < count; i++) {
        if (sdfs_read_block(block, buffer) != 0) {
            kfree(buffer);
            return -1;
        }
        uint32_t next;
        memcpy(&next, buffer, sizeof(next));
        sdfs_free_block(block);
        block = next;
    }
    kfree(buffer);
    return 0;
}

/* ============================================================
 * V2 Directory entry serialization
 *
 * On-disk V2 entry layout:
 *   [0]     type
 *   [1]     name_len
 *   [2..N]  name (name_len bytes)
 *   [N+2..N+5]   start_block
 *   [N+6..N+9]   file_size
 *   [N+10..N+13] created_time
 *   [N+14..N+17] modified_time
 *   [N+18..N+19] uid
 *   [N+20..N+21] gid
 *   [N+22..N+23] permissions
 * Total overhead: 2 + name_len + 24
 * ============================================================ */

static int sdfs_entry_serialize(uint8_t *buf, uint32_t buf_size, const sdfs_entry_t *e) {
    uint32_t needed = 2 + e->name_len + 24;
    if (needed > buf_size) return -1;
    buf[0] = e->type;
    buf[1] = e->name_len;
    memcpy(buf + 2, e->name, e->name_len);
    uint32_t off = 2 + e->name_len;
    uint32_t v;
    uint16_t u16;
    v = e->start_block;  memcpy(buf + off, &v, 4); off += 4;
    v = e->file_size;    memcpy(buf + off, &v, 4); off += 4;
    v = e->created_time; memcpy(buf + off, &v, 4); off += 4;
    v = e->modified_time;memcpy(buf + off, &v, 4); off += 4;
    u16 = e->uid;        memcpy(buf + off, &u16, 2); off += 2;
    u16 = e->gid;        memcpy(buf + off, &u16, 2); off += 2;
    u16 = e->permissions;memcpy(buf + off, &u16, 2); off += 2;
    return (int)needed;
}

/* Deleted entries retain their original name length and payload on disk.
 * Preserve the slot boundary so directory walkers do not parse that payload
 * as a sequence of new entries. V1 directories are read-only, but remain
 * readable with their legacy entry size. */
static uint32_t sdfs_deleted_entry_size(const uint8_t *buf, uint32_t buf_size) {
    if (buf_size < 2 || buf[0] != SDFS_TYPE_DEL) return 2;
    uint32_t overhead = disk_format_version == 1 ? 12u : 24u;
    uint32_t size = 2u + (uint32_t)buf[1] + overhead;
    return size <= buf_size ? size : 0u;
}

static int sdfs_entry_deserialize(const uint8_t *buf, uint32_t buf_size, sdfs_entry_t *e) {
    if (buf_size < 2) return -1;
    e->type = buf[0];
    e->name_len = buf[1];
    if (e->type == SDFS_TYPE_FREE || e->type == SDFS_TYPE_DEL) return 1;

    uint32_t needed_v2 = 2 + e->name_len + 24;
    uint32_t needed_v1 = 2 + e->name_len + 12;

    if (needed_v2 <= buf_size) {
        if (e->name_len == 0) return -1;
        memcpy(e->name, buf + 2, e->name_len);
        e->name[e->name_len] = '\0';
        uint32_t off = 2 + e->name_len;
        memcpy(&e->start_block, buf + off, 4); off += 4;
        memcpy(&e->file_size, buf + off, 4); off += 4;
        memcpy(&e->created_time, buf + off, 4); off += 4;
        memcpy(&e->modified_time, buf + off, 4); off += 4;
        memcpy(&e->uid, buf + off, 2); off += 2;
        memcpy(&e->gid, buf + off, 2); off += 2;
        memcpy(&e->permissions, buf + off, 2); off += 2;
        e->block = 0;
        e->block_offset = 0;
        return (int)needed_v2;
    }

    if (needed_v1 <= buf_size) {
        if (e->name_len == 0) return -1;
        memcpy(e->name, buf + 2, e->name_len);
        e->name[e->name_len] = '\0';
        uint32_t off = 2 + e->name_len;
        memcpy(&e->start_block, buf + off, 4); off += 4;
        memcpy(&e->file_size, buf + off, 4); off += 4;
        uint32_t ts;
        memcpy(&ts, buf + off, 4);
        e->created_time = ts;
        e->modified_time = ts;
        e->uid = 0;
        e->gid = 0;
        e->permissions = SDFS_DEFAULT_PERM;
        e->block = 0;
        e->block_offset = 0;
        return (int)needed_v1;
    }

    return -1;
}

/* ============================================================
 * Directory entry helpers
 * ============================================================ */

static int sdfs_dir_count_entries(uint32_t dir_block) {
    uint32_t cur = dir_block;
    int count = 0;
    while (cur != 0) {
        uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!buf) break;
        if (sdfs_read_block(cur, buf) != 0) { kfree(buf); return -1; }
        sdfs_dir_block_t *db = (sdfs_dir_block_t *)buf;
        if (db->entry_count == 0) { kfree(buf); break; }
        uint8_t *pos = db->data;
        uint32_t remaining = 4090;
        for (int i = 0; i < db->entry_count; i++) {
            if (remaining < 2) break;
            uint8_t type = pos[0];
            uint8_t name_len = pos[1];
            if (type == SDFS_TYPE_FREE || type == SDFS_TYPE_DEL) {
                uint32_t slot_size = type == SDFS_TYPE_DEL
                    ? sdfs_deleted_entry_size(pos, remaining) : 2u;
                if (slot_size < 2) break;
                pos += slot_size; remaining -= slot_size; continue;
            }
            uint32_t entry_size = 2 + name_len + 24;
            if (entry_size > remaining) break;
            if ((type == SDFS_TYPE_FILE || type == SDFS_TYPE_DIR) && name_len > 0) {
                count++;
            }
            pos += entry_size;
            remaining -= entry_size;
        }
        cur = db->next_block;
        kfree(buf);
    }
    return count;
}

static int sdfs_dir_find_entry(uint32_t dir_block, const char *name, sdfs_entry_t *out) {
    uint32_t cur = dir_block;
    while (cur != 0) {
        uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!buf) break;
        sdfs_read_block(cur, buf);
        sdfs_dir_block_t *db = (sdfs_dir_block_t *)buf;
        if (db->entry_count == 0) { kfree(buf); break; }
        uint8_t *pos = db->data;
        uint32_t remaining = 4090;
        int found = 0;
        for (int i = 0; i < db->entry_count; i++) {
            if (remaining < 2) break;
            sdfs_entry_t e;
            int ret = sdfs_entry_deserialize(pos, remaining, &e);
            if (ret < 0) break;
            if (ret == 1) {
                uint32_t slot_size = sdfs_deleted_entry_size(pos, remaining);
                if (slot_size < 2) break;
                pos += slot_size; remaining -= slot_size; continue;
            }
            e.block = cur;
            e.block_offset = (uint16_t)(pos - buf);
            if (strcmp(e.name, name) == 0) {
                *out = e;
                found = 1;
                break;
            }
            pos += ret;
            remaining -= ret;
        }
        cur = db->next_block;
        kfree(buf);
        if (found) return 1;
    }
    return 0;
}

static int sdfs_dir_find_by_index(uint32_t dir_block, int index, sdfs_entry_t *out) {
    uint32_t cur = dir_block;
    int count = 0;
    while (cur != 0) {
        uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!buf) break;
        sdfs_read_block(cur, buf);
        sdfs_dir_block_t *db = (sdfs_dir_block_t *)buf;
        if (db->entry_count == 0) { kfree(buf); break; }
        uint8_t *pos = db->data;
        uint32_t remaining = 4090;
        for (int i = 0; i < db->entry_count; i++) {
            if (remaining < 2) break;
            sdfs_entry_t e;
            int ret = sdfs_entry_deserialize(pos, remaining, &e);
            if (ret < 0) break;
            if (ret == 1) {
                uint32_t slot_size = sdfs_deleted_entry_size(pos, remaining);
                if (slot_size < 2) break;
                pos += slot_size; remaining -= slot_size; continue;
            }
            if (count == index) {
                e.block = cur;
                e.block_offset = (uint16_t)(pos - buf);
                *out = e;
                kfree(buf);
                return 1;
            }
            count++;
            pos += ret;
            remaining -= ret;
        }
        cur = db->next_block;
        kfree(buf);
    }
    return 0;
}

static int sdfs_dir_name_exists(uint32_t dir_block, const char *name) {
    sdfs_entry_t tmp;
    return sdfs_dir_find_entry(dir_block, name, &tmp);
}

static int sdfs_dir_add_entry(uint32_t dir_block, sdfs_entry_t *e) {
    uint32_t cur = dir_block;
    while (1) {
        uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!buf) return -1;
        sdfs_read_block(cur, buf);
        sdfs_dir_block_t *db = (sdfs_dir_block_t *)buf;
        uint8_t *pos = db->data;
        uint32_t remaining = 4090;
        int entry_count = db->entry_count;

        int found_free = 0;
        uint32_t free_slot_remaining = 0;
        int idx;
        for (idx = 0; idx < entry_count; idx++) {
            if (remaining < 2) { break; }
            uint8_t type = pos[0];
            uint8_t name_len = pos[1];
            uint32_t slot_size = type == SDFS_TYPE_DEL
                ? sdfs_deleted_entry_size(pos, remaining)
                : (type == SDFS_TYPE_FREE ? 2u : 2u + name_len + 24u);
            if (slot_size < 2) break;
            if (type == SDFS_TYPE_FREE || type == SDFS_TYPE_DEL) {
                /* Reusing a larger slot would leave unparseable padding. */
                if (slot_size == 2u + (uint32_t)e->name_len + 24u) {
                    found_free = 1;
                    free_slot_remaining = remaining;
                    break;
                }
            }
            if (slot_size > remaining) break;
            pos += slot_size;
            remaining -= slot_size;
        }

        uint32_t entry_size_needed = 2 + e->name_len + 24;

        if (found_free) {
            int ret = sdfs_entry_serialize(pos, free_slot_remaining, e);
            if (ret < 0) { kfree(buf); return -1; }
            if (sdfs_metadata_write(cur, buf) != 0) { kfree(buf); return -1; }
            kfree(buf);
            return 0;
        }

        if (remaining >= entry_size_needed) {
            int ret = sdfs_entry_serialize(pos, remaining, e);
            if (ret < 0) { kfree(buf); return -1; }
            db->entry_count = (uint16_t)(entry_count + 1);
            memcpy(buf, db, 6);
            if (sdfs_metadata_write(cur, buf) != 0) { kfree(buf); return -1; }
            kfree(buf);
            return 0;
        }

        if (db->next_block == 0) {
            uint32_t new_block = sdfs_alloc_block();
            if (new_block == 0) { kfree(buf); return -1; }
            if (sdfs_flush_bitmap() != 0) {
                sdfs_free_block(new_block);
                sdfs_flush_bitmap();
                kfree(buf);
                return -1;
            }
            db->next_block = new_block;
            if (sdfs_metadata_write(cur, buf) != 0) { kfree(buf); return -1; }
        }
        cur = db->next_block;
        kfree(buf);
    }
}

/* Prepare (but do not publish) a destination directory block for rename. The
 * source block may already contain an in-memory tombstone; reuse that buffer
 * when both entries occupy the same block. A new directory block is fully
 * initialized and its allocation bit flushed before its parent link is
 * included in the rename transaction. */
static int sdfs_dir_prepare_rename_add(uint32_t dir_block, sdfs_entry_t *entry,
                                       uint32_t source_block,
                                       uint8_t *source_buffer,
                                       uint32_t *target_block_out,
                                       uint8_t **target_buffer_out,
                                       int *target_owned_out) {
    uint32_t cur = dir_block;
    uint32_t entry_size = 2u + (uint32_t)entry->name_len + 24u;

    while (cur != 0) {
        uint8_t *buf;
        int owned = cur != source_block;
        if (owned) {
            buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
            if (!buf) return -1;
            if (sdfs_read_block(cur, buf) != 0) {
                kfree(buf);
                return -1;
            }
        } else {
            buf = source_buffer;
        }

        sdfs_dir_block_t *db = (sdfs_dir_block_t *)buf;
        uint8_t *pos = db->data;
        uint32_t remaining = sizeof(db->data);
        int malformed = 0;
        for (uint32_t i = 0; i < db->entry_count; i++) {
            if (remaining < 2) { malformed = 1; break; }
            uint8_t type = pos[0];
            uint32_t slot_size;
            if (type == SDFS_TYPE_FREE) {
                slot_size = 2;
            } else if (type == SDFS_TYPE_DEL) {
                slot_size = sdfs_deleted_entry_size(pos, remaining);
                if (slot_size < 2) { malformed = 1; break; }
                if (slot_size == entry_size) {
                    if (sdfs_entry_serialize(pos, remaining, entry) < 0) {
                        malformed = 1;
                        break;
                    }
                    *target_block_out = cur;
                    *target_buffer_out = buf;
                    *target_owned_out = owned;
                    return 0;
                }
            } else {
                sdfs_entry_t current;
                int decoded = sdfs_entry_deserialize(pos, remaining, &current);
                if (decoded <= 1) { malformed = 1; break; }
                slot_size = (uint32_t)decoded;
            }
            if (slot_size > remaining) { malformed = 1; break; }
            pos += slot_size;
            remaining -= slot_size;
        }

        if (malformed) {
            if (owned) kfree(buf);
            return -1;
        }
        if (remaining >= entry_size) {
            if (sdfs_entry_serialize(pos, remaining, entry) < 0) {
                if (owned) kfree(buf);
                return -1;
            }
            db->entry_count++;
            *target_block_out = cur;
            *target_buffer_out = buf;
            *target_owned_out = owned;
            return 0;
        }

        uint32_t next = db->next_block;
        if (next != 0) {
            if (owned) kfree(buf);
            cur = next;
            continue;
        }

        uint32_t new_block = sdfs_alloc_block();
        if (new_block == 0 || sdfs_flush_bitmap() != 0) {
            if (new_block != 0) {
                sdfs_free_block(new_block);
                (void)sdfs_flush_bitmap();
            }
            if (owned) kfree(buf);
            return -1;
        }

        uint8_t *new_buffer = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!new_buffer) {
            sdfs_free_block(new_block);
            (void)sdfs_flush_bitmap();
            if (owned) kfree(buf);
            return -1;
        }
        memset(new_buffer, 0, SDFS_BLOCK_SIZE);
        sdfs_dir_block_t *new_db = (sdfs_dir_block_t *)new_buffer;
        new_db->entry_count = 1;
        if (sdfs_entry_serialize(new_db->data, sizeof(new_db->data), entry) < 0 ||
            sdfs_write_block(new_block, new_buffer) != 0 ||
            sdfs_flush_device() != 0) {
            kfree(new_buffer);
            sdfs_free_block(new_block);
            (void)sdfs_flush_bitmap();
            if (owned) kfree(buf);
            return -1;
        }
        kfree(new_buffer);

        db->next_block = new_block;
        *target_block_out = cur;
        *target_buffer_out = buf;
        *target_owned_out = owned;
        return 0;
    }
    return -1;
}

static int sdfs_dir_remove_entry(uint32_t dir_block, const char *name) {
    uint32_t cur = dir_block;
    while (cur != 0) {
        uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!buf) break;
        if (sdfs_read_block(cur, buf) != 0) { kfree(buf); break; }
        sdfs_dir_block_t *db = (sdfs_dir_block_t *)buf;
        if (db->entry_count == 0) { kfree(buf); break; }
        uint8_t *pos = db->data;
        uint32_t remaining = 4090;
        for (int i = 0; i < db->entry_count; i++) {
            if (remaining < 2) break;
            sdfs_entry_t e;
            int ret = sdfs_entry_deserialize(pos, remaining, &e);
            if (ret < 0) break;
            if (ret > 1 && strcmp(e.name, name) == 0) {
                pos[0] = SDFS_TYPE_DEL;
                if (sdfs_metadata_write(cur, buf) != 0) { kfree(buf); return -1; }
                kfree(buf);
                return 0;
            }
            if (ret <= 1) {
                uint32_t slot_size = ret == 1
                    ? sdfs_deleted_entry_size(pos, remaining) : 2u;
                if (slot_size < 2) break;
                pos += slot_size; remaining -= slot_size; continue;
            }
            pos += ret;
            remaining -= ret;
        }
        cur = db->next_block;
        kfree(buf);
    }
    return -1;
}

/* ============================================================
 * Path resolution
 * ============================================================ */

static int sdfs_is_root_path(const char *path) {
    if (!path) return 0;
    if (strcmp(path, "/") == 0 || strcmp(path, "/disk") == 0 ||
        strcmp(path, "/disk/") == 0 || strcmp(path, "disk:") == 0 ||
        strcmp(path, "disk:/") == 0 || strcmp(path, "ram:") == 0 ||
        strcmp(path, "ram:/") == 0) return 1;
    return 0;
}

static int sdfs_is_valid_path(const char *path) {
    if (!path) return 0;
    if (path[0] == '/') return 1;
    if (strncmp(path, "disk:", 5) == 0 || strncmp(path, "ram:", 4) == 0) return 1;
    return 0;
}

static int sdfs_resolve_parent(const char *path, uint32_t *parent_block_out, char *name_out) {
    if (!path) return -1;

    const char *p = path;
    if (strncmp(p, "disk:", 5) == 0) p += 5;
    else if (strncmp(p, "ram:", 4) == 0) p += 4;
    else if (strncmp(p, "/disk/", 6) == 0) p += 5;

    while (*p == '/') p++;
    if (!*p) return -1;

    char *tmp = (char *)kmalloc(512);
    if (!tmp) return -1;
    tmp[0] = '/';
    int plen = strlen(p);
    if (plen >= 510) { kfree(tmp); return -1; }
    memcpy(tmp + 1, p, plen + 1);

    char *last_slash = NULL;
    for (char *scan = tmp; *scan; scan++) {
        if (*scan == '/') last_slash = scan;
    }
    if (!last_slash) { kfree(tmp); return -1; }

    char *leaf = last_slash + 1;
    if (*leaf == '\0') { kfree(tmp); return -1; }

    uint32_t cur_block = disk_root_block;

    char *comp = tmp;
    while (comp < leaf) {
        if (*comp == '/') { comp++; continue; }
        char *next_slash = comp;
        while (*next_slash && *next_slash != '/') next_slash++;
        char saved = *next_slash;
        *next_slash = '\0';

        sdfs_entry_t e;
        if (!sdfs_dir_find_entry(cur_block, comp, &e)) {
            kfree(tmp); return -1;
        }
        if (e.type != SDFS_TYPE_DIR) { kfree(tmp); return -1; }
        cur_block = e.start_block;

        *next_slash = saved;
        comp = next_slash;
    }

    *parent_block_out = cur_block;
    int nlen = strlen(leaf);
    if (nlen > 255) nlen = 255;
    memcpy(name_out, leaf, nlen);
    name_out[nlen] = '\0';
    kfree(tmp);
    return 0;
}

static int sdfs_resolve_full(const char *path, sdfs_entry_t *out) {
    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;
    return sdfs_dir_find_entry(parent_block, name, out);
}

/* ============================================================
 * Raw block I/O (exposed for tests and fsck)
 * ============================================================ */

int sdfs_read_raw_block(uint32_t block, uint8_t *buffer) {
    return sdfs_read_block(block, buffer);
}

int sdfs_write_raw_block(uint32_t block, uint8_t *buffer) {
    return sdfs_write_block(block, buffer);
}

/* ============================================================
 * fsck - File System Check
 * ============================================================ */

int sdfs_fsck(void) {
    serial_print("[fsck] Starting filesystem check...\n");
    int errors = 0;

    if (!bitmap_cache || disk_total_blocks == 0) {
        serial_print("[fsck] No bitmap loaded\n");
        return -1;
    }

    uint32_t reserved = disk_bitmap_block + disk_bitmap_blocks;
    if (disk_journal_start > 0) {
        reserved = disk_journal_start + disk_journal_blocks;
    }
    if (disk_root_block > reserved) reserved = disk_root_block;

    uint32_t used_count = 0;
    for (uint32_t b = 0; b < disk_total_blocks; b++) {
        if (sdfs_bitmap_test(b)) used_count++;
    }

    uint32_t reachable = 0;

    uint8_t *scratch = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!scratch) {
        serial_print("[fsck] Cannot allocate scratch block\n");
        return -1;
    }

    if (sdfs_read_block(disk_root_block, scratch) == 0) {
        sdfs_dir_block_t *db = (sdfs_dir_block_t *)scratch;
        if (db->entry_count > 0) {
            reachable++;
        }
    }

    kfree(scratch);

    for (uint32_t b = 0; b < reserved; b++) {
        if (sdfs_bitmap_test(b)) {
            reachable++;
        }
    }

    if (used_count != reachable && reachable < used_count) {
        serial_print("[fsck] Bitmap inconsistency detected\n");
        serial_print("[fsck] (may settle after directory scan)\n");
    }

    if (disk_mount_count > disk_max_mounts && disk_max_mounts > 0) {
        serial_print("[fsck] Mount count exceeded max_mounts, resetting\n");
        disk_mount_count = 0;
    }

    serial_print("[fsck] Check complete\n");
    return errors;
}

/* ============================================================
 * Format (V2)
 * ============================================================ */

int sdfs_format(void) {
    if (!sdfs_ramdisk) {
        if (sdfs_is_usb_msc) {
            uint32_t total_sec = sdfs_partition_total_sectors;
            if (total_sec == 0) total_sec = (uint32_t)usb_msc_get_sector_count();
            if (total_sec == 0) total_sec = 204800;
            disk_total_blocks = total_sec / SDFS_SECTORS_PER_BLOCK;
            if (disk_total_blocks < 10) disk_total_blocks = 25600;
            if (disk_total_blocks > SDFS_MAX_BLOCKS) disk_total_blocks = SDFS_MAX_BLOCKS;
        } else {
            sdfs_check_ahci();
            if (sdfs_ahci_port == 0xFF && ata_probe(ata_bus, ata_drive) != 0) {
                serial_print("SDFS: format refused, no AHCI/ATA disk selected\n");
                return -1;
            }

            uint16_t mbr_buf[256];
            if (sdfs_ahci_port != 0xFF) {
                if (!ahci_read_sector(sdfs_ahci_port, 0, 1, (uint8_t*)mbr_buf)) {
                    serial_print("SDFS: format failed, cannot read sector 0 (AHCI)\n");
                    return -1;
                }
            } else if (ata_read_sector(ata_bus, ata_drive, 0, mbr_buf) != 0) {
                serial_print("SDFS: format failed, cannot read sector 0\n");
                return -1;
            }
            uint8_t *mbr = (uint8_t *)mbr_buf;
            uint32_t part_sectors = *(uint32_t *)(mbr + 458);
            if (part_sectors == 0) part_sectors = 204800;

            uint32_t total_sectors = part_sectors;
            if (sdfs_partition_total_sectors > 0) {
                total_sectors = sdfs_partition_total_sectors;
            } else if (sdfs_ahci_port != 0xFF) {
                extern uint64_t ahci_identify(uint8_t portno);
                uint64_t ahci_sectors = ahci_identify(sdfs_ahci_port);
                if (ahci_sectors > 0) {
                    total_sectors = (uint32_t)(ahci_sectors > 0xFFFFFFFF ? 0xFFFFFFFF : ahci_sectors);
                    if (sdfs_partition_lba > 0 && total_sectors > sdfs_partition_lba) {
                        total_sectors -= sdfs_partition_lba;
                    }
                }
            } else {
                uint32_t ata_sectors = ata_identify_sectors(ata_bus, ata_drive);
                if (ata_sectors > 0) {
                    total_sectors = ata_sectors;
                    if (sdfs_partition_lba > 0 && total_sectors > sdfs_partition_lba) {
                        total_sectors -= sdfs_partition_lba;
                    }
                }
            }

            disk_total_blocks = total_sectors / SDFS_SECTORS_PER_BLOCK;
            if (disk_total_blocks < 10) disk_total_blocks = 25600;
            if (disk_total_blocks > SDFS_MAX_BLOCKS) disk_total_blocks = SDFS_MAX_BLOCKS;
        }
    }
    serial_print("SDFS: format total_blocks=");
    char tbuf[16];
    itoa(disk_total_blocks, tbuf, 10);
    serial_print(tbuf);
    serial_print("\n");

    bitmap_bytes = (disk_total_blocks + 7) / 8;
    disk_bitmap_blocks = (bitmap_bytes + SDFS_BLOCK_SIZE - 1) / SDFS_BLOCK_SIZE;
    if (disk_bitmap_blocks < 1) disk_bitmap_blocks = 1;
    disk_bitmap_block = 1;

    disk_journal_blocks = SDFS_DEFAULT_JOURNAL_BLOCKS;
    disk_journal_start = disk_bitmap_block + disk_bitmap_blocks;

    disk_root_block = disk_journal_start + disk_journal_blocks;
    disk_format_version = SDFS_FORMAT_VER;
    disk_mount_count = 0;
    disk_max_mounts = SDFS_DEFAULT_MAX_MOUNTS;

    if (disk_root_block + 1 >= disk_total_blocks) {
        disk_total_blocks = disk_root_block + 3;
    }

    uint8_t *sb_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    memset(sb_buf, 0, SDFS_BLOCK_SIZE);
    sdfs_super_t *sb = (sdfs_super_t *)sb_buf;
    memcpy(sb->magic, SDFS_MAGIC_V2, 8);
    sb->format_version = SDFS_FORMAT_VER;
    sb->block_size = SDFS_BLOCK_SIZE;
    sb->total_blocks = disk_total_blocks;
    sb->bitmap_block = disk_bitmap_block;
    sb->bitmap_blocks = disk_bitmap_blocks;
    sb->journal_start = disk_journal_start;
    sb->journal_blocks = disk_journal_blocks;
    sb->root_dir_block = disk_root_block;
    sb->mount_count = 0;
    sb->max_mounts = SDFS_DEFAULT_MAX_MOUNTS;
    sb->created_time = timer_ticks;

    sb->superblock_crc = crc32_calc(sb_buf, 52);

    if (sdfs_write_block(0, sb_buf) != 0) {
        serial_print("SDFS: format failed, cannot write superblock\n");
        kfree(sb_buf);
        return -1;
    }
    kfree(sb_buf);

    bitmap_cache = (uint8_t *)kmalloc(disk_bitmap_blocks * SDFS_BLOCK_SIZE);
    memset(bitmap_cache, 0, disk_bitmap_blocks * SDFS_BLOCK_SIZE);
    bitmap_bytes = disk_bitmap_blocks * SDFS_BLOCK_SIZE;
    memset(bitmap_block_dirty, 1, sizeof(bitmap_block_dirty));

    for (uint32_t b = 0; b <= disk_root_block; b++) {
        sdfs_bitmap_set(b, 1);
    }
    if (sdfs_save_bitmap() != 0) {
        kfree(bitmap_cache);
        bitmap_cache = NULL;
        return -1;
    }

    for (uint32_t b = disk_journal_start;
         b < disk_journal_start + disk_journal_blocks; b++) {
        if (sdfs_zero_block(b) != 0) {
            kfree(bitmap_cache);
            bitmap_cache = NULL;
            return -1;
        }
    }

    if (sdfs_zero_block(disk_root_block) != 0) {
        serial_print("SDFS: format failed, cannot write root dir\n");
        kfree(bitmap_cache);
        bitmap_cache = NULL;
        return -1;
    }
    if (sdfs_flush_device() != 0) {
        kfree(bitmap_cache);
        bitmap_cache = NULL;
        return -1;
    }

    kfree(bitmap_cache);
    bitmap_cache = NULL;

    serial_print("SDFS: Formatted OK (V2)\n");
    return 0;
}

/* ============================================================
 * Mount
 * ============================================================ */

fs_node_t *sdfs_mount(uint16_t bus, uint8_t drive, uint32_t partition_lba) {
    sdfs_chain_cache_invalidate();
    sdfs_corrupt_mount = 0;
    sdfs_mounted = 0;
    sdfs_mount_err_msg = "OK";
    ata_bus = bus;
    ata_drive = drive;
    if (partition_lba != 0) {
        sdfs_partition_lba = partition_lba;
    }

    if (!sdfs_ramdisk && !sdfs_is_usb_msc) {
        sdfs_check_ahci();
        if (sdfs_ahci_port == 0xFF && sdfs_partition_total_sectors == 0) {
            if (ata_probe(ata_bus, ata_drive) != 0) {
                sdfs_mount_err_msg = "Disco ATA/AHCI nao respondeu ao probe";
                serial_print("SDFS: no AHCI/ATA disk available\n");
                return NULL;
            }
        }
    }

    uint8_t *sb_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!sb_buf) {
        sdfs_mount_err_msg = "Sem memoria para ler superbloco";
        return NULL;
    }
    if (sdfs_read_block(0, sb_buf) != 0) {
        sdfs_mount_err_msg = "Falha de I/O ao ler superbloco (bloco 0)";
        serial_print("SDFS: failed to read superblock\n");
        kfree(sb_buf);
        return NULL;
    }

    sdfs_super_t *sb = (sdfs_super_t *)sb_buf;

    int is_v2 = (memcmp(sb->magic, SDFS_MAGIC_V2, 8) == 0);
    int is_v1 = (memcmp(sb->magic, SDFS_MAGIC_V1, 8) == 0);

    if (!is_v2 && !is_v1) {
        sdfs_mount_err_msg = "Assinatura magica do superbloco invalida (nao e SDFS0020)";
        serial_print("SDFS: Invalid superblock magic\n");
        kfree(sb_buf);
        return NULL;
    }

    if (is_v2) {
        uint32_t saved_crc = sb->superblock_crc;
        sb->superblock_crc = 0;
        uint32_t computed_crc = crc32_calc(sb_buf, 52);
        sb->superblock_crc = saved_crc;

        if (saved_crc != computed_crc) {
            sdfs_mount_err_msg = "CRC32 do superbloco incorreto";
            serial_print("SDFS: Superblock CRC mismatch; refusing mount\n");
            sdfs_corrupt_mount = 1;
            kfree(sb_buf);
            return NULL;
        }

        disk_total_blocks = sb->total_blocks;
        disk_bitmap_block = sb->bitmap_block;
        disk_bitmap_blocks = sb->bitmap_blocks;
        disk_journal_start = sb->journal_start;
        disk_journal_blocks = sb->journal_blocks;
        disk_root_block = sb->root_dir_block;
        disk_format_version = sb->format_version;
        disk_mount_count = sb->mount_count + 1;
        disk_max_mounts = sb->max_mounts;

        if (disk_total_blocks < 10 || disk_total_blocks > SDFS_MAX_BLOCKS ||
            disk_bitmap_blocks == 0 ||
            disk_root_block >= disk_total_blocks ||
            disk_bitmap_block == 0 ||
            (uint64_t)disk_bitmap_block + disk_bitmap_blocks > disk_total_blocks ||
            (uint64_t)disk_bitmap_blocks * SDFS_BLOCK_SIZE <
                ((uint64_t)disk_total_blocks + 7) / 8 ||
            disk_journal_blocks < 3 ||
            disk_journal_start != disk_bitmap_block + disk_bitmap_blocks ||
            (uint64_t)disk_journal_start + disk_journal_blocks != disk_root_block ||
            (uint64_t)disk_journal_start + disk_journal_blocks > disk_total_blocks) {
            sdfs_mount_err_msg = "Geometria do superbloco fora dos limites";
            serial_print("SDFS: unsupported/oversized superblock, forcing reformat\n");
            sdfs_corrupt_mount = 1;
            kfree(sb_buf);
            return NULL;
        }

    } else {
        sdfs_super_v1_t *sb1 = (sdfs_super_v1_t *)sb_buf;
        disk_total_blocks = sb1->total_blocks;
        disk_bitmap_block = sb1->bitmap_block;
        disk_bitmap_blocks = sb1->bitmap_blocks;
        disk_root_block = sb1->root_dir_block;
        disk_journal_start = 0;
        disk_journal_blocks = 0;
        disk_format_version = 1;
        disk_mount_count = 0;
        disk_max_mounts = 0;
        /* V1 has no journal. Fail closed for writes instead of reusing a
          * journal configuration left by a previous mounted filesystem. */
        sdfs_journal_init(0, 0, disk_total_blocks, disk_bitmap_block,
                          disk_bitmap_blocks, sdfs_read_block, sdfs_write_block,
                          sdfs_flush_device);
        serial_print("SDFS: Mounted V1 (no journal)\n");
    }

    kfree(sb_buf);

    if (sdfs_load_bitmap() != 0) {
        sdfs_mount_err_msg = "Falha de I/O ao carregar bitmap de blocos";
        serial_print("SDFS: Failed to load bitmap\n");
        sdfs_corrupt_mount = 1;
        return NULL;
    }

    if (disk_journal_start > 0 && disk_journal_blocks >= 2) {
        sdfs_journal_init(disk_journal_start, disk_journal_blocks,
                          disk_total_blocks, disk_bitmap_block,
                          disk_bitmap_blocks, sdfs_read_block,
                          sdfs_write_block, sdfs_flush_device);

        int replayed = sdfs_journal_replay();
        if (replayed < 0) {
            sdfs_mount_err_msg = "Journal WAL corrompido ou falha de leitura";
            serial_print("SDFS: invalid or unrecoverable journal; refusing mount\n");
            sdfs_corrupt_mount = 1;
            kfree(bitmap_cache);
            bitmap_cache = NULL;
            return NULL;
        }
        if (replayed > 0) {
            serial_print("[journal] Replayed ");
            char buf[16];
            itoa(replayed, buf, 10);
            serial_print(buf);
            serial_print(" entries\n");
        }
    }

    /* Do not mutate even the mount counter until the WAL has been validated
     * and replayed successfully. A damaged committed journal must remain
     * untouched for offline recovery. */
    if (disk_format_version == SDFS_FORMAT_VER) {
        sb_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!sb_buf || sdfs_read_block(0, sb_buf) != 0) {
            if (sb_buf) kfree(sb_buf);
            sdfs_mount_err_msg = "Falha ao reler superbloco apos journal";
            sdfs_corrupt_mount = 1;
            kfree(bitmap_cache);
            bitmap_cache = NULL;
            return NULL;
        }
        sb = (sdfs_super_t *)sb_buf;
        sb->mount_count = disk_mount_count;
        sb->superblock_crc = 0;
        sb->superblock_crc = crc32_calc(sb_buf, 52);
        int write_result = sdfs_write_block(0, sb_buf);
        kfree(sb_buf);
        if (write_result != 0 || sdfs_flush_device() != 0) {
            sdfs_mount_err_msg = "Falha ao atualizar contador de mount no superbloco";
            sdfs_corrupt_mount = 1;
            kfree(bitmap_cache);
            bitmap_cache = NULL;
            return NULL;
        }
    }

    sdfs_fsck();

    fs_node_t *root_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(root_node, 0, sizeof(fs_node_t));
    strcpy(root_node->name, "sdfs_root");
    root_node->flags = FS_DIRECTORY;
    root_node->mask = 0777;
    root_node->uid = 0;
    root_node->gid = 0;
    root_node->inode = disk_root_block;
    root_node->length = 0;
    root_node->open = sdfs_open;
    root_node->close = sdfs_close;
    root_node->readdir = sdfs_readdir;
    root_node->finddir = sdfs_finddir;
    root_node->create = sdfs_vfs_create;
    root_node->create_as = sdfs_vfs_create_as;
    root_node->mkdir = sdfs_vfs_mkdir;
    root_node->unlink = sdfs_vfs_unlink;
    root_node->rmdir = sdfs_vfs_rmdir;
    root_node->rename = sdfs_vfs_rename;
    root_node->sync = sdfs_sync;

    sdfs_mounted = 1;
    serial_print("SDFS: Mounted V2\n");
    return root_node;
}

fs_node_t *sdfs_mount_msc(void) {
    sdfs_is_usb_msc = 1;
    return sdfs_mount(0, 0, sdfs_partition_lba);
}

int sdfs_mount_failed_corrupt(void) {
    return sdfs_corrupt_mount;
}

int sdfs_is_mounted(void) {
    return sdfs_mounted;
}

/* ============================================================
 * Journal status
 * ============================================================ */

int sdfs_journal_dirty(void) {
    if (disk_journal_start == 0) return 0;
    return sdfs_journal_is_dirty();
}

int sdfs_journal_entry_count(void) {
    return 0;
}

/* ============================================================
 * Public path-based API
 * ============================================================ */

static uint32_t sdfs_now(void) {
    return timer_ticks;
}

/* Early SDFS V2 images used these private encodings before permission
 * enforcement existed. Interpret them as the modes they were intended to
 * represent; all new metadata is stored in the standard 0777 layout. */
static uint16_t sdfs_permission_mode(uint16_t stored) {
    if (stored == 0x0314) return 0644;
    if (stored == 0x0315) return 0755;
    return stored & 0777;
}

static int sdfs_create_entry_at(uint32_t parent_block, const char *name,
                                uint8_t type, uint16_t uid, uint16_t gid,
                                uint16_t permissions) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    if (!name || !name[0] || strlen(name) > SDFS_MAX_NAME ||
        (permissions & ~0777u) || sdfs_dir_name_exists(parent_block, name)) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    uint32_t start_block = sdfs_alloc_block();
    if (!start_block) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    if (sdfs_flush_bitmap() != 0) {
        sdfs_free_block(start_block);
        sdfs_flush_bitmap();
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    uint32_t now = sdfs_now();
    sdfs_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = type;
    entry.name_len = strlen(name);
    memcpy(entry.name, name, entry.name_len);
    entry.start_block = start_block;
    entry.created_time = now;
    entry.modified_time = now;
    entry.uid = uid;
    entry.gid = gid;
    entry.permissions = permissions;
    if (sdfs_dir_add_entry(parent_block, &entry) != 0) {
        /* A failed metadata write can leave commit state uncertain. Keep the
         * block allocated rather than risking a live name to a freed block. */
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return 0;
}

int sdfs_create_file_as(const char *path, uint16_t uid, uint16_t gid,
                        uint16_t permissions) {
    if (!sdfs_mounted || !sdfs_is_valid_path(path)) return -1;
    if (sdfs_is_root_path(path) || (permissions & ~0777u)) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;
    return sdfs_create_entry_at(parent_block, name, SDFS_TYPE_FILE,
                                uid, gid, permissions);
}

int sdfs_create_file(const char *path) {
    return sdfs_create_file_as(path, 0, 0, SDFS_DEFAULT_PERM);
}

static uint32_t sdfs_write_file_inner(const char *path, uint8_t *buffer,
                                      uint32_t size) {
    if (!sdfs_mounted || !path || !buffer) return 0;

    sdfs_entry_t existing;
    int exists = sdfs_resolve_full(path, &existing);
    uint32_t parent_block;
    char name[256];

    if (exists) {
        if (existing.type != SDFS_TYPE_FILE) return 0;
        if (sdfs_resolve_parent(path, &parent_block, name) != 0) return 0;
    } else {
        if (sdfs_resolve_parent(path, &parent_block, name) != 0) return 0;
    }

    uint32_t start_block, *new_blocks, new_block_count;
    if (sdfs_allocate_file_chain(buffer, size, &start_block, &new_blocks,
                                 &new_block_count) != 0) return 0;

    /* New file contents must be durable before any directory entry can point
     * at them. Persist allocation bits too, so replay never references blocks
     * that the bitmap still considers free. */
    if (sdfs_flush_device() != 0 || sdfs_flush_bitmap() != 0) {
        sdfs_free_block_list(new_blocks, new_block_count);
        kfree(new_blocks);
        return 0;
    }

    uint32_t now = sdfs_now();

    sdfs_entry_t e;
    memset(&e, 0, sizeof(e));
    e.type = SDFS_TYPE_FILE;
    e.name_len = strlen(name);
    memcpy(e.name, name, e.name_len);
    e.start_block = start_block;
    e.file_size = size;
    e.created_time = exists ? existing.created_time : now;
    e.modified_time = now;
    e.uid = exists ? existing.uid : 0;
    e.gid = exists ? existing.gid : 0;
    e.permissions = exists ? existing.permissions : SDFS_DEFAULT_PERM;

    int metadata_result;
    if (exists) {
        uint8_t *dir_block = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!dir_block || sdfs_read_block(existing.block, dir_block) != 0) {
            if (dir_block) kfree(dir_block);
            sdfs_free_block_list(new_blocks, new_block_count);
            kfree(new_blocks);
            return 0;
        }
        if (sdfs_entry_serialize(dir_block + existing.block_offset,
                                 SDFS_BLOCK_SIZE - existing.block_offset,
                                 &e) < 0) {
            kfree(dir_block);
            sdfs_free_block_list(new_blocks, new_block_count);
            kfree(new_blocks);
            return 0;
        }
        metadata_result = sdfs_metadata_write(existing.block, dir_block);
        kfree(dir_block);
    } else {
        metadata_result = sdfs_dir_add_entry(parent_block, &e);
    }

    if (metadata_result != 0) {
        /* The target might have reached stable storage even if a later flush
         * or journal-clean step reported failure. Keep the COW blocks allocated
         * until recovery can establish which directory version won. */
        kfree(new_blocks);
        return 0;
    }

    kfree(new_blocks);

    /* The directory now names the replacement. Old blocks are unreachable;
     * freeing them after commit can leak space on interruption, but cannot
     * remove data from the committed file. */
    if (exists && sdfs_mark_open_unlinked(existing.start_block,
                                          existing.file_size, 0)) {
        /* Existing descriptors retain their old inode chain until close. */
    } else if (exists && sdfs_free_file_chain(existing.start_block,
                                              existing.file_size) == 0) {
        if (sdfs_flush_bitmap() != 0)
            serial_print("SDFS: replacement committed; old blocks may leak\n");
    } else if (exists) {
        serial_print("SDFS: replacement committed; old chain cleanup deferred\n");
    }

    return size;
}

uint32_t sdfs_write_file(const char *path, uint8_t *buffer, uint32_t size) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return 0;
    uint32_t result = sdfs_write_file_inner(path, buffer, size);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

static void *sdfs_read_file_inner(const char *path, uint32_t *size_out) {
    if (!sdfs_mounted || !path || !size_out) return NULL;

    sdfs_entry_t e;
    if (!sdfs_resolve_full(path, &e)) return NULL;
    if (e.type != SDFS_TYPE_FILE) return NULL;

    *size_out = e.file_size;
    if (e.file_size == 0) {
        uint8_t *empty = (uint8_t *)kmalloc(1);
        empty[0] = 0;
        *size_out = 0;
        return empty;
    }

    uint32_t num_blocks = (e.file_size + 4091) / 4092;
    if (num_blocks == 0) num_blocks = 1;

    uint32_t buf_size = e.file_size;
    uint8_t *result = (uint8_t *)kmalloc(buf_size + 1);
    if (!result) return NULL;
    result[buf_size] = '\0';

    uint32_t remaining = e.file_size;
    uint32_t cur_block = e.start_block;
    uint32_t offset = 0;
    uint8_t *block_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block_buf) {
        kfree(result);
        return NULL;
    }

    while (remaining > 0 && cur_block != 0) {
        sdfs_read_block(cur_block, block_buf);

        uint32_t to_copy = remaining;
        if (to_copy > 4092) to_copy = 4092;

        memcpy(result + offset, block_buf + 4, to_copy);
        offset += to_copy;
        remaining -= to_copy;

        cur_block = sdfs_get_next_block(cur_block);
    }
    kfree(block_buf);

    return result;
}

void *sdfs_read_file(const char *path, uint32_t *size_out) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return NULL;
    void *result = sdfs_read_file_inner(path, size_out);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

int sdfs_create_dir_as(const char *path, uint16_t uid, uint16_t gid,
                       uint16_t permissions) {
    if (!sdfs_mounted || !sdfs_is_valid_path(path)) return -1;
    if (sdfs_is_root_path(path) || (permissions & ~0777u)) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;
    return sdfs_create_entry_at(parent_block, name, SDFS_TYPE_DIR,
                                uid, gid, permissions);
}

int sdfs_create_dir(const char *path) {
    return sdfs_create_dir_as(path, 0, 0, SDFS_DEFAULT_DIR_PERM);
}

static int sdfs_delete_entry(uint32_t parent_block, const char *name,
                             int expect_dir) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(parent_block, name, &e)) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    if (expect_dir >= 0 &&
        (e.type == SDFS_TYPE_DIR) != (expect_dir != 0)) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }

    if (e.type == SDFS_TYPE_DIR && sdfs_dir_count_entries(e.start_block) > 0) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    /* Make the name unreachable durably before releasing any referenced
     * blocks. A crash can leak blocks, but cannot leave a live entry pointing
     * into space that has already been reallocated. */
    if (sdfs_dir_remove_entry(parent_block, name) != 0) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    if (sdfs_mark_open_unlinked(e.start_block, e.file_size,
                                e.type == SDFS_TYPE_DIR)) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return 0;
    }
    if (e.type == SDFS_TYPE_DIR) {
        sdfs_free_block(e.start_block);
    } else if (sdfs_free_file_chain(e.start_block, e.file_size) != 0) {
        serial_print("SDFS: file unlinked; block cleanup deferred\n");
        return 0;
    }
    if (sdfs_flush_bitmap() != 0)
        serial_print("SDFS: delete committed; freed blocks may leak\n");
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return 0;
}

int sdfs_delete(const char *path) {
    if (!sdfs_mounted || !path || sdfs_is_root_path(path)) return -1;
    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;
    return sdfs_delete_entry(parent_block, name, -1);
}

static int sdfs_rename_entry(uint32_t old_parent_block, const char *old_name,
                             uint32_t new_parent_block, const char *new_name) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    if (old_parent_block == new_parent_block &&
        strcmp(old_name, new_name) == 0) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return 0;
    }

    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(old_parent_block, old_name, &e)) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }

    if (sdfs_dir_name_exists(new_parent_block, new_name)) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }

    uint8_t *source_buffer = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!source_buffer) {
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    if (sdfs_read_block(e.block, source_buffer) != 0) {
        kfree(source_buffer);
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    sdfs_entry_t source_entry;
    if (sdfs_entry_deserialize(source_buffer + e.block_offset,
                               SDFS_BLOCK_SIZE - e.block_offset,
                               &source_entry) <= 1 ||
        source_entry.type != e.type ||
        strcmp(source_entry.name, old_name) != 0 ||
        source_entry.start_block != e.start_block) {
        kfree(source_buffer);
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }
    source_buffer[e.block_offset] = SDFS_TYPE_DEL;

    uint32_t now = sdfs_now();

    sdfs_entry_t ne;
    memset(&ne, 0, sizeof(ne));
    ne.type = e.type;
    ne.name_len = strlen(new_name);
    memcpy(ne.name, new_name, ne.name_len);
    ne.start_block = e.start_block;
    ne.file_size = e.file_size;
    ne.created_time = e.created_time;
    ne.modified_time = now;
    ne.uid = e.uid;
    ne.gid = e.gid;
    ne.permissions = e.permissions;

    uint32_t destination_block = 0;
    uint8_t *destination_buffer = NULL;
    int destination_owned = 0;
    if (sdfs_dir_prepare_rename_add(new_parent_block, &ne, e.block,
                                    source_buffer, &destination_block,
                                    &destination_buffer,
                                    &destination_owned) != 0) {
        kfree(source_buffer);
        sleep_mutex_unlock(&sdfs_mutation_lock);
        return -1;
    }

    uint32_t blocks[2];
    uint8_t *buffers[2];
    uint32_t transaction_count = 1;
    blocks[0] = e.block;
    buffers[0] = source_buffer;
    if (destination_block != e.block) {
        blocks[1] = destination_block;
        buffers[1] = destination_buffer;
        transaction_count = 2;
    }

    int result = sdfs_metadata_write_blocks(blocks, buffers,
                                            transaction_count);
    if (destination_owned) kfree(destination_buffer);
    kfree(source_buffer);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

int sdfs_rename(const char *old_path, const char *new_path) {
    if (!sdfs_mounted || !old_path || !new_path ||
        sdfs_is_root_path(old_path) || sdfs_is_root_path(new_path))
        return -1;
    uint32_t old_parent_block, new_parent_block;
    char old_name[256], new_name[256];
    if (sdfs_resolve_parent(old_path, &old_parent_block, old_name) != 0 ||
        sdfs_resolve_parent(new_path, &new_parent_block, new_name) != 0)
        return -1;
    return sdfs_rename_entry(old_parent_block, old_name,
                             new_parent_block, new_name);
}

static int sdfs_path_info_inner(const char *path, int *is_dir_out,
                                uint32_t *size_out) {
    if (!sdfs_mounted || !path) return -1;
    if (sdfs_is_root_path(path)) {
        if (is_dir_out) *is_dir_out = 1;
        if (size_out) *size_out = 0;
        return 0;
    }

    sdfs_entry_t e;
    if (!sdfs_resolve_full(path, &e)) return -1;
    if (is_dir_out) *is_dir_out = (e.type == SDFS_TYPE_DIR);
    if (size_out) *size_out = e.file_size;
    return 0;
}

int sdfs_path_info(const char *path, int *is_dir_out, uint32_t *size_out) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    int result = sdfs_path_info_inner(path, is_dir_out, size_out);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

static int sdfs_list_dir_entry_inner(const char *path, int index,
                                     char *name_out, int *is_dir_out,
                                     uint32_t *size_out) {
    if (!sdfs_mounted || !path) return -1;

    uint32_t dir_block;
    if (sdfs_is_root_path(path)) {
        dir_block = disk_root_block;
    } else {
        sdfs_entry_t e;
        if (!sdfs_resolve_full(path, &e)) return -1;
        if (e.type != SDFS_TYPE_DIR) return -1;
        dir_block = e.start_block;
    }

    sdfs_entry_t out;
    if (!sdfs_dir_find_by_index(dir_block, index, &out)) return -1;

    if (name_out) {
        memcpy(name_out, out.name, out.name_len);
        name_out[out.name_len] = '\0';
    }
    if (is_dir_out) *is_dir_out = (out.type == SDFS_TYPE_DIR);
    if (size_out) *size_out = out.file_size;
    return 0;
}

int sdfs_list_dir_entry(const char *path, int index, char *name_out,
                        int *is_dir_out, uint32_t *size_out) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    int result = sdfs_list_dir_entry_inner(path, index, name_out, is_dir_out,
                                           size_out);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

/* ============================================================
 * V2 permissions API
 * ============================================================ */

static int sdfs_set_permissions_inner(const char *path, uint16_t perm) {
    if (!sdfs_mounted || !path || (perm & ~0777u)) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;

    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(parent_block, name, &e)) return -1;

    e.permissions = perm;
    e.modified_time = sdfs_now();
    uint8_t *block = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block) return -1;
    int rc = -1;
    if (sdfs_read_block(e.block, block) == 0 &&
        sdfs_entry_serialize(block + e.block_offset,
                             SDFS_BLOCK_SIZE - e.block_offset, &e) >= 0)
        rc = sdfs_metadata_write(e.block, block);
    kfree(block);
    return rc;
}

static int sdfs_set_owner_inner(const char *path, uint16_t uid, uint16_t gid) {
    if (!sdfs_mounted || !path || sdfs_is_root_path(path)) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;

    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(parent_block, name, &e)) return -1;
    e.uid = uid;
    e.gid = gid;
    e.modified_time = sdfs_now();

    uint8_t *block = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block) return -1;
    int rc = -1;
    if (sdfs_read_block(e.block, block) == 0 &&
        sdfs_entry_serialize(block + e.block_offset,
                             SDFS_BLOCK_SIZE - e.block_offset, &e) >= 0)
        rc = sdfs_metadata_write(e.block, block);
    kfree(block);
    return rc;
}

int sdfs_set_owner(const char *path, uint16_t uid, uint16_t gid) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    int result = sdfs_set_owner_inner(path, uid, gid);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

int sdfs_get_permissions(const char *path, uint16_t *perm) {
    if (!sdfs_mounted || !path || !perm) return -1;

    if (sdfs_is_root_path(path)) {
        *perm = 0777;
        return 0;
    }

    sdfs_entry_t e;
    if (!sdfs_resolve_full(path, &e)) return -1;
    *perm = sdfs_permission_mode(e.permissions);
    return 0;
}

int sdfs_set_permissions(const char *path, uint16_t perm) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    int result = sdfs_set_permissions_inner(path, perm);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

static int sdfs_get_metadata_inner(const char *path, uint16_t *uid,
                                   uint16_t *gid, uint16_t *permissions) {
    if (!sdfs_mounted || !path) return -1;
    if (sdfs_is_root_path(path)) {
        if (uid) *uid = 0;
        if (gid) *gid = 0;
        if (permissions) *permissions = 0777;
        return 0;
    }
    sdfs_entry_t entry;
    if (!sdfs_resolve_full(path, &entry)) return -1;
    if (uid) *uid = entry.uid;
    if (gid) *gid = entry.gid;
    if (permissions) *permissions = sdfs_permission_mode(entry.permissions);
    return 0;
}

int sdfs_get_metadata(const char *path, uint16_t *uid, uint16_t *gid,
                      uint16_t *permissions) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -1;
    int result = sdfs_get_metadata_inner(path, uid, gid, permissions);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

/* ============================================================
 * VFS callbacks
 * ============================================================ */

static int sdfs_open(fs_node_t *node) {
    if (!node || !node->inode) return -1;
    spinlock_acquire(&sdfs_open_ref_lock);
    for (sdfs_open_ref_t *ref = sdfs_open_refs; ref; ref = ref->next) {
        if (ref->start_block == node->inode) {
            ref->references++;
            ref->size = node->length;
            spinlock_release(&sdfs_open_ref_lock);
            return 0;
        }
    }
    spinlock_release(&sdfs_open_ref_lock);

    sdfs_open_ref_t *created = (sdfs_open_ref_t *)kmalloc(sizeof(*created));
    if (!created) return -1;
    memset(created, 0, sizeof(*created));
    created->start_block = node->inode;
    created->size = node->length;
    created->references = 1;
    created->is_dir = (node->flags & FS_DIRECTORY) != 0;

    spinlock_acquire(&sdfs_open_ref_lock);
    for (sdfs_open_ref_t *ref = sdfs_open_refs; ref; ref = ref->next) {
        if (ref->start_block == node->inode) {
            ref->references++;
            ref->size = node->length;
            spinlock_release(&sdfs_open_ref_lock);
            kfree(created);
            return 0;
        }
    }
    created->next = sdfs_open_refs;
    sdfs_open_refs = created;
    spinlock_release(&sdfs_open_ref_lock);
    return 0;
}

static void sdfs_close(fs_node_t *node) {
    if (!node) return;
    sdfs_open_ref_t *released = NULL;
    spinlock_acquire(&sdfs_open_ref_lock);
    sdfs_open_ref_t **link = &sdfs_open_refs;
    while (*link && (*link)->start_block != node->inode)
        link = &(*link)->next;
    if (*link) {
        sdfs_open_ref_t *ref = *link;
        if (ref->references) ref->references--;
        if (!ref->references) {
            *link = ref->next;
            released = ref;
        }
    }
    spinlock_release(&sdfs_open_ref_lock);
    if (released) {
        if (released->unlinked)
            sdfs_free_unlinked_chain(released->start_block, released->size,
                                     released->is_dir);
        kfree(released);
    }
}

static int sdfs_mark_open_unlinked(uint32_t start_block, uint32_t size,
                                   int is_dir) {
    spinlock_acquire(&sdfs_open_ref_lock);
    for (sdfs_open_ref_t *ref = sdfs_open_refs; ref; ref = ref->next) {
        if (ref->start_block == start_block) {
            ref->size = size;
            ref->is_dir = is_dir != 0;
            ref->unlinked = 1;
            spinlock_release(&sdfs_open_ref_lock);
            return 1;
        }
    }
    spinlock_release(&sdfs_open_ref_lock);
    return 0;
}

static void sdfs_free_unlinked_chain(uint32_t start_block, uint32_t size,
                                     int is_dir) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return;
    int rc = is_dir ? (sdfs_free_block(start_block), 0)
                    : sdfs_free_file_chain(start_block, size);
    if (rc == 0 && sdfs_flush_bitmap() != 0)
        serial_print("SDFS: unlinked blocks remain allocated after close\n");
    else if (rc != 0)
        serial_print("SDFS: unlinked block cleanup deferred after close\n");
    sleep_mutex_unlock(&sdfs_mutation_lock);
}

static uint32_t sdfs_read_inner(fs_node_t *node, uint32_t offset,
                                uint32_t size, uint8_t *buffer) {
    if (!node || !(node->flags & FS_FILE)) return 0;

    uint32_t start_block = node->inode;
    uint32_t file_size = node->length;

    if (offset >= file_size) return 0;
    if (offset + size > file_size) size = file_size - offset;

    uint32_t block_index = offset / 4092;
    uint32_t offset_in_block = offset % 4092;

    uint8_t *block_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block_buf) return 0;

    uint32_t cur_idx = 0;
    uint32_t cur_block = start_block;
    if (start_block != 0 &&
        sdfs_chain_cache_start == start_block &&
        sdfs_chain_cache_ramdisk == sdfs_ramdisk &&
        block_index >= sdfs_chain_cache_idx &&
        sdfs_chain_cache_phys != 0) {
        cur_idx = sdfs_chain_cache_idx;
        cur_block = sdfs_chain_cache_phys;
    }

    while (cur_idx < block_index && cur_block != 0) {
        if (sdfs_read_block(cur_block, block_buf) != 0) {
            kfree(block_buf);
            return 0;
        }
        memcpy(&cur_block, block_buf, 4);
        cur_idx++;
    }
    if (cur_block == 0) {
        kfree(block_buf);
        return 0;
    }

    uint32_t bytes_read = 0;
    uint32_t remaining = size;

    while (remaining > 0 && cur_block != 0) {
        if (sdfs_read_block(cur_block, block_buf) != 0) break;

        sdfs_chain_cache_start = start_block;
        sdfs_chain_cache_idx = cur_idx;
        sdfs_chain_cache_phys = cur_block;
        sdfs_chain_cache_ramdisk = sdfs_ramdisk;

        uint32_t to_copy = 4092 - offset_in_block;
        if (to_copy > remaining) to_copy = remaining;

        memcpy(buffer + bytes_read, block_buf + 4 + offset_in_block, to_copy);
        bytes_read += to_copy;
        remaining -= to_copy;
        offset_in_block = 0;

        uint32_t next_block = 0;
        memcpy(&next_block, block_buf, 4);
        cur_block = next_block;
        cur_idx++;
    }

    kfree(block_buf);
    return bytes_read;
}

static uint32_t sdfs_read(fs_node_t *node, uint32_t offset, uint32_t size,
                          uint8_t *buffer) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return 0;
    uint32_t result = sdfs_read_inner(node, offset, size, buffer);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

static uint32_t sdfs_write_inner(fs_node_t *node, uint32_t offset,
                                 uint32_t size, uint8_t *buffer) {
    if (!node || !(node->flags & FS_FILE)) return 0;

    uint32_t start_block = node->inode;
    uint32_t file_size = node->length;

    if (offset > file_size) return 0;

    uint32_t block_index = offset / 4092;
    uint32_t offset_in_block = offset % 4092;

    uint32_t cur_block = start_block;
    for (uint32_t i = 0; i < block_index; i++) {
        uint32_t next = sdfs_get_next_block(cur_block);
        if (next == 0) {
            next = sdfs_append_block(start_block);
            if (next == 0) return 0;
        }
        cur_block = next;
    }

    uint32_t bytes_written = 0;
    uint32_t remaining = size;
    uint8_t *block_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block_buf) return 0;

    while (remaining > 0) {
        sdfs_read_block(cur_block, block_buf);

        uint32_t to_write = 4092 - offset_in_block;
        if (to_write > remaining) to_write = remaining;

        memcpy(block_buf + 4 + offset_in_block, buffer + bytes_written, to_write);
        sdfs_write_block(cur_block, block_buf);

        bytes_written += to_write;
        remaining -= to_write;
        offset_in_block = 0;

        if (remaining > 0) {
            uint32_t next = sdfs_get_next_block(cur_block);
            if (next == 0) {
                next = sdfs_append_block(start_block);
                if (next == 0) break;
            }
            cur_block = next;
        }
    }

    kfree(block_buf);

    if (offset + bytes_written > file_size) {
        node->length = offset + bytes_written;
        uint32_t dir_block = node->impl;
        uint16_t dir_offset = (uint16_t)node->impl_offset;
        if (dir_block != 0) {
            uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
            if (buf) {
                sdfs_read_block(dir_block, buf);
                uint32_t rem = SDFS_BLOCK_SIZE - dir_offset;
                sdfs_entry_t e;
                if (sdfs_entry_deserialize(buf + dir_offset, rem, &e) > 1) {
                    e.file_size = node->length;
                    e.modified_time = sdfs_now();
                    sdfs_entry_serialize(buf + dir_offset, rem, &e);
                    sdfs_metadata_write(dir_block, buf);
                }
                kfree(buf);
            }
        }
    }

    return bytes_written;
}

static uint32_t sdfs_write(fs_node_t *node, uint32_t offset, uint32_t size,
                           uint8_t *buffer) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return 0;
    uint32_t result = sdfs_write_inner(node, offset, size, buffer);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

static struct dirent *sdfs_readdir_inner(fs_node_t *node, uint32_t index) {
    if (!node || !(node->flags & FS_DIRECTORY)) return NULL;
    uint32_t dir_block = node->inode;

    sdfs_entry_t e;
    if (!sdfs_dir_find_by_index(dir_block, index, &e)) return NULL;

    memset(&sdfs_dirent, 0, sizeof(sdfs_dirent));
    memcpy(sdfs_dirent.name, e.name, e.name_len);
    sdfs_dirent.name[e.name_len] = '\0';
    sdfs_dirent.ino = e.start_block;
    return &sdfs_dirent;
}

static struct dirent *sdfs_readdir(fs_node_t *node, uint32_t index) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return NULL;
    struct dirent *result = sdfs_readdir_inner(node, index);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

static int sdfs_truncate_inner(fs_node_t *node) {
    if (!node || !(node->flags & FS_FILE)) return -22;
    uint32_t old_start = node->inode;
    uint32_t old_size = node->length;
    uint32_t dir_block = node->impl;
    uint16_t dir_offset = (uint16_t)node->impl_offset;
    if (!dir_block) return -5;

    spinlock_acquire(&sdfs_open_ref_lock);
    int already_open = 0;
    for (sdfs_open_ref_t *ref = sdfs_open_refs; ref; ref = ref->next) {
        if (ref->start_block == old_start && ref->references) {
            already_open = 1;
            break;
        }
    }
    spinlock_release(&sdfs_open_ref_lock);
    if (already_open) return -16;

    /* Prepare an empty replacement first. The directory entry is the commit
     * point, so a pre-commit failure leaves the old contents reachable. */
    static uint8_t empty = 0;
    uint32_t new_start = 0, *new_blocks = NULL, new_count = 0;
    if (sdfs_allocate_file_chain(&empty, 0, &new_start, &new_blocks,
                                 &new_count) != 0)
        return -28;
    if (sdfs_flush_device() != 0 || sdfs_flush_bitmap() != 0) {
        sdfs_free_block_list(new_blocks, new_count);
        kfree(new_blocks);
        return -5;
    }

    uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!buf || sdfs_read_block(dir_block, buf) != 0) {
        if (buf) kfree(buf);
        sdfs_free_block_list(new_blocks, new_count);
        kfree(new_blocks);
        return -12;
    }
    uint32_t remaining = SDFS_BLOCK_SIZE - dir_offset;
    sdfs_entry_t e;
    if (sdfs_entry_deserialize(buf + dir_offset, remaining, &e) <= 1 ||
        e.type != SDFS_TYPE_FILE || e.start_block != old_start) {
        kfree(buf);
        sdfs_free_block_list(new_blocks, new_count);
        kfree(new_blocks);
        return -5;
    }
    e.start_block = new_start;
    e.file_size = 0;
    e.modified_time = sdfs_now();
    if (sdfs_entry_serialize(buf + dir_offset, remaining, &e) < 0 ||
        sdfs_metadata_write(dir_block, buf) != 0) {
        /* Metadata may have committed despite an I/O error; retain the new
         * allocation until recovery determines which entry is live. */
        kfree(buf);
        kfree(new_blocks);
        return -5;
    }
    kfree(buf);
    kfree(new_blocks);

    node->inode = new_start;
    node->length = 0;
    if (!sdfs_mark_open_unlinked(old_start, old_size, 0) &&
        sdfs_free_file_chain(old_start, old_size) == 0) {
        if (sdfs_flush_bitmap() != 0)
            serial_print("SDFS: truncate committed; old blocks may leak\n");
    } else if (old_size) {
        serial_print("SDFS: truncate committed; old chain cleanup deferred\n");
    }
    return 0;
}

static int sdfs_truncate(fs_node_t *node) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -5;
    int result = sdfs_truncate_inner(node);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

/* SDFS has no separate metadata/data ordering primitive, so both Linux
 * fsync and fdatasync flush pending bitmap changes and the device cache. */
static int sdfs_sync(fs_node_t *node, int data_only) {
    (void)node;
    (void)data_only;
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return -5;
    int result = sdfs_flush_bitmap();
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result == 0 ? 0 : -5;
}

static fs_node_t *sdfs_finddir_inner(fs_node_t *node, const char *name) {
    if (!node || !(node->flags & FS_DIRECTORY) || !name) return NULL;
    uint32_t dir_block = node->inode;

    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(dir_block, name, &e)) return NULL;

    fs_node_t *res = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(res, 0, sizeof(fs_node_t));
    memcpy(res->name, e.name, e.name_len);
    res->name[e.name_len] = '\0';
    res->inode = e.start_block;
    res->length = e.file_size;
    res->mask = sdfs_permission_mode(e.permissions);
    res->uid = e.uid;
    res->gid = e.gid;
    res->impl = e.block;
    res->impl_offset = e.block_offset;
    res->flags = (e.type == SDFS_TYPE_DIR) ? FS_DIRECTORY : FS_FILE;
    res->read = sdfs_read;
    res->write = sdfs_write;
    res->readdir = sdfs_readdir;
    res->finddir = sdfs_finddir;
    res->create = sdfs_vfs_create;
    res->create_as = sdfs_vfs_create_as;
    res->mkdir = sdfs_vfs_mkdir;
    res->unlink = sdfs_vfs_unlink;
    res->rmdir = sdfs_vfs_rmdir;
    res->rename = sdfs_vfs_rename;
    res->open = sdfs_open;
    res->close = sdfs_close;
    res->truncate = sdfs_truncate;
    res->sync = sdfs_sync;
    return res;
}

static fs_node_t *sdfs_finddir(fs_node_t *node, const char *name) {
    if (sleep_mutex_lock(&sdfs_mutation_lock) != 0) return NULL;
    fs_node_t *result = sdfs_finddir_inner(node, name);
    sleep_mutex_unlock(&sdfs_mutation_lock);
    return result;
}

static fs_node_t *sdfs_vfs_create(fs_node_t *dir_node, const char *name, uint32_t flags) {
    return sdfs_vfs_create_as(dir_node, name, flags, 0, 0,
                              SDFS_DEFAULT_PERM);
}

static fs_node_t *sdfs_vfs_create_as(fs_node_t *dir_node, const char *name,
                                     uint32_t flags, uint16_t uid,
                                     uint16_t gid, uint16_t mode) {
    (void)flags;
    if (!dir_node || !(dir_node->flags & FS_DIRECTORY) || !name) return NULL;
    if (sdfs_create_entry_at(dir_node->inode, name, SDFS_TYPE_FILE,
                             uid, gid, mode) != 0) return NULL;
    return sdfs_finddir(dir_node, name);
}

static fs_node_t *sdfs_vfs_mkdir(fs_node_t *dir_node, const char *name,
                                uint16_t uid, uint16_t gid, uint16_t mode) {
    if (!dir_node || !(dir_node->flags & FS_DIRECTORY) || !name) return NULL;
    if (sdfs_create_entry_at(dir_node->inode, name, SDFS_TYPE_DIR,
                             uid, gid, mode) != 0) return NULL;
    return sdfs_finddir(dir_node, name);
}

static int sdfs_vfs_unlink(fs_node_t *dir_node, const char *name) {
    if (!dir_node || !(dir_node->flags & FS_DIRECTORY) || !name) return -1;
    return sdfs_delete_entry(dir_node->inode, name, 0);
}

static int sdfs_vfs_rmdir(fs_node_t *dir_node, const char *name) {
    if (!dir_node || !(dir_node->flags & FS_DIRECTORY) || !name) return -1;
    return sdfs_delete_entry(dir_node->inode, name, 1);
}

static int sdfs_vfs_rename(fs_node_t *old_dir, const char *old_name,
                           fs_node_t *new_dir, const char *new_name) {
    if (!old_dir || !new_dir || !(old_dir->flags & FS_DIRECTORY) ||
        !(new_dir->flags & FS_DIRECTORY) || !old_name || !new_name)
        return -1;
    return sdfs_rename_entry(old_dir->inode, old_name,
                             new_dir->inode, new_name);
}
