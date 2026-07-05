#include "ata.h"
#include "ahci.h"
#include "sdfs.h"
#include "lgx.h"
#include "mouse.h"
#include "gdt.h"
#include "idt.h"
#include "initrd.h"
#include "io.h"
#include "kheap.h"
#include "keyboard.h"
#include "multiboot.h"
#include "net.h"
#include "netstack.h"
#include "pci.h"
#include "pmm.h"
#include "rtl8139.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "terminal.h"
#include "tcp.h"
#include "udp.h"
#include "dns.h"
#include "timer.h"
#include "vfs.h"
#include "syscall.h"
#include "vmm.h"
#include "vga.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void panic_delay(uint32_t count) {
    for (volatile uint32_t i = 0; i < count; i++) {
        asm volatile("pause");
    }
}

static void beep_tone(uint32_t freq) {
    if (freq == 0) {
        outb(0x61, inb(0x61) & 0xFC);
    } else {
        uint32_t div = 1193180 / freq;
        outb(0x43, 0xb6);
        outb(0x42, (uint8_t)(div));
        outb(0x42, (uint8_t)(div >> 8));
        outb(0x61, inb(0x61) | 3);
    }
}

static void set_leds(uint8_t leds) {
    panic_delay(50000);
    if (inb(0x64) & 2) return;
    outb(0x60, 0xED);
    panic_delay(500000);
    if (inb(0x64) & 2) return;
    outb(0x60, leds);
}

void kernel_panic(const char *msg) {
    asm volatile("cli");
    
    // Matrix glitch effect: black background, red text
    vga_puts("\033[40;31m\033[2J\033[H");
    
    const char *title = "\n\n  [ FATAL ERROR ] KERNEL PANIC\n\n";
    for (int i = 0; title[i]; i++) {
        vga_putc(title[i]);
        beep_tone(1000 + (i % 2)*500); 
        panic_delay(2000000); // typing effect
    }
    beep_tone(0); // stop sound
    
    vga_puts("  A critical system fault has occurred in supervisor mode.\n");
    vga_puts("  Details: ");
    vga_puts(msg);
    vga_puts("\n\n");
    
    panic_delay(50000000);
    
    vga_puts("  Call Trace:\n");
    vga_puts("  [<ffffffff81001234>] ? system_crash+0x12/0x50\n");
    vga_puts("  [<ffffffff81005678>] ? unhandled_exception+0x8f/0x100\n");
    vga_puts("  [<ffffffff810abcdf>] ? panic+0x42/0x60\n\n");
    
    panic_delay(50000000);
    
    vga_puts("  Dumping registers...\n");
    vga_puts("  RAX: 0000000000000000 RBX: ffffffff81000000\n");
    vga_puts("  RCX: 0000000000000042 RDX: 0000000000000000\n");
    vga_puts("  RSP: ffffc90000003f00 RBP: ffffc90000003f20\n\n");
    
    panic_delay(50000000);
    
    vga_puts("\033[40;37m  Kernel halted. CPU locked.\n");
    
    serial_print("KERNEL PANIC: ");
    serial_print(msg);
    serial_print("\n");
    
    uint8_t led_state = 1;
    uint32_t tone = 800;
    uint8_t color_idx = 0;
    
    while (1) {
        // Siren sound
        beep_tone(tone);
        tone = (tone == 800) ? 1200 : (tone == 1200) ? 2000 : 800;
        
        // Flash LEDs rapidly (all at once or cycling fast)
        set_leds(7); // All on
        panic_delay(10000000);
        set_leds(0); // All off
        panic_delay(10000000);
        
        // Extreme epilepsy flash!
        char ansi_cmd[] = "\033[4X;3Ym\033[2J\033[H";
        ansi_cmd[3] = '0' + (color_idx % 8);
        ansi_cmd[6] = '0' + ((color_idx + 3) % 8);
        vga_puts(ansi_cmd);
        
        vga_puts("\n\n\n\n\n\n");
        vga_puts("     ###################################################\n");
        vga_puts("     #                                                 #\n");
        vga_puts("     #    [ !!! ] EPILEPSY WARNING - KERNEL PANIC      #\n");
        vga_puts("     #                                                 #\n");
        vga_puts("     #             SYSTEM HAS BEEN HALTED              #\n");
        vga_puts("     #                                                 #\n");
        vga_puts("     ###################################################\n\n");
        vga_puts("     ERROR: ");
        vga_puts(msg);
        vga_puts("\n");
        
        color_idx++;
        
        panic_delay(10000000); // Very fast delay for flashing
    }
}

bool is_live_mode = true;
uint64_t memory_size = 0;
uint32_t mb2_mods_count = 0;
uint32_t mb2_mods_addr[16]; /* up to 16 modules */

extern uint64_t end[];

/* Multiboot2 tag types */
#define MB2_TAG_END       0
#define MB2_TAG_MEMMAP    6

/* Parse Multiboot2 info structure into kernel globals */
static void parse_multiboot2(uint32_t mbi_addr) {
  struct {
    uint32_t total_size;
    uint32_t reserved;
  } __attribute__((packed)) *info = (void *)(uint64_t)mbi_addr;

  uint32_t offset = 8;
  while (offset + 8 <= info->total_size) {
    struct {
      uint32_t type;
      uint32_t size;
    } __attribute__((packed)) *tag = (void *)((uint64_t)mbi_addr + offset);

    if (tag->type == 1) { /* Basic memory info */
      // Ignored in favor of memory map (tag 6) for >4GB support
    } else if (tag->type == 6) { /* Memory map */
      struct {
        uint32_t type, size;
        uint32_t entry_size;
        uint32_t entry_version;
      } __attribute__((packed)) *mmap = (void *)tag;
      uint32_t entries = (mmap->size - sizeof(*mmap)) / mmap->entry_size;
      for (uint32_t i = 0; i < entries; i++) {
        struct {
          uint64_t base_addr;
          uint64_t length;
          uint32_t type;
          uint32_t reserved;
        } __attribute__((packed)) *entry = (void *)((uint64_t)tag + sizeof(*mmap) + i * mmap->entry_size);
        if (entry->type == 1) { // Available RAM
          uint64_t end_addr = entry->base_addr + entry->length;
          if (end_addr > memory_size) memory_size = end_addr;
        }
      }
    } else if (tag->type == 3) { /* Module */
      struct {
        uint32_t type, size;
        uint32_t mod_start;
        uint32_t mod_end;
        uint32_t cmdline;
      } __attribute__((packed)) *mod = (void *)tag;
      if (mb2_mods_count < 16) {
        mb2_mods_addr[mb2_mods_count++] = (uint32_t)(uint64_t)mod;
      }
    } else if (tag->type == MB2_TAG_END) {
      break;
    } else if (tag->type == 8) { /* Framebuffer */
      struct {
        uint32_t type, size;
        uint64_t framebuffer_addr;
        uint32_t framebuffer_pitch;
        uint32_t framebuffer_width;
        uint32_t framebuffer_height;
        uint8_t framebuffer_bpp;
        uint8_t framebuffer_type;
        uint8_t reserved;
      } __attribute__((packed)) *fb = (void *)tag;
      extern uint64_t vga_fb_addr;
      extern uint32_t vga_fb_width, vga_fb_height, vga_fb_pitch;
      extern uint8_t vga_fb_bpp;
      vga_fb_addr = fb->framebuffer_addr;
      vga_fb_pitch = fb->framebuffer_pitch;
      vga_fb_width = fb->framebuffer_width;
      vga_fb_height = fb->framebuffer_height;
      vga_fb_bpp = fb->framebuffer_bpp;
    }

    offset += (tag->size + 7) & ~7; /* align to 8 bytes */
  }
}

extern void pmm_init_region(uint64_t base, uint64_t size);

static void pmm_init_multiboot_regions(uint32_t mbi_addr) {
  struct {
    uint32_t total_size;
    uint32_t reserved;
  } __attribute__((packed)) *info = (void *)(uint64_t)mbi_addr;

  uint32_t offset = 8;
  while (offset + 8 <= info->total_size) {
    struct {
      uint32_t type;
      uint32_t size;
    } __attribute__((packed)) *tag = (void *)((uint64_t)mbi_addr + offset);

    if (tag->type == 6) { /* Memory map */
      struct {
        uint32_t type, size;
        uint32_t entry_size;
        uint32_t entry_version;
      } __attribute__((packed)) *mmap = (void *)tag;
      uint32_t entries = (mmap->size - sizeof(*mmap)) / mmap->entry_size;
      for (uint32_t i = 0; i < entries; i++) {
        struct {
          uint64_t base_addr;
          uint64_t length;
          uint32_t type;
          uint32_t reserved;
        } __attribute__((packed)) *entry = (void *)((uint64_t)tag + sizeof(*mmap) + i * mmap->entry_size);
        if (entry->type == 1) { // Available RAM
          pmm_init_region(entry->base_addr, entry->length);
        }
      }
    } else if (tag->type == MB2_TAG_END) {
      break;
    }
    offset += (tag->size + 7) & ~7; /* align to 8 bytes */
  }
}

int graphics_exclusive_active(void) { return 0; }
void graphics_exclusive_acquire(int pid) { (void)pid; }
void graphics_exclusive_release(int pid) { (void)pid; }

void task_terminal_loop() {
  terminal_enable_console_mode();
  init_terminal_app();

  while (1) {
    char key_batch[64];
    int key_count = 0;
    char popped_key = 0;
    bool terminal_dirty;

    if (graphics_exclusive_active()) {
      asm volatile("hlt");
      continue;
    }

    // Não roubar teclas do aplicativo em primeiro plano
    while (last_foreground_pid <= 0 &&
           key_count < (int)(sizeof(key_batch) / sizeof(key_batch[0])) &&
           keyboard_pop_char(&popped_key)) {
      key_batch[key_count++] = popped_key;
    }

    for (int i = 0; i < key_count; i++) {
      update_terminal_key(key_batch[i]);
    }

    terminal_dirty = terminal_needs_update(timer_ticks);
    if (terminal_dirty) {
      terminal_flush_updates(timer_ticks);
    }

    if (key_count == 0 && !terminal_dirty) {
      asm volatile("hlt");
    }
  }
}

/*
 * ensure_disk_ready: Monta o SDFS e copia os system files do initrd
 *                    para o disco persistente no primeiro boot.
 *
 * Fluxo:
 *   1. Tenta montar o SDFS do ATA primary master
 *   2. Se falhar (disco sem formatação SDFS), formata via sdfs_format()
 *   3. Monta o SDFS em /house/localhost no VFS
 *   4. Verifica se existe /.system_installed (flag de first-boot)
 *   5. Se não existir: copia todos os arquivos do initrd para o SDFS
 *      com boot animation (initrd_copy_to_sdfs + boot_anim_update),
 *      depois cria o flag /.system_installed
 *   6. Se existir: apenas loga "system already installed"
 *
 * NOTA (LIVECD mode): Esta função está desabilitada para testes rápidos.
 * O sistema inteiro roda do initrd (RAM). Para reativar o disco
 * persistente, descomente a chamada ensure_disk_ready() em kernel_main().
 */
static void ensure_disk_ready(void) {
  fs_node_t *sdfs_root = sdfs_mount(ATA_PRIMARY, ATA_MASTER, 0);
  if (!sdfs_root) {
    serial_print("SDFS mount failed, formatting disk...\n");
    if (sdfs_format() == 0) {
      sdfs_root = sdfs_mount(ATA_PRIMARY, ATA_MASTER, 0);
    }
  }

  if (sdfs_root) {
    vfs_mount("/house/localhost", sdfs_root);
    serial_print("SDFS disk mounted at /house/localhost\n");

    // Primeira inicializacao? Copia system files do initrd para o SDFS
    uint32_t installed_size = 0;
    void *flag = sdfs_read_file("/.system_installed", &installed_size);
    if (!flag) {
      serial_print("First boot: copying system files to SDFS...\n");
      /*
       * Boot animation estilo PopOS/Mint:
       *   init()  -> desenha fundo, logo "LiwusOS", barra de progresso vazia
       *   update() -> preenche a barra conforme initrd_copy_to_sdfs progride
       *   finish() -> completa 100%, mostra "Ready!"
       *
       * initrd_copy_to_sdfs itera o tar do initrd e escreve cada
       * arquivo no SDFS via sdfs_create_file + sdfs_write_file.
       * O DMA do PIIX3 BM-IDE acelera as escritas (~8 setores por
       * chamada em vez de 1).
       */
      initrd_copy_to_sdfs(NULL);
      sdfs_create_file("/.system_installed");
      sdfs_write_file("/.system_installed", (uint8_t *)"1", 1);
      serial_print("System installed to SDFS.\n");
    } else {
      kfree(flag);
      serial_print("SDFS: system already installed.\n");
    }
  } else {
    serial_print("SDFS disk unavailable\n");
  }
}

void init_fpu() {
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    asm volatile("finit");
}

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
  (void)magic;
  uint64_t reserved_start = (uint64_t)end + 0x1000;
  uint64_t pmm_bitmap_bytes;
  uint64_t heap_start;

  init_serial();
  serial_print("LiwusOS Kernel Booting [Network + DNS Patch]...\n");

  /* Parse multiboot2 info */
  parse_multiboot2(mbi_addr);

  if (mb2_mods_count > 0) {
    struct {
      uint32_t type, size;
      uint32_t mod_start;
      uint32_t mod_end;
      uint32_t cmdline;
    } __attribute__((packed)) *mod = (void *)(uint64_t)mb2_mods_addr[0];
    if (mod->mod_end + 0x1000 > reserved_start) {
      reserved_start = mod->mod_end + 0x1000;
    }
  }

  if (memory_size == 0) {
    memory_size = 512 * 1024 * 1024;
  }

  pmm_bitmap_bytes = ((memory_size / 4096) / 32) * sizeof(uint32_t);
  if (pmm_bitmap_bytes == 0) {
    pmm_bitmap_bytes = 4096;
  }

  init_gdt();
  init_idt();
  {
    extern void tss_flush(void);
    tss_flush();
  }
  init_fpu();
  pmm_init(reserved_start, memory_size);
  pmm_init_multiboot_regions(mbi_addr);

  heap_start = reserved_start + pmm_bitmap_bytes + 0x1000;
  if (heap_start < 0x1000000) {
      heap_start = 0x1000000;
  }
  kheap_set_start(heap_start);
  init_vmm(memory_size);

  extern uint64_t vga_fb_addr;
  extern uint32_t vga_fb_width, vga_fb_height, vga_fb_pitch;
  if (vga_fb_addr != 0) {
    extern void vmm_map_framebuffer(uint64_t phys_addr, uint64_t size);
    vmm_map_framebuffer(vga_fb_addr, vga_fb_pitch * vga_fb_height);
  }

  vfs_init();
  serial_print("VFS initialized\n");

  lgx_init();


  vga_init();
  vga_puts("LiwusOS Kernel Booting (Text Mode Only)...\n");

  init_mouse();
  
  pci_init();
  init_gpu();
  ahci_init();      /* AHCI/SATA init */
  ata_bmide_init(); /* BM-IDE DMA init */
  net_init();
  tcp_init();
  udp_init();
  dns_init();
  
  extern void usb_init();
  usb_init();

  if (mb2_mods_count > 0) {
    struct {
      uint32_t type, size;
      uint32_t mod_start;
      uint32_t mod_end;
      uint32_t cmdline;
    } __attribute__((packed)) *mod = (void *)(uint64_t)mb2_mods_addr[0];
    init_initrd(mod->mod_start, mod->mod_end - mod->mod_start);
    serial_print("Initrd loaded into memory (for first boot setup)\n");
  } else {
    serial_print("Initrd not provided by bootloader\n");
  }

  // Rede e Disco
  {
    pci_device_t *net_dev = pci_get_net();
    if (net_dev) {
      init_rtl8139(net_dev);
      serial_print("RTL8139 ethernet initialized\n");
    } else {
      serial_print("No ethernet device found\n");
    }
  }
  // Monta SDFS no disco REAL (AHCI ou ATA)
  {
    uint16_t disk_bus = ATA_PRIMARY;
    uint8_t disk_drive = ATA_MASTER;
    fs_node_t *sdfs_root = NULL;
    
    // Tenta encontrar o disco ATA real
    if (ata_find_first(&disk_bus, &disk_drive) == 0) {
      serial_print("ATA disk found, trying SDFS mount...\n");
    } else {
      serial_print("No ATA disk found, trying AHCI...\n");
    }
    
    // Tenta montar o SDFS direto (disco já formatado de boot anterior)
    sdfs_root = sdfs_mount(disk_bus, disk_drive, 0);
    
    if (!sdfs_root) {
      // Disco não tem SDFS — formata pela primeira vez
      serial_print("SDFS: No valid filesystem, formatting disk...\n");
      vga_puts("Formatting disk for first use...\n");
      if (sdfs_format() == 0) {
        sdfs_root = sdfs_mount(disk_bus, disk_drive, 0);
      }
    }
    
    if (sdfs_root) {
      vfs_mount("/house/localhost", sdfs_root);
      serial_print("SDFS disk mounted at /house/localhost\n");
      vga_puts("Disk mounted at /house/localhost\n");

      // Primeira inicializacao? Copia system files do initrd para o SDFS
      uint32_t installed_size = 0;
      void *flag = sdfs_read_file("/.system_installed", &installed_size);
      if (!flag) {
        serial_print("First boot: copying system files to SDFS...\n");
        vga_puts("First boot: installing system files...\n");
        if (mb2_mods_count > 0) {
            initrd_copy_to_sdfs(NULL);
        }
        sdfs_create_file("/.system_installed");
        sdfs_write_file("/.system_installed", (uint8_t *)"1", 1);
        serial_print("System installed to disk.\n");
        vga_puts("System installed to disk!\n");
      } else {
        kfree(flag);
        serial_print("SDFS: system already installed.\n");
      }
    } else {
      serial_print("SDFS disk unavailable, falling back to ramdisk\n");
      vga_puts("WARNING: No disk found! Using ramdisk (data will be lost on reboot)\n");
      
      // Fallback para ramdisk se não tem disco
      sdfs_enable_ramdisk(64);
      if (sdfs_format() == 0) {
        sdfs_root = sdfs_mount(0, 0, 0);
      }
      if (sdfs_root) {
        vfs_mount("/", sdfs_root);
        if (mb2_mods_count > 0) {
            initrd_copy_to_sdfs(NULL);
        }
        sdfs_create_file("/.system_installed");
        sdfs_write_file("/.system_installed", (uint8_t *)"1", 1);
      }
    }
  }

  init_timer(100);
  init_tasking();
  init_syscalls();

  extern void usb_start_polling(void);
  usb_start_polling();

  create_task_named(lgx_compositor_task, "lgx_comp");
  create_task_named(task_terminal_loop, "terminal");


  serial_print("sti...\n");
  asm volatile("sti");
  while (1) {
    asm volatile("hlt");
  }
}
