#include "ata.h"
#include "sdfs.h"
#include "gdt.h"
#include "idt.h"
#include "initrd.h"
#include "boot_anim.h"
#include "io.h"
#include "kheap.h"
#include "keyboard.h"
#include "mouse.h"
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
#include "video.h"
#include "syscall.h"
#include "vmm.h"
#include "vga.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool is_live_mode = true;
uint32_t memory_size = 0;

extern uint32_t end;
extern void refresh_screen();

static int graphics_exclusive_owner = -1;

int graphics_exclusive_active(void) {
    return graphics_exclusive_owner >= 0;
}

void graphics_exclusive_acquire(int pid) {
    graphics_exclusive_owner = pid;
}

void graphics_exclusive_release(int pid) {
    if (graphics_exclusive_owner == pid || pid < 0) {
        graphics_exclusive_owner = -1;
    }
}

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

    while (key_count < (int)(sizeof(key_batch) / sizeof(key_batch[0])) &&
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
      boot_anim_init();
      initrd_copy_to_sdfs(boot_anim_update);
      boot_anim_finish();
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
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // EM
    cr0 |= (1 << 1);  // MP
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // OSFXSR
    cr4 |= (1 << 10); // OSXMMEXCPT
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    asm volatile("finit");
}

void kernel_main(uint32_t magic, multiboot_info_t *mbi) {
  (void)magic;
  uint32_t reserved_start = (uint32_t)&end + 0x1000;
  memory_size = mbi->mem_upper * 1024;
  uint32_t pmm_bitmap_bytes;
  uint32_t heap_start;

  init_serial();
  serial_print("LiwusOS Kernel Booting [Network + DNS Patch]...\n");

  if (mbi->mods_count > 0) {
    multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;
    if (mods[0].mod_end + 0x1000 > reserved_start) {
      reserved_start = mods[0].mod_end + 0x1000;
    }
  }

  pmm_bitmap_bytes = ((memory_size / 4096) / 32) * sizeof(uint32_t);
  if (pmm_bitmap_bytes == 0) {
    pmm_bitmap_bytes = 4096;
  }

  init_gdt();
  init_idt();
  init_fpu();
  pmm_init(reserved_start, memory_size);

  heap_start = reserved_start + pmm_bitmap_bytes + 0x1000;
  kheap_set_start(heap_start);
  init_vmm(memory_size);

  vfs_init();
  serial_print("VFS initialized\n");

  vga_init();
  vga_puts("LiwusOS Kernel Booting (Text Mode Only)...\n");

  init_video(mbi);

  pci_init();
  ata_bmide_init(); /* BM-IDE DMA init */
  net_init();
  tcp_init();
  udp_init();
  dns_init();
  
  extern void usb_init();
  usb_init();

  if (mbi->mods_count > 0) {
    multiboot_module_t *mods = (multiboot_module_t *)mbi->mods_addr;
    fs_node_t *initrd_root = init_initrd(mods[0].mod_start, mods[0].mod_end - mods[0].mod_start);
    vfs_mount("/", initrd_root);
    serial_print("Initrd initialized and mounted at /\n");
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
  /* Monta SDFS para escrita de arquivos do usuario. System files
     continuam no initrd (RAM) — sem copia de first-boot. */
  {
    fs_node_t *sdfs_root = sdfs_mount(ATA_PRIMARY, ATA_MASTER, 0);
    if (!sdfs_root) {
      serial_print("SDFS mount failed, formatting disk...\n");
      if (sdfs_format() == 0) {
        sdfs_root = sdfs_mount(ATA_PRIMARY, ATA_MASTER, 0);
      }
    }
    if (sdfs_root) {
      vfs_mount("/house/localhost", sdfs_root);
      serial_print("SDFS disk mounted at /house/localhost (user files only)\n");
    } else {
      serial_print("SDFS disk unavailable\n");
    }
  }

  init_timer(100);
  init_tasking();
  init_syscalls();

  extern void usb_start_polling(void);
  usb_start_polling();

  create_task_named(task_terminal_loop, "terminal");

  asm volatile("sti");
  while (1) {
    asm volatile("hlt");
  }
}
