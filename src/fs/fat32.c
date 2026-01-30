#include "fat32.h"
#include "ata.h"
#include "kheap.h"
#include "string.h"
#include "task.h"
#include "vfs.h"
#include "video.h" // Para debug

// Variáveis globais para o filesystem montado
static fat32_boot_sector_t boot_sector;
static uint32_t fat_begin_lba;
static uint32_t cluster_begin_lba;
static uint32_t root_dir_first_cluster;
static uint16_t ata_bus;
static uint8_t ata_drive;

// Protótipos de funções internas
static uint32_t fat32_read_cluster_chain(uint32_t cluster, uint8_t *buffer,
                                         uint32_t buffer_size);
static fs_node_t *fat32_find_directory_entry(fs_node_t *parent,
                                             const char *name);
static uint32_t fat32_read(fs_node_t *node, uint32_t offset, uint32_t size,
                           uint8_t *buffer);
static fs_node_t *fat32_readdir(fs_node_t *node, uint32_t index);
static void fat32_open(fs_node_t *node);
static void fat32_close(fs_node_t *node);

// Monta o sistema de arquivos FAT32
fs_node_t *fat32_mount(uint16_t bus, uint8_t drive,
                       uint32_t partition_lba_start) {
  ata_bus = bus;
  ata_drive = drive;

  // Buffer para ler o setor de boot
  uint16_t boot_sector_buffer[256];
  if (ata_read_sector(ata_bus, ata_drive, partition_lba_start,
                      boot_sector_buffer) != 0) {
    draw_string(10, 100, "FAT32: Falha ao ler o setor de boot.", 0xFF0000);
    return NULL;
  }

  memcpy(&boot_sector, boot_sector_buffer, sizeof(fat32_boot_sector_t));

  // Verifica a assinatura "FAT32"
  if (strncmp((char *)boot_sector.BS_FilSysType, "FAT32   ", 8) != 0) {
    draw_string(10, 110, "FAT32: Filesystem nao e FAT32.", 0xFF0000);
    return NULL;
  }

  // Calcula os offsets importantes
  fat_begin_lba = partition_lba_start + boot_sector.BPB_RsvdSecCnt;
  cluster_begin_lba =
      fat_begin_lba + (boot_sector.BPB_NumFATs * boot_sector.BPB_FATSz32);
  root_dir_first_cluster = boot_sector.BPB_RootClus;

  // Cria o nó raiz do VFS
  fs_node_t *root_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
  memset(root_node, 0, sizeof(fs_node_t));
  strcpy(root_node->name, "/");
  root_node->flags = FS_DIRECTORY;
  root_node->inode = root_dir_first_cluster;
  root_node->open = fat32_open;
  root_node->close = fat32_close;
  // root_node->read = NULL; // Diretórios não têm 'read' direto
  // root_node->readdir = fat32_readdir;

  return root_node;
}

int fat32_format(int *progress) {
  *progress = 0;
  const uint16_t bus = ATA_PRIMARY;
  const uint8_t drive = ATA_MASTER;

  const uint32_t total_sectors = 204800;
  const uint16_t sector_size = 512;

  uint16_t *aligned_buffer = (uint16_t *)kmalloc(sector_size);
  fat32_boot_sector_t *bpb = (fat32_boot_sector_t *)kmalloc(sector_size);

  // 1. Limpa o início do disco
  memset(aligned_buffer, 0, sector_size);
  for (uint32_t i = 0; i < 33; i++) {
    ata_write_sector(bus, drive, i, aligned_buffer);
    *progress = (i * 10) / 33;
    switch_task();
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

  memcpy(aligned_buffer, bpb, sector_size);
  ata_write_sector(bus, drive, 0, aligned_buffer);
  ata_write_sector(bus, drive, bpb->BPB_BkBootSec, aligned_buffer);
  *progress = 25;
  switch_task();

  // 3. Preenche e escreve o FSInfo
  fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t *)bpb; // Reutiliza o buffer
  memset(fsinfo, 0, sector_size);
  fsinfo->FSI_LeadSig = 0x41615252;
  fsinfo->FSI_StrucSig = 0x61417272;
  fsinfo->FSI_Free_Clusters = total_clusters - 1;
  fsinfo->FSI_Nxt_Free = 3;
  fsinfo->FSI_TrailSig = 0xAA550000;

  memcpy(aligned_buffer, fsinfo, sector_size);
  ata_write_sector(bus, drive, bpb->BPB_FSInfo, aligned_buffer);
  ata_write_sector(bus, drive, bpb->BPB_BkBootSec + 1, aligned_buffer);
  *progress = 40;
  switch_task();

  // 4. Inicializa e limpa as FATs
  uint32_t *fat_table = (uint32_t *)aligned_buffer; // Reutiliza o buffer
  memset(fat_table, 0, sector_size);
  fat_table[0] = 0x0FFFFFF8;
  fat_table[1] = 0xFFFFFFFF;
  fat_table[2] = 0x0FFFFFFF;
  ata_write_sector(bus, drive, bpb->BPB_RsvdSecCnt, (uint16_t *)fat_table);
  ata_write_sector(bus, drive, bpb->BPB_RsvdSecCnt + fat_sz32,
                   (uint16_t *)fat_table);

  memset(aligned_buffer, 0, sector_size);
  for (uint32_t i = 1; i < fat_sz32; i++) {
    ata_write_sector(bus, drive, bpb->BPB_RsvdSecCnt + i, aligned_buffer);
    ata_write_sector(bus, drive, bpb->BPB_RsvdSecCnt + fat_sz32 + i,
                     aligned_buffer);
    if ((i % 8) == 0) {
      *progress = 40 + (i * 40) / fat_sz32;
      switch_task();
    }
  }
  *progress = 80;

  // 5. Limpa o cluster do diretório raiz
  uint32_t data_area_start =
      bpb->BPB_RsvdSecCnt + (bpb->BPB_NumFATs * fat_sz32);
  uint32_t root_dir_lba =
      data_area_start + ((bpb->BPB_RootClus - 2) * bpb->BPB_SecPerClus);
  for (uint32_t i = 0; i < bpb->BPB_SecPerClus; i++) {
    ata_write_sector(bus, drive, root_dir_lba + i, aligned_buffer);
    *progress = 80 + (i * 20) / bpb->BPB_SecPerClus;
    switch_task();
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

  while (bytes_to_read > 0 && current_cluster < 0x0FFFFFF8) {
    uint8_t cluster_buffer[cluster_size];
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

// Find dirent sector and offset logic is complex, simplifying:
// Scan Root Dir for name, return info to update.
static int fat32_update_dirent_size(const char *name, uint32_t new_size,
                                    uint32_t start_cluster) {
  uint32_t current_cluster = root_dir_first_cluster;

  // Assume root dir fits in one cluster chain, scan it.
  // Simplifying: Scan just first cluster of root dir for now or traverse chain
  // basic. Only support root dir files.

  uint32_t cluster_size =
      boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
  uint8_t *buffer = (uint8_t *)kmalloc(cluster_size);

  // Read Root Cluster
  uint32_t cluster_lba =
      cluster_begin_lba + (current_cluster - 2) * boot_sector.BPB_SecPerClus;
  for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
    ata_read_sector(ata_bus, ata_drive, cluster_lba + i,
                    (uint16_t *)(buffer + i * 512));
  }

  fat32_directory_entry_t *dirent = (fat32_directory_entry_t *)buffer;
  int entries = cluster_size / sizeof(fat32_directory_entry_t);

  char dos_name[12];
  // Convert input name to DOS 8.3 "NAME    EXT"
  memset(dos_name, ' ', 11);
  dos_name[11] = 0;
  // Simple conversion logic... skipped for brevity, assuming input is valid-ish
  // or we construct simplistic Just find matching start_cluster if provided?
  // No, search by name. Hack: if name is passed, we search... let's trust
  // start_cluster check if unique? No. Let's implement minimal name match.
  int name_len = strlen(name);
  // Copy main name
  int ext_pos = -1;
  for (int k = 0; k < name_len; k++)
    if (name[k] == '.')
      ext_pos = k;

  int nlen = (ext_pos == -1) ? name_len : ext_pos;
  if (nlen > 8)
    nlen = 8;
  for (int k = 0; k < nlen; k++)
    dos_name[k] = (name[k] >= 'a' && name[k] <= 'z') ? name[k] - 32 : name[k];

  if (ext_pos != -1) {
    int elen = name_len - ext_pos - 1;
    if (elen > 3)
      elen = 3;
    for (int k = 0; k < elen; k++)
      dos_name[8 + k] =
          (name[ext_pos + 1 + k] >= 'a' && name[ext_pos + 1 + k] <= 'z')
              ? name[ext_pos + 1 + k] - 32
              : name[ext_pos + 1 + k];
  }

  for (int i = 0; i < entries; i++) {
    if (dirent[i].DIR_Name[0] == 0)
      break;
    if (dirent[i].DIR_Name[0] == 0xE5)
      continue;

    if (strncmp((char *)dirent[i].DIR_Name, dos_name, 11) == 0) {
      // Found
      dirent[i].DIR_FileSize = new_size;
      if (start_cluster != 0) {
        dirent[i].DIR_FstClusHI = (start_cluster >> 16);
        dirent[i].DIR_FstClusLO = (start_cluster & 0xFFFF);
      }

      // Write back
      for (int s = 0; s < boot_sector.BPB_SecPerClus; s++) {
        ata_write_sector(ata_bus, ata_drive, cluster_lba + s,
                         (uint16_t *)(buffer + s * 512));
      }
      kfree(buffer);
      return 0; // Success
    }
  }

  kfree(buffer);
  return -1; // Not found
}

int fat32_create_file(const char *name) {
  // Find free entry in Root DIR
  uint32_t current_cluster = root_dir_first_cluster;
  uint32_t cluster_size =
      boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
  uint8_t *buffer = (uint8_t *)kmalloc(cluster_size);

  uint32_t cluster_lba =
      cluster_begin_lba + (current_cluster - 2) * boot_sector.BPB_SecPerClus;
  for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
    ata_read_sector(ata_bus, ata_drive, cluster_lba + i,
                    (uint16_t *)(buffer + i * 512));
  }

  fat32_directory_entry_t *dirent = (fat32_directory_entry_t *)buffer;
  int entries = cluster_size / sizeof(fat32_directory_entry_t);

  for (int i = 0; i < entries; i++) {
    if (dirent[i].DIR_Name[0] == 0 || dirent[i].DIR_Name[0] == 0xE5) {
      // Found free slot
      memset(&dirent[i], 0, sizeof(fat32_directory_entry_t));

      // Name Conversion
      memset(dirent[i].DIR_Name, ' ', 11);
      int name_len = strlen(name);
      int ext_pos = -1;
      for (int k = 0; k < name_len; k++)
        if (name[k] == '.')
          ext_pos = k;

      int nlen = (ext_pos == -1) ? name_len : ext_pos;
      if (nlen > 8)
        nlen = 8;
      for (int k = 0; k < nlen; k++)
        dirent[i].DIR_Name[k] =
            (name[k] >= 'a' && name[k] <= 'z') ? name[k] - 32 : name[k];

      if (ext_pos != -1) {
        int elen = name_len - ext_pos - 1;
        if (elen > 3)
          elen = 3;
        for (int k = 0; k < elen; k++)
          dirent[i].DIR_Name[8 + k] =
              (name[ext_pos + 1 + k] >= 'a' && name[ext_pos + 1 + k] <= 'z')
                  ? name[ext_pos + 1 + k] - 32
                  : name[ext_pos + 1 + k];
      }

      dirent[i].DIR_Attr = 0x20; // Archive

      // Allocate 1st cluster
      uint32_t free_clus = fat32_find_free_cluster();
      if (free_clus == 0) {
        kfree(buffer);
        return -2;
      } // Full

      fat32_write_fat_entry(free_clus, 0x0FFFFFFF); // EOC

      dirent[i].DIR_FstClusHI = (free_clus >> 16);
      dirent[i].DIR_FstClusLO = (free_clus & 0xFFFF);
      dirent[i].DIR_FileSize = 0;

      // Write directory back
      for (int s = 0; s < boot_sector.BPB_SecPerClus; s++) {
        ata_write_sector(ata_bus, ata_drive, cluster_lba + s,
                         (uint16_t *)(buffer + s * 512));
      }

      kfree(buffer);
      return 0;
    }
  }

  kfree(buffer);
  return -1; // Directory full (no support for extending dir yet)
}

uint32_t fat32_write_file(const char *name, uint8_t *buffer, uint32_t size) {
  // 1. Find file to get start cluster
  uint32_t start_cluster = 0;

  // Scan root dir
  // Reuse logic...
  uint32_t current_cluster = root_dir_first_cluster;
  uint32_t cluster_size =
      boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
  uint8_t *temp_buf = (uint8_t *)kmalloc(cluster_size);
  uint32_t cluster_lba =
      cluster_begin_lba + (current_cluster - 2) * boot_sector.BPB_SecPerClus;

  for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
    ata_read_sector(ata_bus, ata_drive, cluster_lba + i,
                    (uint16_t *)(temp_buf + i * 512));
  }

  // Name match logic again... should refactor
  char dos_name[12];
  memset(dos_name, ' ', 11);
  dos_name[11] = 0;
  int name_len = strlen(name);
  int ext_pos = -1;
  for (int k = 0; k < name_len; k++)
    if (name[k] == '.')
      ext_pos = k;
  int nlen = (ext_pos == -1) ? name_len : ext_pos;
  if (nlen > 8)
    nlen = 8;
  for (int k = 0; k < nlen; k++)
    dos_name[k] = (name[k] >= 'a' && name[k] <= 'z') ? name[k] - 32 : name[k];
  if (ext_pos != -1) {
    int elen = name_len - ext_pos - 1;
    if (elen > 3)
      elen = 3;
    for (int k = 0; k < elen; k++)
      dos_name[8 + k] =
          (name[ext_pos + 1 + k] >= 'a' && name[ext_pos + 1 + k] <= 'z')
              ? name[ext_pos + 1 + k] - 32
              : name[ext_pos + 1 + k];
  }

  fat32_directory_entry_t *dirent = (fat32_directory_entry_t *)temp_buf;
  int entries = cluster_size / sizeof(fat32_directory_entry_t);
  for (int i = 0; i < entries; i++) {
    if (dirent[i].DIR_Name[0] != 0 && dirent[i].DIR_Name[0] != 0xE5) {
      if (strncmp((char *)dirent[i].DIR_Name, dos_name, 11) == 0) {
        start_cluster =
            (dirent[i].DIR_FstClusHI << 16) | dirent[i].DIR_FstClusLO;
        break;
      }
    }
  }
  kfree(temp_buf);

  if (start_cluster == 0)
    return 0; // Not found

  // 2. Write data to chain
  uint32_t written = 0;
  uint32_t remaining = size;
  uint32_t curr = start_cluster;

  while (remaining > 0) {
    uint32_t clba = cluster_begin_lba + (curr - 2) * boot_sector.BPB_SecPerClus;
    uint32_t to_write = (remaining > cluster_size) ? cluster_size : remaining;

    // Prepare buffer for cluster (needs to be sector aligned/sized)
    uint8_t *cbuf = (uint8_t *)kmalloc(cluster_size);
    memset(cbuf, 0, cluster_size); // Pad with 0 for last partial
    memcpy(cbuf, buffer + written, to_write);

    for (int s = 0; s < boot_sector.BPB_SecPerClus; s++) {
      ata_write_sector(ata_bus, ata_drive, clba + s,
                       (uint16_t *)(cbuf + s * 512));
    }
    kfree(cbuf);

    written += to_write;
    remaining -= to_write;

    if (remaining > 0) {
      // Need next cluster
      uint32_t next_clus = 0; // Check FAT
      uint32_t fat_sec =
          fat_begin_lba + (curr * 4 / boot_sector.BPB_BytsPerSec);
      uint32_t fat_off = (curr * 4) % boot_sector.BPB_BytsPerSec;
      uint32_t fbuf[128];
      ata_read_sector(ata_bus, ata_drive, fat_sec, (uint16_t *)fbuf);
      next_clus = fbuf[fat_off / 4] & 0x0FFFFFFF;

      if (next_clus >= 0x0FFFFFF8) {
        // Allocate new
        next_clus = fat32_find_free_cluster();
        if (next_clus == 0)
          break; // Disk full

        fat32_write_fat_entry(curr, next_clus);
        fat32_write_fat_entry(next_clus, 0x0FFFFFFF);
      }
      curr = next_clus;
    }
  }

  // 3. Update size
  fat32_update_dirent_size(name, size, 0);

  return written;
}