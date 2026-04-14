#include "ata.h"
#include "fat32.h"
#include "gdt.h"
#include "idt.h"
#include "initrd.h"
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
#include "wifi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool is_live_mode = true; // Definir global

extern uint32_t end;
extern void refresh_screen();
extern int graphics_exclusive_active(void);

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

static void ensure_fat32_disk_ready(void) {
  int format_progress = 0;

  fs_node_t *fat_root = fat32_mount(ATA_PRIMARY, ATA_MASTER, 0);
  if (!fat_root) {
    serial_print("FAT32 mount failed, formatting disk...\n");
    if (fat32_format(&format_progress) == 0) {
      fat_root = fat32_mount(ATA_PRIMARY, ATA_MASTER, 0);
    }
  }

  if (fat_root) {
    vfs_mount("/house/localhost", fat_root);
    serial_print("FAT32 disk mounted at /house/localhost\n");
  } else {
    serial_print("FAT32 disk unavailable\n");
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
  uint32_t memory_size = mbi->mem_upper * 1024;
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
  serial_print("VMM initialized\n");

  vfs_init();
  serial_print("VFS initialized\n");

  vga_init();
  vga_puts("LiwusOS Kernel Booting (Text Mode Only)...\n");

  // init_video(mbi);

  pci_init();
  net_init();
  tcp_init();
  udp_init();
  dns_init();

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
  ensure_fat32_disk_ready();

  init_timer(100);
  init_tasking();
  init_syscalls();
  create_task_named(task_terminal_loop, "terminal");

  extern void liwshd_loop();
  create_task_named(liwshd_loop, "liwshd");

  asm volatile("sti");
  while (1) {
    asm volatile("hlt");
  }
}
