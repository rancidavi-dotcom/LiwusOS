#include "ata.h"
#include "ahci.h"
#include "sdfs.h"
#include "mouse.h"
#include "gdt.h"
#include "idt.h"
#include "apic.h"
#include "acpi.h"
#include "initrd.h"
#include "kheap.h"
#include "keyboard.h"
#include "multiboot.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "timer.h"
#include "vfs.h"
#include "syscall.h"
#include "vmm.h"
#include "vga.h"
#include "terminal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Aviso de estagio: imprime no serial e na tela para localizar travamentos. */
static void boot_stage(const char *s) {
    serial_print("[BOOT-STAGE] ");
    serial_print(s);
    serial_print("\n");
    vga_puts("[boot] ");
    vga_puts(s);
    vga_puts("\n");
}

/* Panic registers (filled by ISR handler before calling kernel_panic) */
registers_t *g_panic_regs = NULL;

void kernel_panic(const char *msg) {
    asm volatile("cli");

    vga_puts("\033[44;37m\033[2J\033[H");
    vga_puts("\n\n  *** KERNEL PANIC ***\n\n");
    vga_puts("  A critical system fault has occurred.\n");
    vga_puts("  Details: ");
    vga_puts(msg);
    vga_puts("\n\n");

    if (g_panic_regs) {
        char num[32];
        vga_puts("  RIP: "); itoa((int)(g_panic_regs->rip & 0xFFFFFFFF), num, 16); vga_puts(num);
        vga_puts("  RSP: "); itoa((int)(g_panic_regs->rsp & 0xFFFFFFFF), num, 16); vga_puts(num);
        vga_puts("\n  RAX: "); itoa((int)(g_panic_regs->rax & 0xFFFFFFFF), num, 16); vga_puts(num);
        vga_puts("  RBX: "); itoa((int)(g_panic_regs->rbx & 0xFFFFFFFF), num, 16); vga_puts(num);
        vga_puts("  RDI: "); itoa((int)(g_panic_regs->rdi & 0xFFFFFFFF), num, 16); vga_puts(num);
        vga_puts("  RSI: "); itoa((int)(g_panic_regs->rsi & 0xFFFFFFFF), num, 16); vga_puts(num);
        vga_puts("\n  Err: "); itoa(g_panic_regs->err_code, num, 16); vga_puts(num);
        vga_puts("  CS: "); itoa(g_panic_regs->cs, num, 16); vga_puts(num);
        vga_puts("  RFL: "); itoa((int)(g_panic_regs->rflags & 0xFFFFFFFF), num, 16); vga_puts(num);
        vga_puts("\n");
    }

    serial_print("KERNEL PANIC: ");
    serial_print(msg);
    serial_print("\n");

    vga_puts("\n  System Halted.\n");
    while (1) {
        asm volatile("hlt");
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
    } else if (tag->type == 14 || tag->type == 15) { /* ACPI RSDP (v1/v2) */
      acpi_set_rsdp((uint64_t)mbi_addr + offset + 8);
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

void init_fpu() {
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    cr0 |= (1 << 16); /* WP: supervisor writes obey leaf PTE_W permissions. */
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    asm volatile("finit");
}

/*
 * Monta o SDFS em um disco real; se nao houver disco formatado,
 * formata.  Sem disco, usa um ramdisk de 64 MB.
 * No primeiro boot (modo normal), copia o conteudo do initrd para o SDFS.
 */
static void init_filesystem(int kernel_test_mode) {
    uint16_t disk_bus = ATA_PRIMARY;
    uint8_t disk_drive = ATA_MASTER;
    fs_node_t *sdfs_root;

    if (ata_find_first(&disk_bus, &disk_drive) == 0) {
        serial_print("ATA disk found, trying SDFS mount...\n");
    } else {
        serial_print("No ATA disk found, trying AHCI/ATA mount...\n");
    }

    sdfs_root = sdfs_mount(disk_bus, disk_drive, 0);
    boot_stage("fs: mount tried");

    if (!sdfs_root) {
        serial_print("SDFS: no valid filesystem, formatting disk...\n");
        vga_puts("Formatting disk for first use...\n");
        if (sdfs_format() == 0) {
            sdfs_root = sdfs_mount(disk_bus, disk_drive, 0);
        }
        boot_stage("fs: formatted");
    }

    if (sdfs_root) {
        vfs_mount("/house/localhost", sdfs_root);
        vfs_mount("/", sdfs_root);
        serial_print("SDFS disk mounted at /\n");
        vga_puts("Disk mounted at /\n");

        if (!kernel_test_mode) {
            uint32_t installed_size = 0;
            void *flag = sdfs_read_file("/.system_installed", &installed_size);
            if (!flag) {
                serial_print("First boot: copying initrd files to SDFS...\n");
                ata_set_writeback(1);
                if (mb2_mods_count > 0) {
                    initrd_copy_to_sdfs(NULL);
                }
                sdfs_flush_bitmap();
                ata_flush_cache();
                ata_set_writeback(0);
                sdfs_create_file("/.system_installed");
                sdfs_write_file("/.system_installed", (uint8_t *)"1", 1);
                serial_print("System installed to disk.\n");
            } else {
                kfree(flag);
                serial_print("SDFS: system already installed.\n");
            }
        }
        return;
    }

    serial_print("SDFS disk unavailable, falling back to ramdisk\n");
    vga_puts("WARNING: no disk found; using ramdisk (data lost on reboot)\n");
    sdfs_enable_ramdisk(64);
    if (sdfs_format() != 0) {
        serial_print("SDFS: ramdisk format failed\n");
        return;
    }
    sdfs_root = sdfs_mount(0, 0, 0);
    if (!sdfs_root)
        return;
    vfs_mount("/house/localhost", sdfs_root);
    vfs_mount("/", sdfs_root);
    if (mb2_mods_count > 0 && !kernel_test_mode) {
        initrd_copy_to_sdfs(NULL);
    }
    sdfs_create_file("/.system_installed");
    sdfs_write_file("/.system_installed", (uint8_t *)"1", 1);
}

/* The kernel starts only the user-mode init process. Init owns shell restart
 * policy; the kernel supervisor is retained as a boot recovery path. */
static void user_init_supervisor_task(void) {
  char *init_argv[] = { "liwus_init", NULL };
  for (;;) {
    int init_pid = launch_initrd_program_argv("liwus_init", init_argv);
    if (init_pid < 0) {
      serial_print("[boot] user init failed to load; starting kernel terminal\n");
      create_task_named(terminal_task, "terminal-fallback");
      return;
    }
    last_foreground_pid = -1;

    int status = -1;
    if (sys_waitpid(init_pid, &status, 0) != init_pid) {
      serial_print("[boot] waitpid for user init failed; starting kernel terminal\n");
      create_task_named(terminal_task, "terminal-fallback");
      return;
    }
    serial_print("[boot] user init exited; restarting\n");
  }
}

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
  (void)magic;
  uint64_t reserved_start = (uint64_t)end + 0x1000;
  uint64_t pmm_bitmap_bytes;
  uint64_t heap_start;

  init_serial();
  serial_print("LiwusOS Kernel Booting...\n");

  parse_multiboot2(mbi_addr);
  boot_stage("multiboot2 ok");

  {
    extern uint64_t vga_fb_addr;
    extern uint32_t vga_fb_width, vga_fb_height, vga_fb_pitch;
    extern uint8_t vga_fb_bpp;
    serial_print("[fb] addr=0x");
    serial_print_hex((uint32_t)vga_fb_addr);
    serial_print(" wh=");
    char nb[16];
    itoa(vga_fb_width, nb, 10); serial_print(nb);
    serial_print("x"); itoa(vga_fb_height, nb, 10); serial_print(nb);
    serial_print(" pitch="); itoa(vga_fb_pitch, nb, 10); serial_print(nb);
    serial_print(" bpp="); itoa(vga_fb_bpp, nb, 10); serial_print(nb);
    serial_print("\n");
  }

  for (uint32_t i = 0; i < mb2_mods_count; i++) {
    struct {
      uint32_t type, size;
      uint32_t mod_start;
      uint32_t mod_end;
      uint32_t cmdline;
    } __attribute__((packed)) *mod = (void *)(uint64_t)mb2_mods_addr[i];
    if ((uint64_t)mod->mod_end + 0x1000 > reserved_start) {
      reserved_start = (uint64_t)mod->mod_end + 0x1000;
    }
  }

  if (memory_size == 0) {
    memory_size = 512 * 1024 * 1024;
  }
  /* boot.s identity-maps only the first 1 GiB. Until higher physical memory
   * gets a virtual mapping, the PMM and kernel heap must stay inside it. */
  if (memory_size > 0x40000000ULL) memory_size = 0x40000000ULL;

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

  /* pmm_init_multiboot_regions marks every available RAM range free. Put
   * back the complete boot image/module range before any PMM allocation. */
  pmm_deinit_region(0, reserved_start);
  pmm_reserve_region(reserved_start, pmm_bitmap_bytes);

  /* Firmware framebuffer memory can otherwise be reported as available RAM
   * on some machines. Keep its backing frames out of the PMM before claiming
   * the kernel heap arena. */
  {
    extern uint64_t vga_fb_addr;
    extern uint32_t vga_fb_height, vga_fb_pitch;
    if (vga_fb_addr && vga_fb_pitch && vga_fb_height)
      pmm_reserve_region(vga_fb_addr,
                         (uint64_t)vga_fb_pitch * vga_fb_height);
  }

  heap_start = reserved_start + pmm_bitmap_bytes + 0x1000;
  if (heap_start < 0x1000000) {
      heap_start = 0x1000000;
  }
  kheap_set_start(heap_start);
  init_vmm(memory_size);

  {
    extern uint64_t vga_fb_addr;
    extern uint32_t vga_fb_height, vga_fb_pitch;
    if (vga_fb_addr != 0) {
      extern void vmm_map_framebuffer(uint64_t phys_addr, uint64_t size);
      vmm_map_framebuffer(vga_fb_addr, (uint64_t)vga_fb_pitch * vga_fb_height);
    }
  }

  vfs_init();
  serial_print("VFS initialized\n");
  vga_init();
  vga_puts("LiwusOS Kernel Booting\n");
  boot_stage("vfs ok");

  init_mouse();
  acpi_init();
  init_apic();
  pci_init();
  boot_stage("pci ok");

  ahci_init();
  ata_bmide_init();
  boot_stage("disk-hw ok");

  if (mb2_mods_count > 0) {
    struct {
      uint32_t type, size;
      uint32_t mod_start;
      uint32_t mod_end;
      uint32_t cmdline;
    } __attribute__((packed)) *mod = (void *)(uint64_t)mb2_mods_addr[0];
    init_initrd(mod->mod_start, mod->mod_end - mod->mod_start);
    serial_print("Initrd loaded into memory\n");
  } else {
    serial_print("Initrd not provided by bootloader\n");
  }

  /* Detect test mode from initrd (skips first-boot copy). */
  int kernel_test_mode = 0;
  {
    uint32_t tm_size = 0;
    void *tm_flag = initrd_get_file("test_mode", &tm_size);
    if (tm_flag) {
      kernel_test_mode = 1;
    }
  }

  init_filesystem(kernel_test_mode);

  init_tasking();
  init_timer(100);
  init_syscalls();
  boot_stage("sched ok");

  if (kernel_test_mode) {
    serial_print("[boot] TEST MODE detected (test_mode in initrd)\n");
#ifdef KERNEL_TEST
    extern void test_runner_task(void);
    extern volatile int test_runner_done;

    create_task_named(test_runner_task, "test_runner");
    asm volatile("sti");
    while (!test_runner_done) {
      switch_task();
    }

    serial_print("LIWUS_BOOT_READY\n");
    serial_print("[boot] Tests complete. Halting.\n");
    while (1) {
      asm volatile("hlt");
    }
#else
    serial_print("[boot] test_mode marker present but kernel built without tests\n");
#endif
  }

  serial_print("LiwusOS text mode ready\n");
  vga_puts("\nLiwusOS - ring-3 shell\n\n");

  create_task_named(user_init_supervisor_task, "init-supervisor");

  serial_print("LIWUS_BOOT_READY\n");
  asm volatile("sti");
  while (1) {
    asm volatile("hlt");
  }
}
