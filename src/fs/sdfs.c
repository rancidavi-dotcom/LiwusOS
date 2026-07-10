#include "sdfs.h"
#include "vfs.h"
#include "ata.h"
#include "ahci.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"

// ATA_PRIMARY and ATA_MASTER defined in include/ata.h

static int sdfs_mounted = 0;
static uint16_t ata_bus;
static uint8_t ata_drive;
static uint32_t disk_total_blocks;
static uint32_t disk_bitmap_block;
static uint32_t disk_bitmap_blocks;
static uint32_t disk_root_block;
static uint8_t *bitmap_cache = NULL;
static uint32_t bitmap_bytes;

// --- Forward declarations for VFS callbacks ---
static void sdfs_open(fs_node_t *node);
static void sdfs_close(fs_node_t *node);
static uint32_t sdfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t sdfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static struct dirent *sdfs_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *sdfs_finddir(fs_node_t *node, const char *name);
static fs_node_t *sdfs_vfs_create(fs_node_t *dir_node, const char *name, uint32_t flags);
static struct dirent sdfs_dirent;

// ============================================================
// Low-level block I/O
// ============================================================

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

// ============================================================
// Bitmap operations
// ============================================================

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
    sdfs_write_block(disk_bitmap_block, bitmap_cache);
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
  return 0; // no free blocks
}

static void sdfs_free_block(uint32_t block) {
  if (block == 0 || block >= disk_total_blocks) return;
  sdfs_bitmap_set(block, 0);
  sdfs_save_bitmap();
}

// ============================================================
// Block chain helpers (for files)
// ============================================================

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
  sdfs_write_block(block, buf);
  kfree(buf);
}

static uint32_t sdfs_append_block(uint32_t chain_start) {
  uint32_t new_block = sdfs_alloc_block();
  if (new_block == 0) return 0;

  // Find the end of the chain
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

// ============================================================
// Directory entry helpers
// ============================================================

typedef struct {
  uint8_t type;
  uint8_t name_len;
  char name[256];
  uint32_t start_block;
  uint32_t file_size;
  uint32_t timestamp;
  uint32_t block;          // which block contains this entry
  uint16_t block_offset;   // offset within the block
} sdfs_entry_t;

static int sdfs_entry_serialize(uint8_t *buf, uint32_t buf_size, const sdfs_entry_t *e) {
  uint32_t needed = 2 + e->name_len + 12;
  if (needed > buf_size) return -1;
  buf[0] = e->type;
  buf[1] = e->name_len;
  memcpy(buf + 2, e->name, e->name_len);
  uint32_t v;
  v = e->start_block; memcpy(buf + 2 + e->name_len, &v, 4);
  v = e->file_size;   memcpy(buf + 6 + e->name_len, &v, 4);
  v = e->timestamp;   memcpy(buf + 10 + e->name_len, &v, 4);
  return (int)needed;
}

static int sdfs_entry_deserialize(const uint8_t *buf, uint32_t buf_size, sdfs_entry_t *e) {
  if (buf_size < 2) return -1;
  e->type = buf[0];
  e->name_len = buf[1];
  if (e->type == SDFS_TYPE_FREE || e->type == SDFS_TYPE_DEL) return 1; // skip, still consume 2 bytes
  if ((uint32_t)(2 + e->name_len + 12) > buf_size) return -1;
  e->name[0] = '\0';
  if (e->name_len == 0) return -1;
  memcpy(e->name, buf + 2, e->name_len);
  e->name[e->name_len] = '\0';
  memcpy(&e->start_block, buf + 2 + e->name_len, 4);
  memcpy(&e->file_size, buf + 6 + e->name_len, 4);
  memcpy(&e->timestamp, buf + 10 + e->name_len, 4);
  return 2 + e->name_len + 12;
}

// Read a directory block and call back for each entry
// Returns the number of valid (non-free, non-deleted) entries found
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
      uint32_t entry_size = 2 + name_len + 12;
      if (entry_size > remaining) break;
      if (type == SDFS_TYPE_FILE || type == SDFS_TYPE_DIR) {
        if (name_len > 0) {
          count++;
        }
      }
      pos += entry_size;
      remaining -= entry_size;
    }
    cur = db->next_block;
    kfree(buf);
  }
  return count;
}

// Find entry by name in a directory chain
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
      if (ret == 1) { // free/deleted, skip
        pos += 2;
        remaining -= 2;
        continue;
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

// Find the index-th entry in a directory chain
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
      if (ret == 1) { // free/deleted
        pos += 2;
        remaining -= 2;
        continue;
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

// Check if an entry with `name` exists and is valid in the directory chain
static int sdfs_dir_name_exists(uint32_t dir_block, const char *name) {
  sdfs_entry_t tmp;
  return sdfs_dir_find_entry(dir_block, name, &tmp);
}

// Add an entry to a directory chain
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

    // Find first free/deleted entry or end
    int found_free = 0;
    int idx;
    for (idx = 0; idx < entry_count; idx++) {
      if (remaining < 2) { found_free = 0; break; }
      uint8_t type = pos[0];
      if (type == SDFS_TYPE_FREE || type == SDFS_TYPE_DEL) {
        found_free = 1;
        break;
      }
      uint8_t name_len = pos[1];
      uint32_t entry_size = 2 + name_len + 12;
      if (entry_size > remaining) break;
      pos += entry_size;
      remaining -= entry_size;
    }

    uint32_t entry_size_needed = 2 + e->name_len + 12;

    if (found_free) {
      int ret = sdfs_entry_serialize(pos, remaining, e);
      if (ret < 0) { kfree(buf); return -1; }
      if (sdfs_write_block(cur, buf) != 0) { kfree(buf); return -1; }
      kfree(buf);
      return 0;
    }

    if (remaining >= entry_size_needed) {
      int ret = sdfs_entry_serialize(pos, remaining, e);
      if (ret < 0) { kfree(buf); return -1; }
      db->entry_count = (uint16_t)(entry_count + 1);
      memcpy(buf, db, 6);
      if (sdfs_write_block(cur, buf) != 0) { kfree(buf); return -1; }
      kfree(buf);
      return 0;
    }

    if (db->next_block == 0) {
      uint32_t new_block = sdfs_alloc_block();
      if (new_block == 0) { kfree(buf); return -1; }
      db->next_block = new_block;
      memcpy(buf, db, 6);
      if (sdfs_write_block(cur, buf) != 0) { kfree(buf); return -1; }
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
        if (sdfs_write_block(cur, buf) != 0) { kfree(buf); return -1; }
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

// ============================================================
// Path resolution
// ============================================================

// Split path into parent directory block and the leaf name
// e.g. "/house/localhost/test.txt" -> parent block = root, name = "test.txt"
// Returns 0 on success, -1 on failure
static int sdfs_resolve_parent(const char *path, uint32_t *parent_block_out, char *name_out) {
  if (!path || path[0] != '/' || !path[1]) return -1;

  char *tmp = (char *)kmalloc(512);
  if (!tmp) return -1;
  int plen = strlen(path);
  if (plen >= 511) { kfree(tmp); return -1; }
  memcpy(tmp, path, plen + 1);

  // Find last '/'
  char *last_slash = NULL;
  for (char *p = tmp; *p; p++) {
    if (*p == '/') last_slash = p;
  }
  if (!last_slash) { kfree(tmp); return -1; }

  char *leaf = last_slash + 1;
  if (*leaf == '\0') { kfree(tmp); return -1; }

  uint32_t cur_block = disk_root_block;

  // Walk through path components before the leaf
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

// Resolve full path to an entry
static int sdfs_resolve_full(const char *path, sdfs_entry_t *out) {
  uint32_t parent_block;
  char name[256];
  if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;
  return sdfs_dir_find_entry(parent_block, name, out);
}

// ============================================================
// Public API
// ============================================================

int sdfs_format(void) {
  sdfs_check_ahci();
  if (sdfs_ahci_port == 0xFF && ata_probe(ata_bus, ata_drive) != 0) {
    serial_print("SDFS: format refused, no ATA or AHCI disk selected\n");
    return -1;
  }

  // Read partition table from MBR (sector 0)
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

  // First partition entry at offset 446
  uint32_t part_sectors = *(uint32_t *)(mbr + 458); // size in sectors

  if (part_sectors == 0) {
    part_sectors = 204800; // 100MB
  }

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
  if (disk_total_blocks < 10) disk_total_blocks = 25600; // fallback ~100MB
  
  // Calculate bitmap size
  bitmap_bytes = (disk_total_blocks + 7) / 8;
  disk_bitmap_blocks = (bitmap_bytes + SDFS_BLOCK_SIZE - 1) / SDFS_BLOCK_SIZE;
  if (disk_bitmap_blocks < 1) disk_bitmap_blocks = 1;
  disk_bitmap_block = 1; // always block 1
  disk_root_block = disk_bitmap_block + disk_bitmap_blocks;

  // Make sure we don't overwrite beyond available space
  if (disk_root_block + 1 >= disk_total_blocks) {
    disk_total_blocks = disk_root_block + 3;
    uint32_t new_bitmap_bytes = (disk_total_blocks + 7) / 8;
    disk_bitmap_blocks = (new_bitmap_bytes + SDFS_BLOCK_SIZE - 1) / SDFS_BLOCK_SIZE;
    disk_root_block = disk_bitmap_block + disk_bitmap_blocks;
  }

  // Write superblock
  uint8_t *sb_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
  memset(sb_buf, 0, SDFS_BLOCK_SIZE);
  sdfs_super_t *sb = (sdfs_super_t *)sb_buf;
  memcpy(sb->magic, SDFS_MAGIC, 8);
  sb->block_size = SDFS_BLOCK_SIZE;
  sb->total_blocks = disk_total_blocks;
  sb->bitmap_block = disk_bitmap_block;
  sb->bitmap_blocks = disk_bitmap_blocks;
  sb->root_dir_block = disk_root_block;

  if (sdfs_write_block(0, sb_buf) != 0) {
    serial_print("SDFS: format failed, cannot write superblock\n");
    kfree(sb_buf);
    return -1;
  }
  kfree(sb_buf);

  // Initialize bitmap: mark superblock + bitmap blocks + root dir as used
  bitmap_cache = (uint8_t *)kmalloc(disk_bitmap_blocks * SDFS_BLOCK_SIZE);
  memset(bitmap_cache, 0, disk_bitmap_blocks * SDFS_BLOCK_SIZE);
  bitmap_bytes = disk_bitmap_blocks * SDFS_BLOCK_SIZE;

  for (uint32_t b = 0; b <= disk_root_block; b++) {
    sdfs_bitmap_set(b, 1);
  }
  sdfs_save_bitmap();

  // Initialize root directory block
  if (sdfs_zero_block(disk_root_block) != 0) {
    serial_print("SDFS: format failed, cannot write root dir\n");
    kfree(bitmap_cache);
    bitmap_cache = NULL;
    return -1;
  }

  kfree(bitmap_cache);
  bitmap_cache = NULL;

  serial_print("SDFS: Formatted OK\n");

  return 0;
}

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

  // Read superblock
  uint8_t sb_buf[SDFS_BLOCK_SIZE];
  if (sdfs_read_block(0, sb_buf) != 0) {
    serial_print("SDFS: failed to read superblock\n");
    return NULL;
  }
  sdfs_super_t *sb = (sdfs_super_t *)sb_buf;

  if (memcmp(sb->magic, SDFS_MAGIC, 8) != 0) {
    serial_print("SDFS: Invalid superblock magic\n");
    return NULL;
  }

  disk_total_blocks = sb->total_blocks;
  disk_bitmap_block = sb->bitmap_block;
  disk_bitmap_blocks = sb->bitmap_blocks;
  disk_root_block = sb->root_dir_block;

  if (sdfs_load_bitmap() != 0) {
    serial_print("SDFS: Failed to load bitmap\n");
    return NULL;
  }

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
  serial_print("SDFS: Mounted\n");
  return root_node;
}

int sdfs_is_mounted(void) {
  return sdfs_mounted;
}

int sdfs_create_file(const char *path) {
  if (!sdfs_mounted || !path || path[0] != '/') return -1;
  // Root directory counts as existing
  if (strcmp(path, "/") == 0) return -1;
  
  // Bloqueia gravação na raiz do SDFS, exceto arquivos de sistema
  if (strchr(path + 1, '/') == NULL && strcmp(path, "/.system_installed") != 0) {
      return -1;
  }

  uint32_t parent_block;
  char name[256];
  if (sdfs_resolve_parent(path, &parent_block, name) != 0) return -1;

  // Check not already exists
  if (sdfs_dir_name_exists(parent_block, name)) return -1;

  // Allocate a starting data block
  uint32_t start_block = sdfs_alloc_block();
  if (start_block == 0) return -1;

  sdfs_entry_t e;
  memset(&e, 0, sizeof(e));
  e.type = SDFS_TYPE_FILE;
  e.name_len = strlen(name);
  memcpy(e.name, name, e.name_len);
  e.start_block = start_block;
  e.file_size = 0;
  e.timestamp = 0;

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
    // Free old data blocks
    sdfs_free_chain(existing.start_block);
    parent_block = existing.block; // Actually this is the block containing the entry, not the parent dir. We need the parent.
    // Re-resolve to get parent
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return 0;
  } else {
    // Create new file
    if (sdfs_resolve_parent(path, &parent_block, name) != 0) return 0;
  }

  if (size == 0) {
    // Empty file - allocate at least one block
    start_block = sdfs_alloc_block();
    if (start_block == 0) return 0;
  } else {
    // Allocate first block
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

  // Add or update the directory entry
  sdfs_entry_t e;
  memset(&e, 0, sizeof(e));
  e.type = SDFS_TYPE_FILE;
  e.name_len = strlen(name);
  memcpy(e.name, name, e.name_len);
  e.start_block = start_block;
  e.file_size = size;
  e.timestamp = 0;

  if (exists) {
    // Remove old entry, add new one
    // We need parent_block from above
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

  // Determine how many blocks
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

  sdfs_entry_t e;
  memset(&e, 0, sizeof(e));
  e.type = SDFS_TYPE_DIR;
  e.name_len = strlen(name);
  memcpy(e.name, name, e.name_len);
  e.start_block = new_dir_block;
  e.file_size = 0;
  e.timestamp = 0;

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
    // Check if directory is empty
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

  // Remove old entry
  sdfs_dir_remove_entry(old_parent_block, old_name);

  // Add new entry with same attributes but new name
  sdfs_entry_t ne;
  memset(&ne, 0, sizeof(ne));
  ne.type = e.type;
  ne.name_len = strlen(new_name);
  memcpy(ne.name, new_name, ne.name_len);
  ne.start_block = e.start_block;
  ne.file_size = e.file_size;
  ne.timestamp = e.timestamp;

  if (sdfs_dir_add_entry(new_parent_block, &ne) != 0) {
    // Try to restore old entry
    sdfs_entry_t oe;
    memset(&oe, 0, sizeof(oe));
    oe.type = e.type;
    oe.name_len = strlen(old_name);
    memcpy(oe.name, old_name, oe.name_len);
    oe.start_block = e.start_block;
    oe.file_size = e.file_size;
    oe.timestamp = e.timestamp;
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

// ============================================================
// VFS callbacks
// ============================================================

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

  // Navigate to the block containing the offset
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

  if (offset > file_size) return 0; // no sparse files

  uint32_t block_index = offset / 4092;
  uint32_t offset_in_block = offset % 4092;

  uint32_t cur_block = start_block;
  for (uint32_t i = 0; i < block_index; i++) {
    uint32_t next = sdfs_get_next_block(cur_block);
    if (next == 0) {
      next = sdfs_append_block(start_block); // actually we should append to cur_block but start_block chain works
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
        next = sdfs_append_block(start_block); // append finds the end of the chain from start_block
        if (next == 0) break; // Out of space
        // Wait, sdfs_append_block finds the end of the chain from start_block, which is fine!
        // But the end is cur_block.
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
          sdfs_entry_serialize(buf + dir_offset, rem, &e);
          sdfs_write_block(dir_block, buf);
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
    sdfs_entry_serialize(buf + dir_offset, remaining, &e);
    sdfs_write_block(dir_block, buf);
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

  sdfs_entry_t e;
  memset(&e, 0, sizeof(e));
  e.type = SDFS_TYPE_FILE;
  e.name_len = strlen(name);
  if (e.name_len > 255) e.name_len = 255;
  memcpy(e.name, name, e.name_len);
  e.name[e.name_len] = '\0';
  e.start_block = start_block;
  e.file_size = 0;
  e.timestamp = 0;

  if (sdfs_dir_add_entry(parent_block, &e) != 0) {
    sdfs_free_block(start_block);
    return NULL;
  }

  return sdfs_finddir(dir_node, name);
}
