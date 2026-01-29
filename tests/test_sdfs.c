#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define SDFS_BLOCK_SIZE 4096

uint32_t disk_root_block = 1;
uint8_t disk[10][SDFS_BLOCK_SIZE];

typedef struct {
  uint32_t magic;
  uint16_t entry_count;
  uint32_t next_block;
  uint8_t data[4086];
} __attribute__((packed)) sdfs_dir_block_t;

typedef struct {
  uint8_t type;
  uint8_t name_len;
  char name[256];
  uint32_t start_block;
  uint32_t file_size;
  uint32_t timestamp;
} sdfs_entry_t;

int sdfs_entry_serialize(uint8_t *buf, uint32_t max_len, sdfs_entry_t e) {
  uint32_t needed = 2 + e.name_len + 12;
  if (max_len < needed) return -1;
  buf[0] = e.type;
  buf[1] = e.name_len;
  memcpy(buf + 2, e.name, e.name_len);
  
  uint8_t *p = buf + 2 + e.name_len;
  p[0] = (e.start_block) & 0xFF; p[1] = (e.start_block >> 8) & 0xFF;
  p[2] = (e.start_block >> 16) & 0xFF; p[3] = (e.start_block >> 24) & 0xFF;
  p += 4;
  p[0] = (e.file_size) & 0xFF; p[1] = (e.file_size >> 8) & 0xFF;
  p[2] = (e.file_size >> 16) & 0xFF; p[3] = (e.file_size >> 24) & 0xFF;
  p += 4;
  p[0] = (e.timestamp) & 0xFF; p[1] = (e.timestamp >> 8) & 0xFF;
  p[2] = (e.timestamp >> 16) & 0xFF; p[3] = (e.timestamp >> 24) & 0xFF;
  return needed;
}

int sdfs_entry_deserialize(uint8_t *buf, uint32_t max_len, sdfs_entry_t *e) {
  if (max_len < 2) return -1;
  e->type = buf[0];
  e->name_len = buf[1];
  uint32_t needed = 2 + e->name_len + 12;
  if (max_len < needed) return -1;
  memcpy(e->name, buf + 2, e->name_len);
  e->name[e->name_len] = '\0';
  uint8_t *p = buf + 2 + e->name_len;
  e->start_block = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); p += 4;
  e->file_size = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); p += 4;
  e->timestamp = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
  return needed;
}

int sdfs_dir_find_entry(uint32_t dir_block, const char *name, sdfs_entry_t *out) {
  uint32_t cur = dir_block;
  while (cur != 0) {
    uint8_t *buf = disk[cur];
    sdfs_dir_block_t *db = (sdfs_dir_block_t *)buf;
    uint8_t *pos = db->data;
    uint32_t remaining = 4086;

    for (int i = 0; i < db->entry_count; i++) {
      if (remaining < 2) break;
      uint8_t type = pos[0];
      uint8_t name_len = pos[1];
      uint32_t entry_size = 2 + name_len + 12;

      if (type != 0 && type != 3) {
        if (name_len == strlen(name) && memcmp(pos + 2, name, name_len) == 0) {
          if (out) sdfs_entry_deserialize(pos, remaining, out);
          return 1;
        }
      }
      pos += entry_size;
      remaining -= entry_size;
    }
    cur = db->next_block;
  }
  return 0;
}

int sdfs_resolve_parent(const char *path, uint32_t *parent_block_out, char *name_out) {
  char *tmp = malloc(512);
  strcpy(tmp, path);
  char *last_slash = NULL;
  for (char *p = tmp; *p; p++) if (*p == '/') last_slash = p;
  char *leaf = last_slash + 1;
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
      free(tmp); return -1;
    }
    cur_block = e.start_block;
    *next_slash = saved;
    comp = next_slash + 1;
  }
  *parent_block_out = cur_block;
  strcpy(name_out, leaf);
  free(tmp);
  return 0;
}

int main() {
    // block 1 is root
    sdfs_dir_block_t *root = (sdfs_dir_block_t*)disk[1];
    root->entry_count = 1;
    sdfs_entry_t e;
    e.type = 2; // dir
    e.name_len = 5;
    strcpy(e.name, " teste)
