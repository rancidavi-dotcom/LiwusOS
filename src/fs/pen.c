#include "fs/pen.h"
#include "drivers/scsi.h"
#include "drivers/serial.h"
#include "drivers/mp3.h"
#include "fs/fat32.h"
#include "kernel/kheap.h"
#include "kernel/task.h"
#include <stdint.h>
#include <string.h>

/* Compile-time switch: autoplay the first pendrive song right after mount.
 * Off in the final build; playable from the Multimedia app instead. */
#define PEN_TEST_AUTOPLAY 0

static int s_mounted = 0;
static uint32_t s_volume_lba = 0;
static uint32_t s_gen = 0;
static uint32_t s_count = 0;
static char s_names[PEN_MAX_SONGS][PEN_NAME_MAX];
static uint32_t s_sizes[PEN_MAX_SONGS];

static int pen_is_mp3(const char *name) {
  int len = name ? (int)strlen(name) : 0;
  if (len < 5) return 0;
  const char *p = name + len - 4;
  if (p[0] != '.') return 0;
  return (p[1] == 'm' || p[1] == 'M') && (p[2] == 'p' || p[2] == 'P') &&
         (p[3] == '3');
}

static int pen_block_read(uint32_t lba, uint16_t *buffer) {
  return scsi_read_blocks(s_volume_lba + lba, 1, (uint8_t *)buffer);
}

static int pen_block_read_blocks(uint32_t lba, uint32_t count,
                                 uint8_t *buffer) {
  return scsi_read_blocks(s_volume_lba + lba, count, buffer);
}

static int pen_block_write(uint32_t lba, uint16_t *buffer) {
  (void)lba;
  (void)buffer;
  return -1; /* read-only */
}

static void pen_rescan_root(void) {
  s_count = 0;
  for (int i = 0; i < 128; i++) {
    char name[PEN_NAME_MAX];
    int is_dir;
    uint32_t size;
    if (!fat32_root_list_entry(i, name, &is_dir, &size)) {
      break;
    }
    if (is_dir || !pen_is_mp3(name)) {
      continue;
    }
    strncpy(s_names[s_count], name, PEN_NAME_MAX - 1);
    s_names[s_count][PEN_NAME_MAX - 1] = '\0';
    s_sizes[s_count] = size;
    s_count++;
    if (s_count >= PEN_MAX_SONGS) {
      break;
    }
  }
  s_gen++;
}

static void pen_detach(void) {
  s_mounted = 0;
  s_count = 0;
  s_volume_lba = 0;
  s_gen++;
  serial_print("pen: pendrive removido\n");
}

static int pen_attach(void) {
  uint8_t *sector0 = (uint8_t *)kmalloc(512);
  uint32_t partition_start = 0;
  uint16_t signature;
  uint8_t partition_type;
  uint32_t pstart;
  fs_node_t *fs = NULL;

  if (!sector0) return -1;

  if (scsi_read_blocks(0, 1, sector0) != 0) {
    kfree(sector0);
    return -1;
  }

  signature = (uint16_t)(sector0[510] | (sector0[511] << 8));
  partition_type = sector0[450];
  pstart = (uint32_t)sector0[454] | ((uint32_t)sector0[455] << 8) |
           ((uint32_t)sector0[456] << 16) | ((uint32_t)sector0[457] << 24);

  if (signature == 0xAA55 && (partition_type == 0x0B || partition_type == 0x0C)) {
    partition_start = pstart;
  }

  s_volume_lba = partition_start;
  fat32_set_block_io(pen_block_read, pen_block_write);
  fat32_set_block_io_blocks(pen_block_read_blocks);
  fs = fat32_mount(0, 0, s_volume_lba);
  kfree(sector0);

  if (!fs) {
    serial_print("pen: montagem FAT32 falhou\n");
    return -1;
  }

  s_mounted = 1;
  pen_rescan_root();
  serial_print("pen: pendrive montado (");
  serial_print_hex(s_count);
  serial_print(" mp3)\n");

#if PEN_TEST_AUTOPLAY
  if (s_count > 0) {
    char req[PEN_NAME_MAX + 8];
    uint32_t nl = strlen(s_names[0]);
    memcpy(req, "/pen/", 5);
    if (nl >= PEN_NAME_MAX + 8 - 6) {
      nl = PEN_NAME_MAX + 8 - 6;
    }
    memcpy(req + 5, s_names[0], nl);
    req[5 + nl] = '\0';
    serial_print("pen: TEST autoplay -> ");
    serial_print(req);
    serial_print("\n");
    audio_song_request(req);
  }
#endif
  return 0;
}

void pen_init(void) {
  scsi_init();
  if (scsi_present()) {
    serial_print("pen: verificando pendrive no boot\n");
    pen_attach();
  } else {
    serial_print("pen: nenhum pendrive no boot\n");
  }
}

void pen_task(void) {
  for (;;) {
    int present = scsi_present();
    if (present && !s_mounted) {
      pen_attach();
    } else if (!present && s_mounted) {
      pen_detach();
    }
    for (int i = 0; i < 20; i++) {
      switch_task();
    }
  }
}

uint32_t pen_song_count(void) { return s_count; }

const char *pen_song_name(uint32_t index) {
  if (index >= s_count) return NULL;
  return s_names[index];
}

void *pen_read_song(const char *name, uint32_t *size_out) {
  char path[PEN_NAME_MAX + 2];
  if (!s_mounted || !name) {
    if (size_out) *size_out = 0;
    return NULL;
  }
  path[0] = '/';
  strncpy(path + 1, name, PEN_NAME_MAX);
  path[PEN_NAME_MAX] = '\0';
  return fat32_read_file_path(path, size_out);
}

uint32_t pen_song_gen(void) { return s_gen; }

int pen_is_connected(void) { return s_mounted; }