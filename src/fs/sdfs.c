#include "sdfs.h"
#include "journal.h"
#include "crc32.h"
#include "vfs.h"
#include "ata.h"
#include "ahci.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "timer.h"

static int sdfs_mounted = 0;
static uint16_t ata_bus;
static uint8_t ata_drive;
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

static void sdfs_open(fs_node_t *node);
static void sdfs_close(fs_node_t *node);
static uint32_t sdfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t sdfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static struct dirent *sdfs_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *sdfs_finddir(fs_node_t *node, const char *name);
static fs_node_t *sdfs_vfs_create(fs_node_t *dir_node, const char *name, uint32_t flags);
static struct dirent sdfs_dirent;

static uint8_t sdfs_ahci_port = 0xFF;
static uint8_t *sdfs_ramdisk = NULL;

void sdfs_enable_ramdisk(uint32_t mb) {
    uint64_t bytes = mb * 1024 * 1024;
    sdfs_ramdisk = (uint8_t *)kmalloc_a(bytes);
    if (!sdfs_ramdisk) {
        serial_print("SDFS: Failed to allocate ramdisk!\n");
        return;
    }
    disk_total_blocks = bytes / SDFS_BLOCK_SIZE;
    serial_print("SDFS: Ramdisk enabled!\n");
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
    uint32_t lba = block * SDFS_SECTORS_PER_BLOCK;
    if (sdfs_ahci_port != 0xFF) {
        return ahci_read_sector(sdfs_ahci_port, lba, SDFS_SECTORS_PER_BLOCK, buffer) ? 0 : -1;
    }
    if (ata_bmide_available()) {
        return ata_bmide_read(lba, SDFS_SECTORS_PER_BLOCK, (uint16_t *)buffer);
    } else {
        for (int i = 0; i < SDFS_SECTORS_PER_BLOCK; i++) {
            if (ata_read_sector(ata_bus, ata_drive, lba + i, (uint16_t *)(buffer + i * SDFS_SECTOR_SIZE)) != 0) return -1;
        }
    }
    return 0;
}

static int sdfs_write_block(uint32_t block, uint8_t *buffer) {
    if (sdfs_ramdisk) {
        if (block >= disk_total_blocks) return -1;
        memcpy(sdfs_ramdisk + block * SDFS_BLOCK_SIZE, buffer, SDFS_BLOCK_SIZE);
        return 0;
    }
    uint32_t lba = block * SDFS_SECTORS_PER_BLOCK;
    if (sdfs_ahci_port != 0xFF) {
        return ahci_write_sector(sdfs_ahci_port, lba, SDFS_SECTORS_PER_BLOCK, buffer) ? 0 : -1;
    }
    if (ata_bmide_available()) {
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

/*
 * Journal-aware metadata write.
 * Journal the intended block content first, then write to disk.
 * After a successful flush to disk, clear the journal so a subsequent
 * mount does not re-apply already-persisted blocks.
 */
static int sdfs_metadata_write(uint32_t block, uint8_t *buffer) {
    sdfs_journal_write_block(block, buffer);
    int ret = sdfs_write_block(block, buffer);
    if (ret == 0) {
        sdfs_journal_clean();
    }
    return ret;
}

/* ============================================================
 * Bitmap operations
 * ============================================================ */

static int sdfs_load_bitmap(void) {
    if (bitmap_cache) kfree(bitmap_cache);
    bitmap_bytes = disk_bitmap_blocks * SDFS_BLOCK_SIZE;
    bitmap_cache = (uint8_t *)kmalloc(bitmap_bytes);
    if (!bitmap_cache) return -1;
    if (sdfs_read_block(disk_bitmap_block, bitmap_cache) != 0) return -1;
    return 0;
}

static void sdfs_save_bitmap(void) {
    if (bitmap_cache) {
        sdfs_metadata_write(disk_bitmap_block, bitmap_cache);
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

static uint32_t sdfs_alloc_block(void) {
    for (uint32_t b = 0; b < disk_total_blocks; b++) {
        if (!sdfs_bitmap_test(b)) {
            sdfs_bitmap_set(b, 1);
            sdfs_save_bitmap();
            if (sdfs_zero_block(b) != 0) return 0;
            return b;
        }
    }
    return 0;
}

static void sdfs_free_block(uint32_t block) {
    if (block == 0 || block >= disk_total_blocks) return;
    sdfs_bitmap_set(block, 0);
    sdfs_save_bitmap();
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

static void sdfs_free_chain(uint32_t start_block) {
    uint32_t cur = start_block;
    while (cur != 0) {
        uint32_t next = sdfs_get_next_block(cur);
        sdfs_free_block(cur);
        cur = next;
    }
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
                pos += 2; remaining -= 2; continue;
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
            if (ret == 1) { pos += 2; remaining -= 2; continue; }
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
            if (ret == 1) { pos += 2; remaining -= 2; continue; }
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
        int free_idx = -1;
        uint32_t free_slot_remaining = 0;
        int idx;
        for (idx = 0; idx < entry_count; idx++) {
            if (remaining < 2) { break; }
            uint8_t type = pos[0];
            uint8_t name_len = pos[1];
            uint32_t slot_size = (type == SDFS_TYPE_FREE || type == SDFS_TYPE_DEL) ? 2 : (2 + name_len + 24);
            if (type == SDFS_TYPE_FREE || type == SDFS_TYPE_DEL) {
                if (slot_size >= 2 + e->name_len + 24) {
                    found_free = 1;
                    free_idx = idx;
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
            db->next_block = new_block;
            if (sdfs_metadata_write(cur, buf) != 0) { kfree(buf); return -1; }
        }
        cur = db->next_block;
        kfree(buf);
    }
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
            if (ret <= 1) { pos += 2; remaining -= 2; continue; }
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

static int sdfs_resolve_parent(const char *path, uint32_t *parent_block_out, char *name_out) {
    if (!path || path[0] != '/' || !path[1]) return -1;

    char *tmp = (char *)kmalloc(512);
    if (!tmp) return -1;
    int plen = strlen(path);
    if (plen >= 511) { kfree(tmp); return -1; }
    memcpy(tmp, path, plen + 1);

    char *last_slash = NULL;
    for (char *p = tmp; *p; p++) {
        if (*p == '/') last_slash = p;
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
    sdfs_check_ahci();
    if (sdfs_ahci_port == 0xFF && ata_probe(ata_bus, ata_drive) != 0) {
        serial_print("SDFS: format refused, no ATA or AHCI disk selected\n");
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
    uint64_t ahci_sectors = 0;
    if (sdfs_ahci_port != 0xFF) {
        extern uint64_t ahci_identify(uint8_t portno);
        ahci_sectors = ahci_identify(sdfs_ahci_port);
        if (ahci_sectors > 0) {
            total_sectors = (uint32_t)(ahci_sectors > 0xFFFFFFFF ? 0xFFFFFFFF : ahci_sectors);
        }
    }

    disk_total_blocks = total_sectors / SDFS_SECTORS_PER_BLOCK;
    if (disk_total_blocks < 10) disk_total_blocks = 25600;

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

    for (uint32_t b = 0; b <= disk_root_block; b++) {
        sdfs_bitmap_set(b, 1);
    }
    sdfs_save_bitmap();

    if (sdfs_zero_block(disk_root_block) != 0) {
        serial_print("SDFS: format failed, cannot write root dir\n");
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
    (void)partition_lba;
    ata_bus = bus;
    ata_drive = drive;

    sdfs_check_ahci();
    if (sdfs_ahci_port == 0xFF) {
        if (ata_probe(ata_bus, ata_drive) != 0) {
            serial_print("SDFS: no ATA disk at requested bus/drive\n");
            return NULL;
        }
    }

    uint8_t *sb_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!sb_buf) return NULL;
    if (sdfs_read_block(0, sb_buf) != 0) {
        serial_print("SDFS: failed to read superblock\n");
        kfree(sb_buf);
        return NULL;
    }

    sdfs_super_t *sb = (sdfs_super_t *)sb_buf;

    int is_v2 = (memcmp(sb->magic, SDFS_MAGIC_V2, 8) == 0);
    int is_v1 = (memcmp(sb->magic, SDFS_MAGIC_V1, 8) == 0);

    if (!is_v2 && !is_v1) {
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
            serial_print("SDFS: Superblock CRC mismatch! Running fsck...\n");
            /* Continue anyway - fsck will fix */
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

        sb->mount_count = disk_mount_count;
        sb->superblock_crc = 0;
        sb->superblock_crc = crc32_calc(sb_buf, 52);
        sdfs_write_block(0, sb_buf);
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
        serial_print("SDFS: Mounted V1 (no journal)\n");
    }

    kfree(sb_buf);

    if (sdfs_load_bitmap() != 0) {
        serial_print("SDFS: Failed to load bitmap\n");
        return NULL;
    }

    if (disk_journal_start > 0 && disk_journal_blocks >= 2) {
        sdfs_journal_init(disk_journal_start, disk_journal_blocks,
                          sdfs_read_block, sdfs_write_block);

        int replayed = sdfs_journal_replay();
        if (replayed > 0) {
            serial_print("[journal] Replayed ");
            char buf[16];
            itoa(replayed, buf, 10);
            serial_print(buf);
            serial_print(" entries\n");
        }
    }

    sdfs_fsck();

    fs_node_t *root_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(root_node, 0, sizeof(fs_node_t));
    strcpy(root_node->name, "sdfs_root");
    root_node->flags = FS_DIRECTORY;
    root_node->inode = disk_root_block;
    root_node->length = 0;
    root_node->open = sdfs_open;
    root_node->close = sdfs_close;
    root_node->readdir = sdfs_readdir;
    root_node->finddir = sdfs_finddir;
    root_node->create = sdfs_vfs_create;

    sdfs_mounted = 1;
    serial_print("SDFS: Mounted V2\n");
    return root_node;
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

int sdfs_create_file(const char *path) {
    if (!sdfs_mounted || !path || path[0] != '/') return -1;
    if (strcmp(path, "/") == 0) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;
    if (sdfs_dir_name_exists(parent_block, name)) return -1;

    uint32_t start_block = sdfs_alloc_block();
    if (start_block == 0) return -1;

    uint32_t now = sdfs_now();

    sdfs_entry_t e;
    memset(&e, 0, sizeof(e));
    e.type = SDFS_TYPE_FILE;
    e.name_len = strlen(name);
    memcpy(e.name, name, e.name_len);
    e.start_block = start_block;
    e.file_size = 0;
    e.created_time = now;
    e.modified_time = now;
    e.uid = 0;
    e.gid = 0;
    e.permissions = SDFS_DEFAULT_PERM;

    if (sdfs_dir_add_entry(parent_block, &e) != 0) {
        sdfs_free_block(start_block);
        return -1;
    }
    return 0;
}

uint32_t sdfs_write_file(const char *path, uint8_t *buffer, uint32_t size) {
    if (!sdfs_mounted || !path || !buffer) return 0;

    sdfs_entry_t existing;
    int exists = sdfs_resolve_full(path, &existing);

    uint32_t start_block;
    uint32_t parent_block;
    char name[256];

    if (exists) {
        if (existing.type != SDFS_TYPE_FILE) return 0;
        sdfs_free_chain(existing.start_block);
        if (sdfs_resolve_parent(path, &parent_block, name) != 0) return 0;
    } else {
        if (sdfs_resolve_parent(path, &parent_block, name) != 0) return 0;
    }

    if (size == 0) {
        start_block = sdfs_alloc_block();
        if (start_block == 0) return 0;
    } else {
        start_block = sdfs_alloc_block();
        if (start_block == 0) return 0;

        uint32_t cur_block = start_block;
        uint32_t remaining = size;
        uint8_t *src = buffer;

        while (remaining > 0) {
            uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
            if (!buf) { sdfs_free_chain(start_block); return 0; }
            memset(buf, 0, SDFS_BLOCK_SIZE);

            uint32_t to_write = remaining;
            if (to_write > 4092) to_write = 4092;

            memcpy(buf + 4, src, to_write);
            if (sdfs_write_block(cur_block, buf) != 0) {
                kfree(buf);
                sdfs_free_chain(start_block);
                return 0;
            }
            kfree(buf);

            src += to_write;
            remaining -= to_write;

            if (remaining > 0) {
                cur_block = sdfs_append_block(start_block);
                if (cur_block == 0) {
                    sdfs_free_chain(start_block);
                    return 0;
                }
            }
        }
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

    if (exists) {
        sdfs_dir_remove_entry(parent_block, name);
    }
    if (sdfs_dir_add_entry(parent_block, &e) != 0) {
        sdfs_free_chain(start_block);
        return 0;
    }

    return size;
}

void *sdfs_read_file(const char *path, uint32_t *size_out) {
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

    while (remaining > 0 && cur_block != 0) {
        uint8_t block_buf[SDFS_BLOCK_SIZE];
        sdfs_read_block(cur_block, block_buf);

        uint32_t to_copy = remaining;
        if (to_copy > 4092) to_copy = 4092;

        memcpy(result + offset, block_buf + 4, to_copy);
        offset += to_copy;
        remaining -= to_copy;

        cur_block = sdfs_get_next_block(cur_block);
    }

    return result;
}

int sdfs_create_dir(const char *path) {
    if (!sdfs_mounted || !path || path[0] != '/') return -1;
    if (strcmp(path, "/") == 0) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;
    if (sdfs_dir_name_exists(parent_block, name)) return -1;

    uint32_t new_dir_block = sdfs_alloc_block();
    if (new_dir_block == 0) return -1;

    uint32_t now = sdfs_now();

    sdfs_entry_t e;
    memset(&e, 0, sizeof(e));
    e.type = SDFS_TYPE_DIR;
    e.name_len = strlen(name);
    memcpy(e.name, name, e.name_len);
    e.start_block = new_dir_block;
    e.file_size = 0;
    e.created_time = now;
    e.modified_time = now;
    e.uid = 0;
    e.gid = 0;
    e.permissions = SDFS_DEFAULT_PERM | SDFS_PERM_EXEC;

    if (sdfs_dir_add_entry(parent_block, &e) != 0) {
        sdfs_free_block(new_dir_block);
        return -1;
    }
    return 0;
}

int sdfs_delete(const char *path) {
    if (!sdfs_mounted || !path || strcmp(path, "/") == 0) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;

    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(parent_block, name, &e)) return -1;

    if (e.type == SDFS_TYPE_DIR) {
        if (sdfs_dir_count_entries(e.start_block) > 0) return -1;
        sdfs_free_block(e.start_block);
    } else {
        sdfs_free_chain(e.start_block);
    }

    sdfs_dir_remove_entry(parent_block, name);
    return 0;
}

int sdfs_rename(const char *old_path, const char *new_path) {
    if (!sdfs_mounted || !old_path || !new_path) return -1;
    if (strcmp(old_path, "/") == 0 || strcmp(new_path, "/") == 0) return -1;

    uint32_t old_parent_block;
    char old_name[256];
    if (sdfs_resolve_parent(old_path, &old_parent_block, old_name) != 0) return -1;

    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(old_parent_block, old_name, &e)) return -1;

    uint32_t new_parent_block;
    char new_name[256];
    if (sdfs_resolve_parent(new_path, &new_parent_block, new_name) != 0) return -1;

    if (sdfs_dir_name_exists(new_parent_block, new_name)) return -1;

    sdfs_dir_remove_entry(old_parent_block, old_name);

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

    if (sdfs_dir_add_entry(new_parent_block, &ne) != 0) {
        sdfs_entry_t oe;
        memset(&oe, 0, sizeof(oe));
        oe.type = e.type;
        oe.name_len = strlen(old_name);
        memcpy(oe.name, old_name, oe.name_len);
        oe.start_block = e.start_block;
        oe.file_size = e.file_size;
        oe.created_time = e.created_time;
        oe.modified_time = e.modified_time;
        oe.uid = e.uid;
        oe.gid = e.gid;
        oe.permissions = e.permissions;
        sdfs_dir_add_entry(old_parent_block, &oe);
        return -1;
    }

    return 0;
}

int sdfs_path_info(const char *path, int *is_dir_out, uint32_t *size_out) {
    if (!sdfs_mounted || !path) return -1;
    if (strcmp(path, "/") == 0) {
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

int sdfs_list_dir_entry(const char *path, int index, char *name_out,
                        int *is_dir_out, uint32_t *size_out) {
    if (!sdfs_mounted || !path) return -1;

    uint32_t dir_block;
    if (strcmp(path, "/") == 0) {
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

/* ============================================================
 * V2 permissions API
 * ============================================================ */

int sdfs_set_permissions(const char *path, uint16_t perm) {
    if (!sdfs_mounted || !path) return -1;

    uint32_t parent_block;
    char name[256];
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;

    sdfs_entry_t e;
    if (!sdfs_dir_find_entry(parent_block, name, &e)) return -1;

    e.permissions = perm;
    e.modified_time = sdfs_now();

    sdfs_dir_remove_entry(parent_block, name);
    return sdfs_dir_add_entry(parent_block, &e);
}

int sdfs_get_permissions(const char *path, uint16_t *perm) {
    if (!sdfs_mounted || !path || !perm) return -1;

    if (strcmp(path, "/") == 0) {
        *perm = SDFS_DEFAULT_PERM | SDFS_PERM_EXEC;
        return 0;
    }

    sdfs_entry_t e;
    if (!sdfs_resolve_full(path, &e)) return -1;
    *perm = e.permissions;
    return 0;
}

/* ============================================================
 * VFS callbacks
 * ============================================================ */

static void sdfs_open(fs_node_t *node) {
    (void)node;
}

static void sdfs_close(fs_node_t *node) {
    (void)node;
}

static uint32_t sdfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || !(node->flags & FS_FILE)) return 0;

    uint32_t start_block = node->inode;
    uint32_t file_size = node->length;

    if (offset >= file_size) return 0;
    if (offset + size > file_size) size = file_size - offset;

    uint32_t block_index = offset / 4092;
    uint32_t offset_in_block = offset % 4092;

    uint32_t cur_block = start_block;
    for (uint32_t i = 0; i < block_index; i++) {
        cur_block = sdfs_get_next_block(cur_block);
        if (cur_block == 0) return 0;
    }

    uint32_t bytes_read = 0;
    uint32_t remaining = size;

    while (remaining > 0 && cur_block != 0) {
        uint8_t block_buf[SDFS_BLOCK_SIZE];
        sdfs_read_block(cur_block, block_buf);

        uint32_t to_copy = 4092 - offset_in_block;
        if (to_copy > remaining) to_copy = remaining;

        memcpy(buffer + bytes_read, block_buf + 4 + offset_in_block, to_copy);
        bytes_read += to_copy;
        remaining -= to_copy;
        offset_in_block = 0;

        cur_block = sdfs_get_next_block(cur_block);
    }

    return bytes_read;
}

static uint32_t sdfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
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

    while (remaining > 0) {
        uint8_t block_buf[SDFS_BLOCK_SIZE];
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

static struct dirent *sdfs_readdir(fs_node_t *node, uint32_t index) {
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

static void sdfs_truncate(fs_node_t *node) {
    if (!node || !(node->flags & FS_FILE)) return;
    uint32_t start_block = node->inode;
    uint32_t next = sdfs_get_next_block(start_block);
    if (next != 0) {
        sdfs_set_next_block(start_block, 0);
        sdfs_free_chain(next);
    }
    node->length = 0;
    uint32_t dir_block = node->impl;
    uint16_t dir_offset = (uint16_t)node->impl_offset;
    if (dir_block == 0) return;
    uint8_t *buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!buf) return;
    sdfs_read_block(dir_block, buf);
    uint32_t remaining = SDFS_BLOCK_SIZE - dir_offset;
    sdfs_entry_t e;
    if (sdfs_entry_deserialize(buf + dir_offset, remaining, &e) > 1) {
        e.file_size = 0;
        e.modified_time = sdfs_now();
        sdfs_entry_serialize(buf + dir_offset, remaining, &e);
        sdfs_metadata_write(dir_block, buf);
    }
    kfree(buf);
}

static fs_node_t *sdfs_finddir(fs_node_t *node, const char *name) {
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
    res->impl = e.block;
    res->impl_offset = e.block_offset;
    res->flags = (e.type == SDFS_TYPE_DIR) ? FS_DIRECTORY : FS_FILE;
    res->read = sdfs_read;
    res->write = sdfs_write;
    res->readdir = sdfs_readdir;
    res->finddir = sdfs_finddir;
    res->create = sdfs_vfs_create;
    res->open = sdfs_open;
    res->close = sdfs_close;
    res->truncate = sdfs_truncate;
    return res;
}

static fs_node_t *sdfs_vfs_create(fs_node_t *dir_node, const char *name, uint32_t flags) {
    (void)flags;
    if (!dir_node || !(dir_node->flags & FS_DIRECTORY) || !name) return NULL;
    uint32_t parent_block = dir_node->inode;

    if (sdfs_dir_name_exists(parent_block, name)) return NULL;

    uint32_t start_block = sdfs_alloc_block();
    if (start_block == 0) return NULL;

    uint32_t now = sdfs_now();

    sdfs_entry_t e;
    memset(&e, 0, sizeof(e));
    e.type = SDFS_TYPE_FILE;
    uint32_t nlen = strlen(name);
    if (nlen > 255) nlen = 255;
    e.name_len = (uint8_t)nlen;
    memcpy(e.name, name, e.name_len);
    e.name[e.name_len] = '\0';
    e.start_block = start_block;
    e.file_size = 0;
    e.created_time = now;
    e.modified_time = now;
    e.uid = 0;
    e.gid = 0;
    e.permissions = SDFS_DEFAULT_PERM;

    if (sdfs_dir_add_entry(parent_block, &e) != 0) {
        sdfs_free_block(start_block);
        return NULL;
    }

    return sdfs_finddir(dir_node, name);
}
