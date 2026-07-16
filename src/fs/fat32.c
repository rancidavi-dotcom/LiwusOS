#include "fat32.h"
#include "ata.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "vfs.h"

// Variáveis globais para o filesystem montado
static fat32_boot_sector_t boot_sector;
static uint32_t fat_begin_lba;
static uint32_t cluster_begin_lba;
static uint32_t root_dir_first_cluster;
static uint16_t ata_bus;
static uint8_t ata_drive;
static int fat32_mounted = 0;

typedef struct {
  uint32_t dir_cluster;
  uint32_t entry_cluster;
  uint32_t entry_index;
  fat32_directory_entry_t entry;
  int found;
} fat32_lookup_result_t;

// Protótipos de funções internas
static uint32_t fat32_read(fs_node_t *node, uint32_t offset, uint32_t size,
                           uint8_t *buffer);
static void fat32_open(fs_node_t *node);
static void fat32_close(fs_node_t *node);
static struct dirent *fat32_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *fat32_finddir(fs_node_t *node, const char *name);
static void fat32_make_dos_name(const char *name, char dos_name[12]);
static void fat32_entry_name_to_string(const fat32_directory_entry_t *entry,
                                       char *out, uint32_t out_size);
static uint32_t fat32_entry_start_cluster(const fat32_directory_entry_t *entry);
static int fat32_find_entry_in_dir(uint32_t dir_cluster, const char *name,
                                   fat32_lookup_result_t *result);
static int fat32_load_root_dir(uint8_t **buffer_out, uint32_t *size_out,
                               uint32_t *cluster_lba_out);
static int fat32_load_directory(uint32_t start_cluster, uint8_t **buffer_out,
                                uint32_t *size_out);
static uint32_t fat32_next_cluster(uint32_t current_cluster);
static void fat32_free_cluster_chain(uint32_t start_cluster);
static void fat32_write_fat_entry(uint32_t cluster, uint32_t value);

static void fat32_maybe_yield(void) {
  if (current_task) {
    switch_task();
  }
}

// Monta o sistema de arquivos FAT32
fs_node_t *fat32_mount(uint16_t bus, uint8_t drive,
                       uint32_t partition_lba_start) {
  ata_bus = bus;
  ata_drive = drive;

  // Buffer para ler o setor de boot
  uint16_t boot_sector_buffer[256];
  if (ata_read_sector(ata_bus, ata_drive, partition_lba_start,
                      boot_sector_buffer) != 0) {
    serial_print("FAT32: Falha ao ler o setor de boot.\n");
    return NULL;
  }

  memcpy(&boot_sector, boot_sector_buffer, sizeof(fat32_boot_sector_t));

  // Verifica a assinatura "FAT32"
  if (strncmp((char *)boot_sector.BS_FilSysType, "FAT32   ", 8) != 0) {
    serial_print("FAT32: Filesystem nao e FAT32.\n");
    return NULL;
  }

  // Calcula os offsets importantes
  fat_begin_lba = partition_lba_start + boot_sector.BPB_RsvdSecCnt;
  cluster_begin_lba =
      fat_begin_lba + (boot_sector.BPB_NumFATs * boot_sector.BPB_FATSz32);
  root_dir_first_cluster = boot_sector.BPB_RootClus;

  // Cria o nó raiz do VFS para este disco
  fs_node_t *root_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
  memset(root_node, 0, sizeof(fs_node_t));
  strcpy(root_node->name, "fat32_root");
  root_node->flags = FS_DIRECTORY;
  root_node->inode = root_dir_first_cluster;
  root_node->open = fat32_open;
  root_node->close = fat32_close;
  root_node->readdir = fat32_readdir;
  root_node->finddir = fat32_finddir;

  fat32_mounted = 1;

  return root_node;
}

int fat32_is_mounted(void) { return fat32_mounted; }

int fat32_format(int *progress) {
  *progress = 0;
  const uint16_t bus = ATA_PRIMARY;
  const uint8_t drive = ATA_MASTER;

  const uint32_t total_sectors = 204800;
  const uint16_t sector_size = 512;
  uint16_t reserved_sectors;
  uint8_t sectors_per_cluster;
  uint8_t num_fats;
  uint32_t root_cluster;
  uint16_t fsinfo_sector;
  uint16_t backup_boot_sector;

  uint16_t *aligned_buffer = (uint16_t *)kmalloc(sector_size);
  fat32_boot_sector_t *bpb = (fat32_boot_sector_t *)kmalloc(sector_size);

  // 1. Limpa o início do disco
  memset(aligned_buffer, 0, sector_size);
  for (uint32_t i = 0; i < 33; i++) {
    ata_write_sector(bus, drive, i, aligned_buffer);
    *progress = (i * 10) / 33;
    fat32_maybe_yield();
  }

  // 2. Preenche e escreve o Setor de Boot (BPB)
  *progress = 15;
  memset(bpb, 0, sector_size);
  memcpy(bpb->BS_OEMName, "LIWUSOS ", 8);
  bpb->BS_JmpBoot[0] = 0xEB;
  bpb->BS_JmpBoot[1] = 0x58;
  bpb->BS_JmpBoot[2] = 0x90;
  bpb->BPB_BytsPerSec = sector_size;
  bpb->BPB_SecPerClus = 8;
  bpb->BPB_RsvdSecCnt = 32;
  bpb->BPB_NumFATs = 2;
  bpb->BPB_Media = 0xF8;
  bpb->BPB_TotSec32 = total_sectors;
  uint32_t total_clusters =
      (total_sectors - bpb->BPB_RsvdSecCnt) / bpb->BPB_SecPerClus;
  uint32_t fat_sz32 = (total_clusters * 4 + (sector_size - 1)) / sector_size;
  bpb->BPB_FATSz32 = fat_sz32;
  bpb->BPB_RootClus = 2;
  bpb->BPB_FSInfo = 1;
  bpb->BPB_BkBootSec = 6;
  bpb->BS_DrvNum = 0x80;
  bpb->BS_BootSig = 0x29;
  bpb->BS_VolID = 0xDEADC0DE;
  memcpy(bpb->BS_VolLab, "LIWUSOSHD  ", 11);
  memcpy(bpb->BS_FilSysType, "FAT32   ", 8);
  bpb->BS_SigA = 0xAA55;

  reserved_sectors = bpb->BPB_RsvdSecCnt;
  sectors_per_cluster = bpb->BPB_SecPerClus;
  num_fats = bpb->BPB_NumFATs;
  root_cluster = bpb->BPB_RootClus;
  fsinfo_sector = bpb->BPB_FSInfo;
  backup_boot_sector = bpb->BPB_BkBootSec;

  memcpy(aligned_buffer, bpb, sector_size);
  ata_write_sector(bus, drive, 0, aligned_buffer);
  ata_write_sector(bus, drive, backup_boot_sector, aligned_buffer);
  *progress = 25;
  fat32_maybe_yield();

  // 3. Preenche e escreve o FSInfo
  fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t *)bpb; // Reutiliza o buffer
  memset(fsinfo, 0, sector_size);
  fsinfo->FSI_LeadSig = 0x41615252;
  fsinfo->FSI_StrucSig = 0x61417272;
  fsinfo->FSI_Free_Clusters = total_clusters - 1;
  fsinfo->FSI_Nxt_Free = 3;
  fsinfo->FSI_TrailSig = 0xAA550000;

  memcpy(aligned_buffer, fsinfo, sector_size);
  ata_write_sector(bus, drive, fsinfo_sector, aligned_buffer);
  ata_write_sector(bus, drive, backup_boot_sector + 1, aligned_buffer);
  *progress = 40;
  fat32_maybe_yield();

  // 4. Inicializa e limpa as FATs
  uint32_t *fat_table = (uint32_t *)aligned_buffer; // Reutiliza o buffer
  memset(fat_table, 0, sector_size);
  fat_table[0] = 0x0FFFFFF8;
  fat_table[1] = 0xFFFFFFFF;
  fat_table[2] = 0x0FFFFFFF;
  ata_write_sector(bus, drive, reserved_sectors, (uint16_t *)fat_table);
  ata_write_sector(bus, drive, reserved_sectors + fat_sz32,
                   (uint16_t *)fat_table);

  memset(aligned_buffer, 0, sector_size);
  for (uint32_t i = 1; i < fat_sz32; i++) {
    ata_write_sector(bus, drive, reserved_sectors + i, aligned_buffer);
    ata_write_sector(bus, drive, reserved_sectors + fat_sz32 + i,
                     aligned_buffer);
    if ((i % 8) == 0) {
      *progress = 40 + (i * 40) / fat_sz32;
      fat32_maybe_yield();
    }
  }
  *progress = 80;

  // 5. Limpa o cluster do diretório raiz
  uint32_t data_area_start =
      reserved_sectors + (num_fats * fat_sz32);
  uint32_t root_dir_lba =
      data_area_start + ((root_cluster - 2) * sectors_per_cluster);
  for (uint32_t i = 0; i < sectors_per_cluster; i++) {
    ata_write_sector(bus, drive, root_dir_lba + i, aligned_buffer);
    *progress = 80 + (i * 20) / sectors_per_cluster;
    fat32_maybe_yield();
  }

  *progress = 100;
  // kfree(aligned_buffer); // TODO
  // kfree(bpb); // TODO
  return 0; // Sucesso
}

// Abre um arquivo (não faz nada por enquanto)
static void fat32_open(fs_node_t *node) {
  // Poderíamos incrementar um contador de referências aqui se quiséssemos
}

// Fecha um arquivo (não faz nada por enquanto)
static void fat32_close(fs_node_t *node) {
  // Poderíamos decrementar um contador de referências aqui
}

static void fat32_make_dos_name(const char *name, char dos_name[12]) {
  int name_len;
  int ext_pos = -1;
  int nlen;

  memset(dos_name, ' ', 11);
  dos_name[11] = 0;

  if (!name) {
    return;
  }

  name_len = strlen(name);
  for (int k = 0; k < name_len; k++) {
    if (name[k] == '.') {
      ext_pos = k;
      break;
    }
  }

  nlen = (ext_pos == -1) ? name_len : ext_pos;
  if (nlen > 8) {
    nlen = 8;
  }
  for (int k = 0; k < nlen; k++) {
    char ch = name[k];
    dos_name[k] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
  }

  if (ext_pos != -1) {
    int elen = name_len - ext_pos - 1;
    if (elen > 3) {
      elen = 3;
    }
    for (int k = 0; k < elen; k++) {
      char ch = name[ext_pos + 1 + k];
      dos_name[8 + k] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
    }
  }
}

static void fat32_entry_name_to_string(const fat32_directory_entry_t *entry,
                                       char *out, uint32_t out_size) {
  int pos = 0;
  int need_dot = 0;

  if (!out || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (!entry) {
    return;
  }

  for (int i = 0; i < 8 && pos < (int)out_size - 1; i++) {
    char ch = (char)entry->DIR_Name[i];
    if (ch == ' ') {
      break;
    }
    out[pos++] = ch;
  }

  for (int i = 8; i < 11; i++) {
    if (entry->DIR_Name[i] != ' ') {
      need_dot = 1;
      break;
    }
  }

  if (need_dot && pos < (int)out_size - 1) {
    out[pos++] = '.';
    for (int i = 8; i < 11 && pos < (int)out_size - 1; i++) {
      char ch = (char)entry->DIR_Name[i];
      if (ch == ' ') {
        break;
      }
      out[pos++] = ch;
    }
  }

  out[pos] = '\0';
}

static uint32_t fat32_next_cluster(uint32_t current_cluster) {
  uint32_t fat_sector;
  uint32_t fat_offset;
  uint32_t fat_buffer[128];

  fat_sector =
      fat_begin_lba + (current_cluster * 4 / boot_sector.BPB_BytsPerSec);
  fat_offset = (current_cluster * 4) % boot_sector.BPB_BytsPerSec;
  ata_read_sector(ata_bus, ata_drive, fat_sector, (uint16_t *)fat_buffer);
  return fat_buffer[fat_offset / 4] & 0x0FFFFFFF;
}

static int fat32_load_root_dir(uint8_t **buffer_out, uint32_t *size_out,
                               uint32_t *cluster_lba_out) {
  uint32_t cluster_size;
  uint8_t *buffer;
  uint32_t cluster_lba;

  if (!fat32_mounted) {
    return -1;
  }

  cluster_size = boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
  buffer = (uint8_t *)kmalloc(cluster_size);
  if (!buffer) {
    return -1;
  }

  cluster_lba =
      cluster_begin_lba + (root_dir_first_cluster - 2) * boot_sector.BPB_SecPerClus;
  for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
    ata_read_sector(ata_bus, ata_drive, cluster_lba + i,
                    (uint16_t *)(buffer + i * 512));
  }

  *buffer_out = buffer;
  if (size_out) {
    *size_out = cluster_size;
  }
  if (cluster_lba_out) {
    *cluster_lba_out = cluster_lba;
  }
  return 0;
}

static int fat32_load_directory(uint32_t start_cluster, uint8_t **buffer_out,
                                uint32_t *size_out) {
  uint32_t cluster_size;
  uint32_t current_cluster;
  uint8_t *buffer;
  uint32_t total_size;

  if (!fat32_mounted || start_cluster < 2) {
    return -1;
  }

  cluster_size = boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
  total_size = 0;
  current_cluster = start_cluster;

  while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
    total_size += cluster_size;
    current_cluster = fat32_next_cluster(current_cluster);
  }

  buffer = (uint8_t *)kmalloc(total_size);
  if (!buffer) {
    return -1;
  }

  current_cluster = start_cluster;
  total_size = 0;
  while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
    uint32_t cluster_lba =
        cluster_begin_lba + (current_cluster - 2) * boot_sector.BPB_SecPerClus;
    for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
      ata_read_sector(ata_bus, ata_drive, cluster_lba + i,
                      (uint16_t *)(buffer + total_size +
                                   i * boot_sector.BPB_BytsPerSec));
    }
    total_size += cluster_size;
    current_cluster = fat32_next_cluster(current_cluster);
  }

  *buffer_out = buffer;
  if (size_out) {
    *size_out = total_size;
  }
  return 0;
}

static void fat32_free_cluster_chain(uint32_t start_cluster) {
  uint32_t current = start_cluster;

  while (current >= 2 && current < 0x0FFFFFF8) {
    uint32_t next = fat32_next_cluster(current);
    fat32_write_fat_entry(current, 0);
    if (next == current) {
      break;
    }
    current = next;
  }
}

// Lê o conteúdo de um arquivo
static uint32_t fat32_read(fs_node_t *node, uint32_t offset, uint32_t size,
                           uint8_t *buffer) {
  if (!node || (node->flags & FS_FILE) == 0) {
    return 0;
  }

  // Calcula o cluster inicial e o offset dentro do cluster
  uint32_t cluster_size =
      boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
  uint32_t start_cluster_index = offset / cluster_size;
  uint32_t offset_in_cluster = offset % cluster_size;

  uint32_t current_cluster = node->inode;
  for (uint32_t i = 0; i < start_cluster_index; i++) {
    // Navega na FAT para encontrar o próximo cluster
    uint32_t fat_sector =
        fat_begin_lba + (current_cluster * 4 / boot_sector.BPB_BytsPerSec);
    uint32_t fat_offset = (current_cluster * 4) % boot_sector.BPB_BytsPerSec;
    uint16_t fat_buffer[256];
    ata_read_sector(ata_bus, ata_drive, fat_sector, fat_buffer);
    current_cluster = ((uint32_t *)fat_buffer)[fat_offset / 4] & 0x0FFFFFFF;

    if (current_cluster >= 0x0FFFFFF8) {
      return 0; // Fim da cadeia antes do offset desejado
    }
  }

  uint32_t bytes_read = 0;
  uint32_t bytes_to_read = size;

  // Allocate cluster buffer on heap to avoid stack overflow with large clusters
  uint8_t *cluster_buffer = (uint8_t *)kmalloc(cluster_size);
  if (!cluster_buffer) {
    return 0; // Memory allocation failed
  }

  while (bytes_to_read > 0 && current_cluster < 0x0FFFFFF8) {
    uint32_t cluster_lba =
        cluster_begin_lba + (current_cluster - 2) * boot_sector.BPB_SecPerClus;

    for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
      ata_read_sector(
          ata_bus, ata_drive, cluster_lba + i,
          (uint16_t *)(cluster_buffer + i * boot_sector.BPB_BytsPerSec));
    }

    uint32_t read_start = offset_in_cluster;
    uint32_t to_copy = cluster_size - read_start;
    if (to_copy > bytes_to_read) {
      to_copy = bytes_to_read;
    }

    memcpy(buffer + bytes_read, cluster_buffer + read_start, to_copy);
    bytes_read += to_copy;
    bytes_to_read -= to_copy;
    offset_in_cluster = 0;

    // Pega o próximo cluster
    uint32_t fat_sector =
        fat_begin_lba + (current_cluster * 4 / boot_sector.BPB_BytsPerSec);
    uint32_t fat_offset = (current_cluster * 4) % boot_sector.BPB_BytsPerSec;
    uint16_t fat_buffer[256];
    ata_read_sector(ata_bus, ata_drive, fat_sector, fat_buffer);
    current_cluster = ((uint32_t *)fat_buffer)[fat_offset / 4] & 0x0FFFFFFF;
  }

  kfree(cluster_buffer);
  return bytes_read;
}

// ============================================
// FAT32 WRITE IMPLEMENTATION
// ============================================

static uint32_t fat32_find_free_cluster() {
  uint32_t fat_sector_start = fat_begin_lba;
  uint32_t entries_per_sector = boot_sector.BPB_BytsPerSec / 4;
  uint32_t fat_size = boot_sector.BPB_FATSz32;

  // Scan FAT
  uint32_t fat_buffer[128]; // 512 bytes

  for (uint32_t s = 0; s < fat_size; s++) {
    ata_read_sector(ata_bus, ata_drive, fat_sector_start + s,
                    (uint16_t *)fat_buffer);
    for (uint32_t i = 0; i < entries_per_sector; i++) {
      if ((fat_buffer[i] & 0x0FFFFFFF) == 0) {
        return s * entries_per_sector + i;
      }
    }
  }
  return 0; // Full
}

static void fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
  uint32_t fat_sector =
      fat_begin_lba + (cluster * 4 / boot_sector.BPB_BytsPerSec);
  uint32_t fat_offset = (cluster * 4) % boot_sector.BPB_BytsPerSec;

  uint32_t buffer[128];
  ata_read_sector(ata_bus, ata_drive, fat_sector, (uint16_t *)buffer);

  buffer[fat_offset / 4] = value;

  // Write to all FATs
  for (int i = 0; i < boot_sector.BPB_NumFATs; i++) {
    ata_write_sector(ata_bus, ata_drive,
                     fat_sector + i * boot_sector.BPB_FATSz32,
                     (uint16_t *)buffer);
  }
}

static struct dirent fat32_dirent;

static struct dirent *fat32_readdir(fs_node_t *node, uint32_t index) {
  uint32_t dir_cluster = node->inode;
  uint8_t *buffer;
  uint32_t size;
  fat32_directory_entry_t *dirent;
  int entries;
  int current = 0;

  if (!fat32_mounted || !(node->flags & FS_DIRECTORY)) {
    return NULL;
  }

  if (fat32_load_directory(dir_cluster, &buffer, &size) != 0) {
    return NULL;
  }

  dirent = (fat32_directory_entry_t *)buffer;
  entries = size / sizeof(fat32_directory_entry_t);
  for (int i = 0; i < entries; i++) {
    if (dirent[i].DIR_Name[0] == 0) break;
    if (dirent[i].DIR_Name[0] == 0xE5 || dirent[i].DIR_Attr == 0x0F || (dirent[i].DIR_Attr & 0x08)) continue;

    if (current == (int)index) {
      fat32_entry_name_to_string(&dirent[i], fat32_dirent.name, sizeof(fat32_dirent.name));
      fat32_dirent.ino = fat32_entry_start_cluster(&dirent[i]);
      kfree(buffer);
      return &fat32_dirent;
    }
    current++;
  }

  kfree(buffer);
  return NULL;
}

static fs_node_t *fat32_finddir(fs_node_t *node, const char *name) {
  uint32_t dir_cluster = node->inode;
  fat32_lookup_result_t lookup;

  if (fat32_find_entry_in_dir(dir_cluster, name, &lookup) > 0) {
    fs_node_t *res = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(res, 0, sizeof(fs_node_t));
    strcpy(res->name, name);
    res->inode = fat32_entry_start_cluster(&lookup.entry);
    res->length = lookup.entry.DIR_FileSize;
    res->flags = (lookup.entry.DIR_Attr & 0x10) ? FS_DIRECTORY : FS_FILE;
    res->read = fat32_read;
    res->write = NULL;
    res->readdir = fat32_readdir;
    res->finddir = fat32_finddir;
    res->open = fat32_open;
    res->close = fat32_close;
    return res;
  }

  return NULL;
}

static uint32_t fat32_cluster_size_bytes(void) {
  return boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
}

static void fat32_zero_cluster(uint32_t cluster) {
  uint32_t cluster_size = fat32_cluster_size_bytes();
  uint8_t *buffer = (uint8_t *)kmalloc(cluster_size);
  uint32_t cluster_lba =
      cluster_begin_lba + (cluster - 2) * boot_sector.BPB_SecPerClus;
  memset(buffer, 0, cluster_size);
  for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
    ata_write_sector(ata_bus, ata_drive, cluster_lba + i,
                     (uint16_t *)(buffer + i * boot_sector.BPB_BytsPerSec));
  }
  kfree(buffer);
}

static int fat32_is_end_of_chain(uint32_t cluster) {
  return cluster >= 0x0FFFFFF8;
}

static int fat32_is_valid_name(const char *name) {
  int len;
  int dot_count = 0;
  int part_len = 0;
  int ext_len = 0;
  int in_ext = 0;

  if (!name || !name[0]) {
    return 0;
  }

  len = strlen(name);
  if (len > 12) {
    return 0;
  }

  for (int i = 0; i < len; i++) {
    char ch = name[i];
    if (ch == '/' || ch == '\\') {
      return 0;
    }
    if (ch == '.') {
      dot_count++;
      if (dot_count > 1 || i == 0 || i == len - 1) {
        return 0;
      }
      in_ext = 1;
      continue;
    }
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
      if (in_ext) {
        ext_len++;
      } else {
        part_len++;
      }
    } else {
      return 0;
    }
  }

  if (part_len == 0 || part_len > 8 || ext_len > 3) {
    return 0;
  }
  return 1;
}

static void fat32_path_parent(const char *path, char *parent_out,
                              uint32_t parent_out_size, char *leaf_out,
                              uint32_t leaf_out_size) {
  int last_sep = -1;
  int len = path ? strlen(path) : 0;

  if (parent_out_size > 0) {
    parent_out[0] = '\0';
  }
  if (leaf_out_size > 0) {
    leaf_out[0] = '\0';
  }

  if (!path || !path[0]) {
    return;
  }

  for (int i = 0; i < len; i++) {
    if (path[i] == '/') {
      last_sep = i;
    }
  }

  if (last_sep <= 0) {
    if (parent_out_size > 0) {
      strcpy(parent_out, "/");
    }
    strncpy(leaf_out, path[0] == '/' ? path + 1 : path, leaf_out_size - 1);
    leaf_out[leaf_out_size - 1] = '\0';
    return;
  }

  if (last_sep >= (int)parent_out_size) {
    last_sep = (int)parent_out_size - 1;
  }
  memcpy(parent_out, path, last_sep);
  parent_out[last_sep] = '\0';
  strncpy(leaf_out, path + last_sep + 1, leaf_out_size - 1);
  leaf_out[leaf_out_size - 1] = '\0';
}

static void fat32_join_path(const char *base, const char *name, char *out,
                            uint32_t out_size) {
  uint32_t out_len;

  out[0] = '\0';
  if (!base || !name || out_size == 0) {
    return;
  }

  if (strcmp(base, "/") == 0) {
    strcpy(out, "/");
    out_len = strlen(out);
    if (out_len < out_size - 1) {
      strncpy(out + out_len, name, out_size - out_len - 1);
      out[out_size - 1] = '\0';
    }
    return;
  }

  strncpy(out, base, out_size - 1);
  out[out_size - 1] = '\0';
  if (out[strlen(out) - 1] != '/') {
    strcat(out, "/");
  }
  out_len = strlen(out);
  if (out_len < out_size - 1) {
    strncpy(out + out_len, name, out_size - out_len - 1);
    out[out_size - 1] = '\0';
  }
}

static uint32_t fat32_entry_start_cluster(
    const fat32_directory_entry_t *entry) {
  return ((uint32_t)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
}

static void fat32_set_entry_start_cluster(fat32_directory_entry_t *entry,
                                          uint32_t cluster) {
  entry->DIR_FstClusHI = (cluster >> 16) & 0xFFFF;
  entry->DIR_FstClusLO = cluster & 0xFFFF;
}

static int fat32_write_directory_cluster(uint32_t cluster, uint8_t *buffer) {
  uint32_t cluster_lba =
      cluster_begin_lba + (cluster - 2) * boot_sector.BPB_SecPerClus;
  for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
    ata_write_sector(ata_bus, ata_drive, cluster_lba + i,
                     (uint16_t *)(buffer + i * boot_sector.BPB_BytsPerSec));
  }
  return 0;
}

static int fat32_read_directory_cluster(uint32_t cluster, uint8_t *buffer) {
  uint32_t cluster_lba =
      cluster_begin_lba + (cluster - 2) * boot_sector.BPB_SecPerClus;
  for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
    ata_read_sector(ata_bus, ata_drive, cluster_lba + i,
                    (uint16_t *)(buffer + i * boot_sector.BPB_BytsPerSec));
  }
  return 0;
}

static int fat32_find_entry_in_dir(uint32_t dir_cluster, const char *name,
                                   fat32_lookup_result_t *result) {
  char dos_name[12];
  uint32_t cluster_size = fat32_cluster_size_bytes();
  uint8_t *buffer = (uint8_t *)kmalloc(cluster_size);
  uint32_t current_cluster = dir_cluster;

  if (!buffer || !name || !result) {
    if (buffer) {
      kfree(buffer);
    }
    return -1;
  }

  fat32_make_dos_name(name, dos_name);
  memset(result, 0, sizeof(*result));
  result->dir_cluster = dir_cluster;
  result->entry_cluster = dir_cluster;

  while (current_cluster >= 2 && !fat32_is_end_of_chain(current_cluster)) {
    fat32_directory_entry_t *dirent;
    int entries;

    fat32_read_directory_cluster(current_cluster, buffer);
    dirent = (fat32_directory_entry_t *)buffer;
    entries = cluster_size / sizeof(fat32_directory_entry_t);

    for (int i = 0; i < entries; i++) {
      if (dirent[i].DIR_Name[0] == 0) {
        kfree(buffer);
        return 0;
      }
      if (dirent[i].DIR_Name[0] == 0xE5 || dirent[i].DIR_Attr == 0x0F ||
          (dirent[i].DIR_Attr & 0x08)) {
        continue;
      }
      if (strncmp((char *)dirent[i].DIR_Name, dos_name, 11) == 0) {
        result->entry_cluster = current_cluster;
        result->entry_index = i;
        memcpy(&result->entry, &dirent[i], sizeof(result->entry));
        result->found = 1;
        kfree(buffer);
        return 1;
      }
    }

    current_cluster = fat32_next_cluster(current_cluster);
  }

  kfree(buffer);
  return 0;
}

static int fat32_resolve_directory(const char *path, uint32_t *cluster_out) {
  uint32_t current_cluster = root_dir_first_cluster;
  int start = 0;
  int len;
  int i;

  if (!cluster_out || !fat32_mounted || !path || !path[0]) {
    return -1;
  }

  if (strcmp(path, "/") == 0) {
    *cluster_out = root_dir_first_cluster;
    return 0;
  }

  len = strlen(path);
  if (path[0] == '/') {
    start = 1;
  }

  for (i = start; i <= len; i++) {
    if (path[i] == '/' || path[i] == '\0') {
      fat32_lookup_result_t lookup;
      char part[32];
      int part_len = i - start;

      if (part_len <= 0) {
        start = i + 1;
        continue;
      }
      if (part_len >= (int)sizeof(part)) {
        return -1;
      }

      memcpy(part, path + start, part_len);
      part[part_len] = '\0';

      if (!fat32_is_valid_name(part)) {
        return -1;
      }
      if (fat32_find_entry_in_dir(current_cluster, part, &lookup) <= 0) {
        return -1;
      }
      if ((lookup.entry.DIR_Attr & 0x10) == 0) {
        return -1;
      }
      current_cluster = fat32_entry_start_cluster(&lookup.entry);
      if (current_cluster < 2) {
        return -1;
      }
      start = i + 1;
    }
  }

  *cluster_out = current_cluster;
  return 0;
}

static int fat32_resolve_path(const char *path, fat32_lookup_result_t *result) {
  char parent[128];
  char leaf[32];
  uint32_t parent_cluster;
  int lookup_status;

  if (!path || !result || !path[0] || strcmp(path, "/") == 0) {
    return -1;
  }

  fat32_path_parent(path, parent, sizeof(parent), leaf, sizeof(leaf));
  if (!leaf[0] || !fat32_is_valid_name(leaf)) {
    return -1;
  }
  if (fat32_resolve_directory(parent[0] ? parent : "/", &parent_cluster) != 0) {
    return -1;
  }

  lookup_status = fat32_find_entry_in_dir(parent_cluster, leaf, result);
  if (lookup_status < 0) {
    return -1;
  }
  result->dir_cluster = parent_cluster;
  return lookup_status;
}

static int fat32_find_free_dir_slot(uint32_t dir_cluster,
                                    uint32_t *target_cluster_out,
                                    uint32_t *target_index_out) {
  uint32_t cluster_size = fat32_cluster_size_bytes();
  uint8_t *buffer = (uint8_t *)kmalloc(cluster_size);
  uint32_t current_cluster = dir_cluster;

  if (!buffer) {
    return -1;
  }

  while (current_cluster >= 2 && !fat32_is_end_of_chain(current_cluster)) {
    fat32_directory_entry_t *dirent;
    int entries;

    fat32_read_directory_cluster(current_cluster, buffer);
    dirent = (fat32_directory_entry_t *)buffer;
    entries = cluster_size / sizeof(fat32_directory_entry_t);

    for (int i = 0; i < entries; i++) {
      if (dirent[i].DIR_Name[0] == 0 || dirent[i].DIR_Name[0] == 0xE5) {
        *target_cluster_out = current_cluster;
        *target_index_out = i;
        kfree(buffer);
        return 0;
      }
    }

    if (fat32_is_end_of_chain(fat32_next_cluster(current_cluster))) {
      uint32_t new_cluster = fat32_find_free_cluster();
      if (new_cluster == 0) {
        break;
      }
      fat32_write_fat_entry(current_cluster, new_cluster);
      fat32_write_fat_entry(new_cluster, 0x0FFFFFFF);
      fat32_zero_cluster(new_cluster);
      *target_cluster_out = new_cluster;
      *target_index_out = 0;
      kfree(buffer);
      return 0;
    }

    current_cluster = fat32_next_cluster(current_cluster);
  }

  kfree(buffer);
  return -1;
}

static int fat32_write_directory_entry(uint32_t dir_cluster, uint32_t entry_index,
                                       fat32_directory_entry_t *entry) {
  uint32_t cluster_size = fat32_cluster_size_bytes();
  uint8_t *buffer = (uint8_t *)kmalloc(cluster_size);
  fat32_directory_entry_t *dirent;

  if (!buffer || dir_cluster < 2 || !entry) {
    if (buffer) {
      kfree(buffer);
    }
    return -1;
  }

  fat32_read_directory_cluster(dir_cluster, buffer);
  dirent = (fat32_directory_entry_t *)buffer;
  memcpy(&dirent[entry_index], entry, sizeof(*entry));
  fat32_write_directory_cluster(dir_cluster, buffer);
  kfree(buffer);
  return 0;
}

static int fat32_mark_directory_entry_deleted(uint32_t dir_cluster,
                                              uint32_t entry_index) {
  uint32_t cluster_size = fat32_cluster_size_bytes();
  uint8_t *buffer = (uint8_t *)kmalloc(cluster_size);
  fat32_directory_entry_t *dirent;

  if (!buffer || dir_cluster < 2) {
    if (buffer) {
      kfree(buffer);
    }
    return -1;
  }

  fat32_read_directory_cluster(dir_cluster, buffer);
  dirent = (fat32_directory_entry_t *)buffer;
  dirent[entry_index].DIR_Name[0] = 0xE5;
  fat32_write_directory_cluster(dir_cluster, buffer);
  kfree(buffer);
  return 0;
}

static int fat32_is_directory_empty(uint32_t dir_cluster) {
  uint32_t size;
  uint8_t *buffer;
  fat32_directory_entry_t *dirent;
  int entries;

  if (fat32_load_directory(dir_cluster, &buffer, &size) != 0) {
    return 0;
  }

  dirent = (fat32_directory_entry_t *)buffer;
  entries = size / sizeof(fat32_directory_entry_t);
  for (int i = 0; i < entries; i++) {
    char name[32];
    if (dirent[i].DIR_Name[0] == 0) {
      break;
    }
    if (dirent[i].DIR_Name[0] == 0xE5 || dirent[i].DIR_Attr == 0x0F ||
        (dirent[i].DIR_Attr & 0x08)) {
      continue;
    }
    fat32_entry_name_to_string(&dirent[i], name, sizeof(name));
    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
      kfree(buffer);
      return 0;
    }
  }

  kfree(buffer);
  return 1;
}

static int fat32_create_dir_entry(uint32_t parent_cluster, const char *name,
                                  uint8_t attr, uint32_t start_cluster) {
  char dos_name[12];
  uint32_t target_cluster;
  uint32_t target_index;
  fat32_directory_entry_t entry;
  fat32_lookup_result_t existing;

  if (!fat32_is_valid_name(name)) {
    return -1;
  }
  if (fat32_find_entry_in_dir(parent_cluster, name, &existing) > 0) {
    return -1;
  }
  if (fat32_find_free_dir_slot(parent_cluster, &target_cluster, &target_index) != 0) {
    return -1;
  }

  memset(&entry, 0, sizeof(entry));
  fat32_make_dos_name(name, dos_name);
  memcpy(entry.DIR_Name, dos_name, 11);
  entry.DIR_Attr = attr;
  fat32_set_entry_start_cluster(&entry, start_cluster);
  entry.DIR_FileSize = (attr & 0x10) ? 0 : 0;
  return fat32_write_directory_entry(target_cluster, target_index, &entry);
}

static uint32_t fat32_allocate_cluster_chain(uint32_t needed_clusters) {
  uint32_t first_cluster = 0;
  uint32_t previous = 0;

  if (needed_clusters == 0) {
    needed_clusters = 1;
  }

  for (uint32_t i = 0; i < needed_clusters; i++) {
    uint32_t cluster = fat32_find_free_cluster();
    if (cluster == 0) {
      if (first_cluster >= 2) {
        fat32_free_cluster_chain(first_cluster);
      }
      return 0;
    }
    fat32_write_fat_entry(cluster, 0x0FFFFFFF);
    fat32_zero_cluster(cluster);
    if (previous >= 2) {
      fat32_write_fat_entry(previous, cluster);
    } else {
      first_cluster = cluster;
    }
    previous = cluster;
  }

  return first_cluster;
}

static uint32_t fat32_write_cluster_chain(uint32_t start_cluster,
                                          const uint8_t *buffer,
                                          uint32_t size) {
  uint32_t cluster_size = fat32_cluster_size_bytes();
  uint32_t written = 0;
  uint32_t current_cluster = start_cluster;
  uint8_t *cluster_buffer = (uint8_t *)kmalloc(cluster_size);

  if (!cluster_buffer || start_cluster < 2) {
    if (cluster_buffer) {
      kfree(cluster_buffer);
    }
    return 0;
  }

  while (current_cluster >= 2 && !fat32_is_end_of_chain(current_cluster) &&
         written < size) {
    uint32_t chunk = size - written;
    uint32_t cluster_lba;
    if (chunk > cluster_size) {
      chunk = cluster_size;
    }

    memset(cluster_buffer, 0, cluster_size);
    memcpy(cluster_buffer, buffer + written, chunk);
    cluster_lba =
        cluster_begin_lba + (current_cluster - 2) * boot_sector.BPB_SecPerClus;
    for (int s = 0; s < boot_sector.BPB_SecPerClus; s++) {
      ata_write_sector(ata_bus, ata_drive, cluster_lba + s,
                       (uint16_t *)(cluster_buffer +
                                    s * boot_sector.BPB_BytsPerSec));
    }

    written += chunk;
    current_cluster = fat32_next_cluster(current_cluster);
  }

  kfree(cluster_buffer);
  return written;
}

int fat32_list_dir_entry(const char *path, int index, char *name_out,
                         int *is_dir_out, uint32_t *size_out) {
  uint32_t dir_cluster;
  uint8_t *buffer;
  uint32_t size;
  fat32_directory_entry_t *dirent;
  int entries;
  int current = 0;

  if (!fat32_mounted || !path || index < 0) {
    return 0;
  }
  if (fat32_resolve_directory(path, &dir_cluster) != 0) {
    return 0;
  }
  if (fat32_load_directory(dir_cluster, &buffer, &size) != 0) {
    return 0;
  }

  dirent = (fat32_directory_entry_t *)buffer;
  entries = size / sizeof(fat32_directory_entry_t);
  for (int i = 0; i < entries; i++) {
    if (dirent[i].DIR_Name[0] == 0) {
      break;
    }
    if (dirent[i].DIR_Name[0] == 0xE5 || dirent[i].DIR_Attr == 0x0F ||
        (dirent[i].DIR_Attr & 0x08)) {
      continue;
    }
    if (current == index) {
      if (name_out) {
        fat32_entry_name_to_string(&dirent[i], name_out, 32);
      }
      if (is_dir_out) {
        *is_dir_out = (dirent[i].DIR_Attr & 0x10) ? 1 : 0;
      }
      if (size_out) {
        *size_out = dirent[i].DIR_FileSize;
      }
      kfree(buffer);
      return 1;
    }
    current++;
  }

  kfree(buffer);
  return 0;
}

void *fat32_read_file_path(const char *path, uint32_t *size_out) {
  fat32_lookup_result_t lookup;
  uint32_t file_size;
  uint32_t start_cluster;
  uint8_t *buffer;

  if (size_out) {
    *size_out = 0;
  }
  if (!fat32_mounted || !path || fat32_resolve_path(path, &lookup) <= 0) {
    return NULL;
  }
  if (lookup.entry.DIR_Attr & 0x10) {
    return NULL;
  }

  file_size = lookup.entry.DIR_FileSize;
  start_cluster = fat32_entry_start_cluster(&lookup.entry);
  buffer = (uint8_t *)kmalloc(file_size + 1);
  if (!buffer) {
    return NULL;
  }

  memset(buffer, 0, file_size + 1);
  if (file_size > 0 && start_cluster >= 2) {
    fs_node_t fake_node;
    memset(&fake_node, 0, sizeof(fake_node));
    fake_node.flags = FS_FILE;
    fake_node.inode = start_cluster;
    fake_node.length = file_size;
    fat32_read(&fake_node, 0, file_size, buffer);
  }
  if (size_out) {
    *size_out = file_size;
  }
  return buffer;
}

int fat32_create_file_path(const char *path) {
  char parent[128];
  char leaf[32];
  uint32_t parent_cluster;
  uint32_t file_cluster;

  if (!fat32_mounted || !path || strcmp(path, "/") == 0) {
    return -1;
  }
  fat32_path_parent(path, parent, sizeof(parent), leaf, sizeof(leaf));
  if (!fat32_is_valid_name(leaf)) {
    return -1;
  }
  if (fat32_resolve_directory(parent[0] ? parent : "/", &parent_cluster) != 0) {
    return -1;
  }

  file_cluster = fat32_allocate_cluster_chain(1);
  if (file_cluster == 0) {
    return -1;
  }
  if (fat32_create_dir_entry(parent_cluster, leaf, 0x20, file_cluster) != 0) {
    fat32_free_cluster_chain(file_cluster);
    return -1;
  }
  return 0;
}

uint32_t fat32_write_file_path(const char *path, uint8_t *buffer, uint32_t size) {
  fat32_lookup_result_t lookup;
  uint32_t needed_clusters;
  uint32_t new_start_cluster;
  fat32_directory_entry_t entry;
  int lookup_status;

  if (!fat32_mounted || !path) {
    return 0;
  }

  lookup_status = fat32_resolve_path(path, &lookup);
  if (lookup_status <= 0) {
    if (fat32_create_file_path(path) != 0) {
      return 0;
    }
    lookup_status = fat32_resolve_path(path, &lookup);
    if (lookup_status <= 0) {
      return 0;
    }
  }
  if (lookup.entry.DIR_Attr & 0x10) {
    return 0;
  }

  needed_clusters = (size + fat32_cluster_size_bytes() - 1) /
                    fat32_cluster_size_bytes();
  if (needed_clusters == 0) {
    needed_clusters = 1;
  }
  new_start_cluster = fat32_allocate_cluster_chain(needed_clusters);
  if (new_start_cluster == 0) {
    return 0;
  }

  if (size > 0 && fat32_write_cluster_chain(new_start_cluster, buffer, size) != size) {
    fat32_free_cluster_chain(new_start_cluster);
    return 0;
  }

  entry = lookup.entry;
  if (fat32_entry_start_cluster(&lookup.entry) >= 2) {
    fat32_free_cluster_chain(fat32_entry_start_cluster(&lookup.entry));
  }
  fat32_set_entry_start_cluster(&entry, new_start_cluster);
  entry.DIR_FileSize = size;
  if (fat32_write_directory_entry(lookup.entry_cluster, lookup.entry_index, &entry) != 0) {
    fat32_free_cluster_chain(new_start_cluster);
    return 0;
  }

  return size;
}

int fat32_delete_path(const char *path) {
  fat32_lookup_result_t lookup;
  uint32_t start_cluster;

  if (!fat32_mounted || !path || strcmp(path, "/") == 0) {
    return -1;
  }
  if (fat32_resolve_path(path, &lookup) <= 0) {
    return -1;
  }

  start_cluster = fat32_entry_start_cluster(&lookup.entry);
  if (lookup.entry.DIR_Attr & 0x10) {
    if (!fat32_is_directory_empty(start_cluster)) {
      return -1;
    }
  }
  if (start_cluster >= 2) {
    fat32_free_cluster_chain(start_cluster);
  }
  return fat32_mark_directory_entry_deleted(lookup.entry_cluster,
                                            lookup.entry_index);
}

int fat32_rename_path(const char *old_path, const char *new_path) {
  fat32_lookup_result_t lookup;
  char old_parent[128];
  char new_parent[128];
  char new_leaf[32];
  char new_dos[12];
  fat32_directory_entry_t updated;

  if (!fat32_mounted || !old_path || !new_path || strcmp(old_path, "/") == 0 ||
      strcmp(new_path, "/") == 0) {
    return -1;
  }

  fat32_path_parent(old_path, old_parent, sizeof(old_parent), new_leaf, sizeof(new_leaf));
  fat32_path_parent(new_path, new_parent, sizeof(new_parent), new_leaf, sizeof(new_leaf));
  if (strcmp(old_parent[0] ? old_parent : "/", new_parent[0] ? new_parent : "/") != 0) {
    return -1;
  }
  if (!fat32_is_valid_name(new_leaf)) {
    return -1;
  }
  if (fat32_resolve_path(old_path, &lookup) <= 0) {
    return -1;
  }
  if (fat32_find_entry_in_dir(lookup.dir_cluster, new_leaf, &lookup) > 0) {
    return -1;
  }
  if (fat32_resolve_path(old_path, &lookup) <= 0) {
    return -1;
  }

  updated = lookup.entry;
  fat32_make_dos_name(new_leaf, new_dos);
  memcpy(updated.DIR_Name, new_dos, 11);
  return fat32_write_directory_entry(lookup.entry_cluster, lookup.entry_index,
                                     &updated);
}

int fat32_create_dir(const char *path) {
  char parent[128];
  char leaf[32];
  uint32_t parent_cluster;
  uint32_t new_cluster;
  uint32_t cluster_size;
  uint8_t *buffer;
  fat32_directory_entry_t *dirent;

  if (!fat32_mounted || !path || strcmp(path, "/") == 0) {
    return -1;
  }
  fat32_path_parent(path, parent, sizeof(parent), leaf, sizeof(leaf));
  if (!fat32_is_valid_name(leaf)) {
    return -1;
  }
  if (fat32_resolve_directory(parent[0] ? parent : "/", &parent_cluster) != 0) {
    return -1;
  }
  if (fat32_find_entry_in_dir(parent_cluster, leaf, &(fat32_lookup_result_t){0}) > 0) {
    return -1;
  }

  new_cluster = fat32_allocate_cluster_chain(1);
  if (new_cluster == 0) {
    return -1;
  }

  cluster_size = fat32_cluster_size_bytes();
  buffer = (uint8_t *)kmalloc(cluster_size);
  if (!buffer) {
    fat32_free_cluster_chain(new_cluster);
    return -1;
  }
  memset(buffer, 0, cluster_size);
  dirent = (fat32_directory_entry_t *)buffer;

  memset(&dirent[0], 0, sizeof(fat32_directory_entry_t));
  memcpy(dirent[0].DIR_Name, ".          ", 11);
  dirent[0].DIR_Attr = 0x10;
  fat32_set_entry_start_cluster(&dirent[0], new_cluster);

  memset(&dirent[1], 0, sizeof(fat32_directory_entry_t));
  memcpy(dirent[1].DIR_Name, "..         ", 11);
  dirent[1].DIR_Attr = 0x10;
  fat32_set_entry_start_cluster(&dirent[1], parent_cluster);

  fat32_write_directory_cluster(new_cluster, buffer);
  kfree(buffer);

  if (fat32_create_dir_entry(parent_cluster, leaf, 0x10, new_cluster) != 0) {
    fat32_free_cluster_chain(new_cluster);
    return -1;
  }

  return 0;
}

int fat32_path_info(const char *path, int *is_dir_out, uint32_t *size_out) {
  fat32_lookup_result_t lookup;

  if (is_dir_out) {
    *is_dir_out = 0;
  }
  if (size_out) {
    *size_out = 0;
  }

  if (!fat32_mounted || !path) {
    return 0;
  }

  if (strcmp(path, "/") == 0) {
    if (is_dir_out) {
      *is_dir_out = 1;
    }
    return 1;
  }

  if (fat32_resolve_path(path, &lookup) <= 0) {
    return 0;
  }

  if (is_dir_out) {
    *is_dir_out = (lookup.entry.DIR_Attr & 0x10) ? 1 : 0;
  }
  if (size_out) {
    *size_out = lookup.entry.DIR_FileSize;
  }
  return 1;
}

int fat32_create_file(const char *name) { return fat32_create_file_path(name); }

uint32_t fat32_write_file(const char *name, uint8_t *buffer, uint32_t size) {
  return fat32_write_file_path(name, buffer, size);
}

int fat32_root_list_entry(int index, char *name_out, int *is_dir_out,
                          uint32_t *size_out) {
  return fat32_list_dir_entry("/", index, name_out, is_dir_out, size_out);
}

void *fat32_read_file(const char *name, uint32_t *size_out) {
  char path[128];
  fat32_join_path("/", name, path, sizeof(path));
  return fat32_read_file_path(path, size_out);
}

int fat32_delete_file(const char *name) {
  char path[128];
  fat32_join_path("/", name, path, sizeof(path));
  return fat32_delete_path(path);
}

int fat32_rename_file(const char *old_name, const char *new_name) {
  char old_path[128];
  char new_path[128];
  fat32_join_path("/", old_name, old_path, sizeof(old_path));
  fat32_join_path("/", new_name, new_path, sizeof(new_path));
  return fat32_rename_path(old_path, new_path);
}
