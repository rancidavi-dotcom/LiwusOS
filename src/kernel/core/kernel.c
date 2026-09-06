#include "ata.h"
#include "ahci.h"
#include "sdfs.h"
#include "mouse.h"
#include "gdt.h"
#include "idt.h"
#include "apic.h"
#include "initrd.h"
#include "io.h"
#include "kheap.h"
#include "keyboard.h"
#include "multiboot.h"
#include "pci.h"
#include "gpu.h"
#include "audio.h"
#include "mp3.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "timer.h"
#include "vfs.h"
#include "syscall.h"
#include "vmm.h"
#include "vga.h"
#include "drivers/boot_splash.h"
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
    
    // Simple blue screen, white text
    vga_puts("\033[44;37m\033[2J\033[H");
    vga_puts("\n\n  *** KERNEL PANIC ***\n\n");
    vga_puts("  A critical system fault has occurred.\n");
    vga_puts("  Details: ");
    vga_puts(msg);
    vga_puts("\n\n");
    
    serial_print("KERNEL PANIC: ");
    serial_print(msg);
    serial_print("\n");
    
    vga_puts("  System Halted.\n");
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

/* Boot chime: emits the audio driver's welcome tone once the GUI is up. */
void audio_boot_chime_task() {
    static const audio_note_t chime[] = {
        { 523, 90 },  /* C5 */
        { 659, 90 },  /* E5 */
        { 784, 90 },  /* G5 */
        { 1047, 160 }, /* C6 */
        { 0, 60 }
    };
    audio_play_notes(chime, sizeof(chime) / sizeof(chime[0]),
                      AUDIO_DEFAULT_RATE, 70);
    while (1) {
        asm volatile("hlt");
    }
}

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
  (void)magic;
  uint64_t reserved_start = (uint64_t)end + 0x1000;
  uint64_t pmm_bitmap_bytes;
  uint64_t heap_start;

  init_serial();
  serial_print("LiwusOS Kernel Booting...\n");

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
  init_apic();

  extern uint64_t vga_fb_addr;
  extern uint32_t vga_fb_width, vga_fb_height, vga_fb_pitch;
  if (vga_fb_addr != 0) {
    extern void vmm_map_framebuffer(uint64_t phys_addr, uint64_t size);
    vmm_map_framebuffer(vga_fb_addr, vga_fb_pitch * vga_fb_height);
  }

  vfs_init();
  serial_print("VFS initialized\n");
  vga_init();
  boot_splash_init();
  vga_puts("LiwusOS Kernel Booting (Text Mode Only)...\n");

init_mouse();
   
   boot_splash_set_progress(10, "Inicializando hardware...");
   pci_init();
   init_gpu();
   ahci_init();      /* AHCI/SATA init */
   ata_bmide_init(); /* BM-IDE DMA init */
  
  extern void usb_init();
  usb_init();

  boot_splash_set_progress(25, "Inicializando audio e USB...");
  audio_init();     /* AC'97 audio driver (PC speaker fallback) */

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

  boot_splash_set_progress(40, "Montando sistema de arquivos...");


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

      boot_splash_set_progress(55, "Instalando arquivos do sistema...");

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

  boot_splash_set_progress(70, "Inicializando tarefas...");

  /* ---- Detect test mode from initrd ---- */
  int kernel_test_mode = 0;
  {
    uint32_t tm_size = 0;
    void *tm_flag = initrd_get_file("test_mode", &tm_size);
    if (tm_flag) {
      kernel_test_mode = 1;
      serial_print("[boot] TEST MODE detected (test_mode in initrd)\n");
    }
  }

  init_timer(100);
  init_tasking();
  init_syscalls();

if (kernel_test_mode) {
    /* ---- TCC integration test: if 'test_tcc' is present in the initrd,
       launch the Tiny C Compiler (userspace) to compile a C file and then
       run the resulting ELF. Exercised end-to-end by scripts/tcc_test.sh. */
      {
        extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
        extern void switch_task(void);
        uint32_t tcc_marker_size = 0;
        if (initrd_get_file("test_tcc", &tcc_marker_size)) {
          serial_print("[boot] TCC integration test detected\n");

/* ---- Phase 1: tcc -c to test object file output (diagnostic). */
         {
           static char *c_argv[] = { "tcc", "-c", "/house/localhost/hello_tcc.c", "-o", "/house/localhost/hello_tcc.o", NULL };
           int cpid = launch_initrd_program_argv("tcc", c_argv);
           serial_print("[tcc] launch compile pid=");
           char lb[16]; itoa(cpid, lb, 10); serial_print(lb);
           serial_print("\n");

           int found = 0;
           for (int spins = 0; spins < 400000 && !found; spins++) {
             switch_task();
             uint32_t esize = 0;
             void *edata = sdfs_read_file("/hello_tcc.o", &esize);
             if (edata && esize > 0) {
               kfree(edata);
               found = 1;
             }
           }

           uint32_t esize = 0;
           void *edata = sdfs_read_file("/hello_tcc.o", &esize);
           if (found && edata && esize > 0) {
             serial_print("OK: libtcc compiled hello_tcc.c (hello_tcc.o size=");
             char sz[16]; itoa(esize, sz, 10); serial_print(sz); serial_print(")\n");
             /* Dump first 64 bytes of .o file to check .text content */
             unsigned char *p = (unsigned char*)edata;
             char hexbuf[3];
             serial_print(".o bytes: ");
             for (int i = 0; i < 64 && i < (int)esize; i++) {
               hexbuf[0] = "0123456789abcdef"[(p[i]>>4)&0xF];
               hexbuf[1] = "0123456789abcdef"[p[i]&0xF];
               hexbuf[2] = ' ';
               serial_print(hexbuf);
             }
             serial_print("\n");
             kfree(edata);
           } else {
             if (edata) kfree(edata);
             serial_print("FAIL: hello_tcc.o missing\n");
           }
         }
       }
     }

    /* ---- Image decode smoke test: when 'test_img' is present in the
       initrd, decode the bundled SDFS images and report dimensions. This
       exercises the kernel-side stb_image integration end-to-end. Runs
       before the SDFS test suite so it reads from the real disk SDFS. */
    {
      extern int image_decode(const uint8_t *data, uint32_t size,
                              uint32_t **out_pixels, int *out_w, int *out_h);
      extern void image_free(uint32_t *pixels);
      uint32_t mark_size = 0;
      if (initrd_get_file("test_img", &mark_size)) {
        serial_print("[boot] Image decode test detected\n");
        const char *files[] = { "/teste.png", "/teste.jpg", "/teste.bmp" };
        int all_ok = 1;
        for (int f = 0; f < 3; f++) {
          const char *path = files[f];
          uint32_t fsize = 0;
          void *fdata = sdfs_read_file(path, &fsize);
          if (!fdata || fsize == 0) {
            if (fdata) kfree(fdata);
            serial_print("IMGFAIL: could not read "); serial_print(path); serial_print("\n");
            all_ok = 0;
            continue;
          }
          uint32_t *pixels = NULL;
          int w = 0, h = 0;
          int rc = image_decode((const uint8_t *)fdata, fsize, &pixels, &w, &h);
          kfree(fdata);
          if (rc == 0 && pixels) {
            serial_print("IMGOK: "); serial_print(path); serial_print(" = ");
            char nb[12]; itoa(w, nb, 10); serial_print(nb);
            serial_print("x"); itoa(h, nb, 10); serial_print(nb);
            serial_print("\n");
            image_free(pixels);
          } else {
            serial_print("IMGFAIL: decode "); serial_print(path); serial_print("\n");
            all_ok = 0;
          }
        }
        if (all_ok)
          serial_print("IMG_ALL_OK\n");

        /* ---- Recursive scan check: walk the whole SDFS like the image
           viewer does, and require a nested image in a subdir to show up.
           Proves "detect all images in the system" end to end. */
        {
          char stack[64][200];
          int top = -1;
          int found_nested = 0;
          if (sdfs_is_mounted()) {
            strcpy(stack[++top], "/");
          }
          while (top >= 0) {
            char dir[200];
            strcpy(dir, stack[top--]);
            int rooty = (dir[0] == '/' && dir[1] == '\0');
            int idx = 0;
            while (1) {
              char name[64];
              int is_dir;
              uint32_t size;
              if (sdfs_list_dir_entry(dir, idx++, name, &is_dir, &size) != 0)
                break;
              const char *dot = NULL;
              for (const char *c = name; *c; c++)
                if (*c == '.') dot = c;
              int is_img = (dot && (strcmp(dot, ".png") == 0 ||
                                    strcmp(dot, ".jpg") == 0 ||
                                    strcmp(dot, ".jpeg") == 0 ||
                                    strcmp(dot, ".bmp") == 0 ||
                                    strcmp(dot, ".gif") == 0 ||
                                    strcmp(dot, ".tga") == 0 ||
                                    strcmp(dot, ".psd") == 0));
              char child[200];
              if (rooty) { strcpy(child, "/"); strncat(child, name, 198); }
              else       { strcpy(child, dir); strncat(child, "/", 199);
                           strncat(child, name, 199 - strlen(child)); }
              if (is_dir) {
                if (top + 1 < 64) { strcpy(stack[++top], child); }
              } else if (is_img) {
                serial_print("IMGSCAN: "); serial_print(child); serial_print("\n");
                if (strstr(child, "nested.png") != NULL)
                  found_nested = 1;
              }
            }
          }
          if (found_nested)
            serial_print("IMG_NESTED_OK\n");
          else
            serial_print("IMG_NESTED_MISSING\n");
        }
      }
    }

    /* ---- TEST MODE: run test suites, skip normal boot ---- */
    extern void test_runner_task(void);
    extern volatile int test_runner_done;
    extern int launch_initrd_program(const char *filename);
    extern int sys_waitpid(int pid, int *status, int options);

    /* If a userspace test_runner ELF is present in the initrd, launch it
       as a Ring 3 process and wait for it to finish. Otherwise run the
       kernel-side SDFS tests. */
    uint32_t ur_size = 0;
    if (initrd_get_file("test_runner", &ur_size)) {
      serial_print("[boot] Launching userspace test_runner\n");
      int pid = launch_initrd_program("/test_runner");
      if (pid > 0) {
        /* Wait for the user test process to exit (reap zombie). */
        sys_waitpid(pid, NULL, 0);
      } else {
        serial_print("[boot] ERROR: could not launch /test_runner\n");
      }
    } else {
      create_task_named(test_runner_task, "test_runner");
      /* Wait for tests to finish */
      while (!test_runner_done) {
        switch_task();
      }
    }

    serial_print("LIWUS_BOOT_READY\n");
    serial_print("[boot] Tests complete. Halting.\n");
    asm volatile("sti");
    while (1) {
      asm volatile("hlt");
    }
  }

  /* Sync music MP3s from the initrd to SDFS + seed the media song list. */
  mp3_init();

  /* Apply saved Sound settings (volume/rate) from SDFS. */
  extern void sound_config_apply(void);
  sound_config_apply();

  extern void usb_start_polling(void);
  usb_start_polling();

  extern void gui_init(void);
  extern void gui_compositor_task(void);
  
  boot_splash_set_progress(85, "Iniciando interface grafica...");
  gui_init();
  create_task_named(gui_compositor_task, "gui");
  create_task_named(audio_boot_chime_task, "audioboot");
  create_task_named(media_task, "media");

  /* Virtual pendrive: probe the SCSI bus and start the hot-plug watcher. */
  extern void pen_init(void);
  extern void pen_task(void);
  pen_init();
  create_task_named(pen_task, "pen");
  extern void terminal_task();
  create_task_named(terminal_task, "terminal");

  boot_splash_set_progress(100, "Pronto!");
  boot_splash_done();

  /* Stable serial marker consumed by the headless regression suite. */
  serial_print("LIWUS_BOOT_READY\n");
  serial_print("sti...\n");
  asm volatile("sti");
  while (1) {
    asm volatile("hlt");
  }
}
