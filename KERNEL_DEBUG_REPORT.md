# LiwusOS Kernel Debugging Report

## Executive Summary
This report contains kernel debugging information for LiwusOS including page fault handling, virtual memory mapping, and filesystem operations.

---

## 1. PAGE FAULT HANDLER (CPU Exception 0x0E)

### Location
- **File**: [src/kernel/isr.c](src/kernel/isr.c)
- **Assembly**: [src/boot/interrupt.s](src/boot/interrupt.s#L41)

### Assembly Code (ISR14 - Page Fault Interrupt)
```asm
001100ab <isr14>:
  1100ab:       fa                      cli
  1100ac:       6a 0e                   push   $0xe
  1100ae:       eb 75                   jmp    110125 <isr_common_stub>
```

The page fault interrupt is defined with an error code (using `ISR_ERRCODE` macro), which pushes the error code automatically by the CPU.

### C Handler Implementation
**File**: [src/kernel/isr.c](src/kernel/isr.c#L15)

```c
void isr_handler(registers_t *regs) {
  if (regs->int_no == 128) {
    syscall_handler(regs);
  } else {
    serial_print("CPU exception: ");
    serial_print_hex(regs->int_no);
    serial_print(" err=");
    serial_print_hex(regs->err_code);
    if (regs->int_no == 14) {  // ← PAGE FAULT DETECTION
      uint32_t cr2;
      asm volatile("mov %%cr2, %0" : "=r"(cr2));  // ← Read faulting address
      serial_print(" cr2=");
      serial_print_hex(cr2);
    }
    serial_print(" eip=");
    serial_print_hex(regs->eip);
    serial_print("\n");
  }
}
```

### Key Features:
1. **CR2 Register Access**: Reads the faulting virtual address via CR2
2. **Error Code Interpretation**: Error code at `regs->err_code` indicates:
   - Bit 0: Present (0 = not present, 1 = protection violation)
   - Bit 1: Write (0 = read, 1 = write)
   - Bit 2: User Mode (0 = kernel, 1 = user)
3. **Debugging Output**: Prints exception #, error code, faulting address (CR2), and EIP

### Exception Flow
```
Page Fault occurs
    ↓
isr14 (assembly) pushes error code and exception number
    ↓
isr_common_stub (assembly) saves all registers as registers_t struct
    ↓
isr_handler() (C function) processes the fault
    ↓
Extracts CR2 (faulting address) for diagnosis
```

---

## 2. INSTRUCTION AT 0x00102E2B

### Disassembly
```asm
00102e20 <switch_page_directory>:
  102e20:       55                      push   %ebp
  102e21:       89 e5                   mov    %esp,%ebp
  102e23:       8b 45 08                mov    0x8(%ebp),%eax
  102e26:       a3 ec 7e 11 00          mov    %eax,0x117eec
  102e2b:       8b 80 00 20 00 00       mov    0x2000(%eax),%eax
  102e31:       89 45 08                mov    %eax,0x8(%ebp)
  102e34:       5d                      pop    %ebp
  102e35:       e9 d9 d1 00 00          jmp    110013 <load_page_directory>
```

### Instruction Analysis
At **0x00102E2B**: `mov 0x2000(%eax),%eax`

**Context**: This instruction is in the `switch_page_directory()` function
- It accesses offset `0x2000` (8192 bytes) from an address in EAX
- This corresponds to accessing the `physicalAddr` field of a `page_directory_t` struct
- Struct layout: `tablesPhysical[1024]` (4096 bytes) + `tablesVirtual[1024]` (4096 pointers) + `physicalAddr` (4 bytes)
- Offset 0x2000 = 8192 bytes into the structure

**Purpose**: Load the physical address of the page directory for switching contexts

---

## 3. VIRTUAL MEMORY MAPPING CODE

### VMM Header File
**File**: [include/vmm.h](include/vmm.h)

```c
typedef struct {
  uint32_t tablesPhysical[1024];     // Offsets 0x0000-0x0FFC (4096 bytes)
  uint32_t *tablesVirtual[1024];     // Offsets 0x1000-0x1FFC (4096 bytes)
  uint32_t physicalAddr;              // Offset 0x2000 (physical address of tablesPhysical)
} page_directory_t;

#define PAGE_SIZE 4096
```

### VMM Initialization
**File**: [src/kernel/vmm.c](src/kernel/vmm.c#L47)

```c
void init_vmm(uint32_t memory_size) {
  kernel_directory = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  memset(kernel_directory, 0, sizeof(page_directory_t));

  uint32_t phys_addr;
  kernel_directory->physicalAddr = (uint32_t)kmalloc_ap(4096, &phys_addr);
  memset((void *)kernel_directory->physicalAddr, 0, 4096);

  uint32_t *pd = (uint32_t *)kernel_directory->physicalAddr;

  // Identity-map all kernel physical memory
  for (uint32_t i = 0; i < memory_size; i += 4096) {
    uint32_t pd_index = i >> 22;        // Extract PD index (bits 22-31)
    uint32_t pt_index = (i >> 12) & 0x03FF;  // Extract PT index (bits 12-21)

    if (!kernel_directory->tablesVirtual[pd_index]) {
      uint32_t *table = (uint32_t *)kmalloc_a(4096);
      kernel_directory->tablesVirtual[pd_index] = table;
      pd[pd_index] = ((uint32_t)table) | 0x3;
      memset(table, 0, 4096);
    }
    kernel_directory->tablesVirtual[pd_index][pt_index] = i | 0x3;
  }

  // Map high memory (VRAM at 0xFD000000)
  uint32_t fb_phys = 0xFD000000;
  for (int i = 0; i < 16 * 1024 * 1024; i += 4096) {
    uint32_t addr = fb_phys + i;
    uint32_t pd_index = addr >> 22;
    uint32_t pt_index = (addr >> 12) & 0x03FF;

    if (!kernel_directory->tablesVirtual[pd_index]) {
      uint32_t *table = (uint32_t *)kmalloc_a(4096);
      kernel_directory->tablesVirtual[pd_index] = table;
      pd[pd_index] = ((uint32_t)table) | 0x3;
      memset(table, 0, 4096);
    }
    kernel_directory->tablesVirtual[pd_index][pt_index] = addr | 0x3;
  }

  current_directory = kernel_directory;
  load_page_directory((uint32_t *)kernel_directory->physicalAddr);
  enable_paging();
}
```

### Page Mapping Function
**File**: [src/kernel/vmm.c](src/kernel/vmm.c#L18)

```c
void vmm_map_page(void *phys, void *virt, uint32_t flags) {
  page_directory_t *dir = current_directory;
  uint32_t pd_index = (uint32_t)virt >> 22;
  uint32_t pt_index = ((uint32_t)virt >> 12) & 0x03FF;
  uint32_t *pd = (uint32_t *)dir->physicalAddr;

  if (!dir->tablesVirtual[pd_index]) {
    uint32_t *new_table = (uint32_t *)kmalloc_a(4096);
    uint32_t phys_table = (uint32_t)new_table;
    memset(new_table, 0, 4096);

    dir->tablesVirtual[pd_index] = new_table;
    dir->tablesPhysical[pd_index] = phys_table | 0x7;  // PRESENT, RW, USER
    pd[pd_index] = phys_table | 0x7;
  } else {
    uint32_t pd_flags = 0x1;
    if (flags & 0x2) pd_flags |= 0x2;  // Write flag
    if (flags & 0x4) pd_flags |= 0x4;  // User flag
    pd[pd_index] |= pd_flags;
  }

  uint32_t *table = dir->tablesVirtual[pd_index];
  table[pt_index] = ((uint32_t)phys) | (flags & 0xFFF) | 0x1;  // Present
  
  // Flush TLB entry
  asm volatile("invlpg (%0)" ::"r" (virt) : "memory");
}
```

### Page Copy Function (for fork)
**File**: [src/kernel/vmm.c](src/kernel/vmm.c#L109)

```c
page_directory_t *vmm_copy_directory(page_directory_t *src) {
  page_directory_t *dir = vmm_create_directory();
  for (int i = 0; i < 1024; i++) {
    if (src->tablesVirtual[i] && src->tablesVirtual[i] != kernel_directory->tablesVirtual[i]) {
      uint32_t *old_table = src->tablesVirtual[i];
      uint32_t *new_table = (uint32_t *)kmalloc_a(4096);
      dir->tablesVirtual[i] = new_table;
      dir->tablesPhysical[i] = (uint32_t)new_table | 0x7;
      memset(new_table, 0, 4096);
      for (int j = 0; j < 1024; j++) {
        if (old_table[j] & 0x1) {
          uint32_t flags = old_table[j] & 0xFFF;
          uint32_t phys = old_table[j] & 0xFFFFF000;
          void *new_phys = kmalloc_a(4096);
          memcpy(new_phys, (void *)(phys), 4096);
          new_table[j] = (uint32_t)new_phys | flags;
        }
      }
    }
  }
  return dir;
}
```

### Heap Allocation (brk syscall)
**File**: [src/kernel/vmm.c](src/kernel/vmm.c#L135)

```c
uint32_t sys_brk(uint32_t addr) {
  if (!current_task) return 0;
  if (addr == 0 || addr < current_task->heap_start) return current_task->heap_end;
  if (addr > current_task->heap_end) {
    uint32_t start = (current_task->heap_end + 0xFFF) & 0xFFFFF000;
    uint32_t end = (addr + 0xFFF) & 0xFFFFF000;
    for (uint32_t p = start; p < end; p += 4096) {
      void *phys = kmalloc_a(4096);
      vmm_map_page(phys, (void *)p, 0x7);  // Flags: PRESENT, RW, USER
      memset((void *)p, 0, 4096);
    }
    current_task->heap_end = addr;
  }
  return current_task->heap_end;
}
```

### Dynamic Page Mapping in Syscalls
**File**: [src/kernel/syscall.c](src/kernel/syscall.c#L139)

```c
// User stack mapping (execve syscall)
for (int i = 0; i < 4; i++) {
  void *phys = kmalloc_a(4096);
  vmm_map_page(phys, (void *)(0xBFFFF000 - i * 4096), 0x7);
  memset((void *)(0xBFFFF000 - i * 4096), 0, 4096);
}
```

### Paging Flags
- `0x1`: Present bit
- `0x2`: Read/Write bit
- `0x4`: User/Supervisor bit
- `0x7`: Common user page (PRESENT | RW | USER)
- `0x3`: Common kernel page (PRESENT | RW)

---

## 4. FILE SYSTEM OPERATIONS

### VFS Interface
**File**: [include/vfs.h](include/vfs.h)

```c
#define FS_FILE        0x01
#define FS_DIRECTORY   0x02

typedef struct fs_node {
    char name[128];
    uint32_t mask;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t inode;
    uint32_t length;
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    struct fs_node* ptr;  // For directories
} fs_node_t;

uint32_t read_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
uint32_t write_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
```

### VFS Implementation
**File**: [src/fs/vfs.c](src/fs/vfs.c)

```c
uint32_t read_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node && node->read) {
        return node->read(node, offset, size, buffer);
    }
    return 0;
}

uint32_t write_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node && node->write) {
        return node->write(node, offset, size, buffer);
    }
    return 0;
}
```

### FAT32 Implementation

#### Mounting
**File**: [src/fs/fat32.c](src/fs/fat32.c#L47)

```c
fs_node_t *fat32_mount(uint16_t bus, uint8_t drive, uint32_t partition_lba_start) {
  ata_bus = bus;
  ata_drive = drive;

  uint16_t boot_sector_buffer[256];
  if (ata_read_sector(ata_bus, ata_drive, partition_lba_start,
                      boot_sector_buffer) != 0) {
    draw_string(10, 100, "FAT32: Falha ao ler o setor de boot.", 0xFF0000);
    return NULL;
  }

  memcpy(&boot_sector, boot_sector_buffer, sizeof(fat32_boot_sector_t));

  if (strncmp((char *)boot_sector.BS_FilSysType, "FAT32   ", 8) != 0) {
    draw_string(10, 110, "FAT32: Filesystem nao e FAT32.", 0xFF0000);
    return NULL;
  }

  // Calculate important offsets
  fat_begin_lba = partition_lba_start + boot_sector.BPB_RsvdSecCnt;
  cluster_begin_lba =
      fat_begin_lba + (boot_sector.BPB_NumFATs * boot_sector.BPB_FATSz32);
  root_dir_first_cluster = boot_sector.BPB_RootClus;

  fs_node_t *root_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
  memset(root_node, 0, sizeof(fs_node_t));
  strcpy(root_node->name, "/");
  root_node->flags = FS_DIRECTORY;
  root_node->inode = root_dir_first_cluster;
  root_node->open = fat32_open;
  root_node->close = fat32_close;

  fat32_mounted = 1;
  return root_node;
}
```

#### File Reading
**File**: [src/fs/fat32.c](src/fs/fat32.c#L500+)

```c
static uint32_t fat32_read(fs_node_t *node, uint32_t offset, uint32_t size,
                           uint8_t *buffer) {
  if (!node || (node->flags & FS_FILE) == 0) {
    return 0;
  }

  uint32_t cluster_size = boot_sector.BPB_SecPerClus * boot_sector.BPB_BytsPerSec;
  uint32_t start_cluster_index = offset / cluster_size;
  uint32_t offset_in_cluster = offset % cluster_size;
  uint32_t current_cluster = node->inode;

  // Navigate FAT to find starting cluster
  for (uint32_t i = 0; i < start_cluster_index; i++) {
    uint32_t fat_sector = fat_begin_lba + (current_cluster * 4 / boot_sector.BPB_BytsPerSec);
    uint32_t fat_offset = (current_cluster * 4) % boot_sector.BPB_BytsPerSec;
    uint16_t fat_buffer[256];
    ata_read_sector(ata_bus, ata_drive, fat_sector, fat_buffer);
    current_cluster = ((uint32_t *)fat_buffer)[fat_offset / 4] & 0x0FFFFFFF;

    if (current_cluster >= 0x0FFFFFF8) {
      return 0;  // End of chain before desired offset
    }
  }

  uint32_t bytes_read = 0;
  uint32_t bytes_to_read = size;

  while (bytes_to_read > 0 && current_cluster < 0x0FFFFFF8) {
    uint8_t cluster_buffer[cluster_size];
    uint32_t cluster_lba =
        cluster_begin_lba + (current_cluster - 2) * boot_sector.BPB_SecPerClus;

    for (int i = 0; i < boot_sector.BPB_SecPerClus; i++) {
      ata_read_sector(ata_bus, ata_drive, cluster_lba + i,
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

    // Get next cluster
    uint32_t fat_sector = fat_begin_lba + (current_cluster * 4 / boot_sector.BPB_BytsPerSec);
    uint32_t fat_offset = (current_cluster * 4) % boot_sector.BPB_BytsPerSec;
    uint16_t fat_buffer[256];
    ata_read_sector(ata_bus, ata_drive, fat_sector, fat_buffer);
    current_cluster = ((uint32_t *)fat_buffer)[fat_offset / 4] & 0x0FFFFFFF;
  }

  return bytes_read;
}
```

#### FAT32 Disk Write Operations
**File**: [src/fs/fat32.c](src/fs/fat32.c#L110+) - Format Function

Multiple disk write operations happen during FAT32 formatting:
1. **Boot Sector Write**: `ata_write_sector()` to sectors 0 and backup
2. **FSInfo Write**: `ata_write_sector()` to FSInfo sector
3. **FAT Write**: `ata_write_sector()` for FAT table sectors
4. **Root Directory Write**: `ata_write_sector()` for root cluster
5. **FAT Entry Write**: `fat32_write_fat_entry()` updates FAT table on disk

### ATA Disk Driver
**File**: [src/drivers/ata.c](src/drivers/ata.c)

```c
int ata_read_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer) {
    // Select drive
    outb(bus + 6, 0x40 | drive | ((lba >> 24) & 0x0F));
    
    // Small delay for hardware
    for(int i=0; i<100; i++) inb(bus + ATA_REG_STATUS);

    outb(bus + 2, 1);
    outb(bus + 3, (uint8_t)lba);
    outb(bus + 4, (uint8_t)(lba >> 8));
    outb(bus + 5, (uint8_t)(lba >> 16));
    outb(bus + 7, ATA_CMD_READ);

    if (ata_wait_bsy(bus) < 0) return -1;
    if (ata_wait_drq(bus) < 0) return -1;

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(bus);
    }
    return 0;
}

void ata_write_sector(uint16_t bus, uint8_t drive, uint32_t lba, uint16_t* buffer) {
    outb(bus + 6, 0x40 | drive | ((lba >> 24) & 0x0F));
    outb(bus + 2, 1);
    outb(bus + 3, (uint8_t)lba);
    outb(bus + 4, (uint8_t)(lba >> 8));
    outb(bus + 5, (uint8_t)(lba >> 16));
    outb(bus + 7, ATA_CMD_WRITE);

    if (ata_wait_bsy(bus) < 0) return;
    if (ata_wait_drq(bus) < 0) return;

    for (int i = 0; i < 256; i++) {
        outw(bus, buffer[i]);
    }

    // Flush cache to persist writes
    outb(bus + 7, 0xE7);
    ata_wait_bsy(bus);
}
```

---

## 5. RECENT CHANGES AND BUILD INFO

### Git History (Last 7 commits)
```
2f36c7e Create README.md
8614069 Docker Support
2a52f3c Features 1.0
5e9b8c8 chore: apply gitignore and untrack build artifacts
dd7d048 Wayland feature
1baa24b Adicionando LGX (Liwus Graphical eXtension)
c2bc5f6 Massive refactor: new kernel and build system architecture
```

### Compiled Binaries Available
- ✓ `kernel.bin` - Kernel executable
- ✓ `liw.elf` - ELF application loader

---

## 6. MEMORY LAYOUT AND HIGH MEMORY ADDRESSES

### Typical Virtual Address Mapping
- **0x00000000 - 0x00FFFFFF**: Lower 16MB (usually kernel code/data)
- **0xFD000000 - 0xFD000000 + 16MB**: Video/Graphics RAM (mapped to physical 0xFD000000)
- **0xBFFFF000 - 0xBFFFF000 + 16KB**: User stack (grows downward)
- **0x40000000+**: User heap

### Address 0xF0011F53 Pattern
The high memory address pattern referenced (0xF00xxxxx) typically indicates:
- Graphics/VRAM mapping region
- Frame buffer addresses
- Device memory regions

### Known Mappings
```c
// From init_vmm():
uint32_t fb_phys = 0xFD000000;  // Physical frame buffer
// Mapped to same virtual address (identity mapping)

// User program layout:
entry_point = 0x08048000;       // Typical ELF entry
user_stack = 0xBFFFF000;         // Top of user stack
heap_start = 0x40000000;         // Heap start
```

---

## 7. KEY FILES REFERENCE

| File | Purpose |
|------|---------|
| [src/kernel/isr.c](src/kernel/isr.c) | Interrupt/Exception handlers (page faults here) |
| [src/kernel/vmm.c](src/kernel/vmm.c) | Virtual memory manager (paging implementation) |
| [src/kernel/vmm.h](include/vmm.h) | VMM data structures |
| [src/boot/interrupt.s](src/boot/interrupt.s) | Assembly interrupt stubs |
| [src/boot/boot.s](src/boot/boot.s) | Boot code with paging enable |
| [src/fs/vfs.c](src/fs/vfs.c) | Virtual file system abstraction |
| [src/fs/fat32.c](src/fs/fat32.c) | FAT32 implementation with disk I/O |
| [src/drivers/ata.c](src/drivers/ata.c) | ATA disk driver (read/write) |

---

## Debugging Tips

1. **To trace page faults**: Monitor serial output in `isr_handler()` - CR2 will contain faulting address
2. **To check memory mapping**: Use `vmm_map_page()` calls to diagnose mapping issues
3. **To debug filesystem**: Check FAT32 sector reads/writes in `ata_read_sector()`/`ata_write_sector()`
4. **To inspect binaries**: Use `objdump -d kernel.bin` to disassemble and find instructions by address

---

Generated from LiwusOS kernel source code for debugging purposes.
