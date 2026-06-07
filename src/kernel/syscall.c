#include "syscall.h"
#include "sdfs.h"
#include "initrd.h"
#include "io.h"
#include "kheap.h"
#include "keyboard.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "tcp.h"
#include "timer.h"
#include "video.h"
#include "vmm.h"
#include "vfs.h"

extern uint32_t elf_load_file(void *elf_data);
extern int graphics_exclusive_active(void);
extern void graphics_exclusive_acquire(int pid);
extern void graphics_exclusive_release(int pid);
extern task_t *current_task;

static char *launch_last_error = "nenhum erro";
const char *get_launch_last_error() { return launch_last_error; }

void sys_exit(int status) {
  (void)status;
  sys_exit_process(0);
}

int sys_execve(const char *filename, char *const argv[], char *const envp[]) {
  (void)envp;
  const uint32_t max_args = 16;
  uint32_t size = 0;
  void *addr = NULL;

  /*
   * Ordem de busca:
   *   1. SDFS (disco persistente) — montado em /house/localhost
   *      Caminho: "/" + filename (sys_execve já espera paths absolutos
   *      sem o "/" inicial, então adicionamos um "/" na frente)
   *   2. initrd (RAM) — montado na raiz do VFS, contém os system files
   *      originais do tar que foi gerado na build
   *
   * Se SDFS não estiver montado (LIVECD mode), sdfs_read_file retorna
   * NULL porque sdfs_mounted == 0, caindo no fallback do initrd.
   */
  char sdfs_path[256];
  sdfs_path[0] = '/';
  strncpy(sdfs_path + 1, filename, 254);
  sdfs_path[255] = '\0';
  addr = sdfs_read_file(sdfs_path, &size);

  if (!addr) {
    size = 0;
    addr = initrd_get_file(filename, &size);
  }

  if (!addr) {
    launch_last_error = "arquivo nao encontrado";
    return -1;
  }

  uint32_t entry = elf_load_file(addr);
  if (entry == 0) {
    launch_last_error = "formato ELF invalido";
    return -1;
  }

  uint32_t stack_size = 64 * 1024;
  uint32_t stack_top = 0xC0000000;
  uint32_t stack_base = stack_top - stack_size;
  for (uint32_t vaddr = stack_base; vaddr < stack_top; vaddr += 4096) {
    void *phys = kmalloc_a(4096);
    vmm_map_page(phys, (void *)vaddr, 0x07);
    memset((void *)vaddr, 0, 4096);
  }

  uint8_t *esp_bytes = (uint8_t *)stack_top;

  int argc = 0;
  if (argv) {
    while (argv[argc] && argc < (int)max_args) argc++;
  }

  uint32_t arg_ptrs[16];
  for (int i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    esp_bytes -= len;
    memcpy(esp_bytes, argv[i], len);
    arg_ptrs[i] = (uint32_t)esp_bytes;
  }

  uint32_t *esp = (uint32_t *)((uint32_t)esp_bytes & ~0xF);

  esp--; *esp = 0;
  for (int i = argc - 1; i >= 0; i--) {
    esp--; *esp = arg_ptrs[i];
  }
  uint32_t argv_ptr_addr = (uint32_t)esp;

  esp--; *esp = 0;
  esp--; *esp = argv_ptr_addr;
  esp--; *esp = (uint32_t)argc;

  serial_print("EXEC: "); serial_print(filename);
  serial_print(" argc="); char nbuf[12]; itoa(argc, nbuf, 10); serial_print(nbuf);
  serial_print("\n");

  extern uint32_t execve_new_esp;
  execve_new_esp = (uint32_t)esp;
  launch_last_error = "ok";
  return entry;
}

uint32_t execve_new_esp = 0;

int launch_initrd_program(const char *filename) {
  return launch_initrd_program_argv(filename, NULL);
}

int launch_initrd_program_argv(const char *filename, char *const argv[]) {
  int ret = sys_execve(filename, argv, NULL);
  if (ret == -1) return -1;
  if (execve_new_esp == 0) return -1;
  int pid = create_user_task_named((uint32_t)ret, execve_new_esp, filename);
  execve_new_esp = 0;
  
  serial_print("launch: returning pid=");
  char nbuf[12];
  itoa(pid, nbuf, 10);
  serial_print(nbuf);
  serial_print("\n");
  
  return pid;
}

#define FD_TYPE_FREE 0
#define FD_TYPE_FILE 1
#define FD_TYPE_SOCKET 2

typedef struct {
  int type;
  uint32_t base_addr;
  uint32_t size;
  uint32_t offset;
  int owned_buffer;
  void *socket_ptr;
} kfile_t;

static kfile_t fd_table[32];

int sys_open(const char *filename, int flags) {
  (void)flags;
  int fd = -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      fd = i;
      break;
    }
  }
  if (fd == -1) return -1;

  fs_node_t *node = vfs_open(filename);
  if (!node) return -1;

  fd_table[fd].type = FD_TYPE_FILE;
  fd_table[fd].socket_ptr = (void *)node;
  fd_table[fd].offset = 0;
  fd_table[fd].size = node->length;
  fd_table[fd].base_addr = 0;
  fd_table[fd].owned_buffer = 0;
  return fd;
}

int sys_read(int fd, void *buf, uint32_t count) {
  if (fd == 0) {
    if (count == 0) return 0;
    char c;
    while (!keyboard_pop_char(&c)) {
        switch_task();
    }
    ((char *)buf)[0] = c;
    return 1;
  }
  if (fd < 0 || fd >= 32 || fd_table[fd].type != FD_TYPE_FILE) return -1;
  kfile_t *f = &fd_table[fd];
  if (f->socket_ptr) {
    uint32_t r = read_fs((fs_node_t *)f->socket_ptr, f->offset, count, (uint8_t *)buf);
    f->offset += r;
    return (int)r;
  }
  uint32_t available = f->size - f->offset;
  uint32_t to_read = count < available ? count : available;
  memcpy(buf, (void *)(f->base_addr + f->offset), to_read);
  f->offset += to_read;
  return (int)to_read;
}

#include "terminal.h"

int sys_write(int fd, const void *buf, uint32_t count) {
  if (fd == 1 || fd == 2) {
    terminal_append_output_n((const char*)buf, count);
    return count;
  }
  if (fd < 0 || fd >= 32) return -1;
  if (fd_table[fd].type == FD_TYPE_FILE && fd_table[fd].socket_ptr) {
    uint32_t w = write_fs((fs_node_t *)fd_table[fd].socket_ptr, fd_table[fd].offset, count, (uint8_t *)buf);
    fd_table[fd].offset += w;
    return (int)w;
  }
  return -1;
}

int sys_close(int fd) {
  if (fd < 0 || fd >= 32) return -1;
  if (fd_table[fd].type == FD_TYPE_SOCKET) tcp_close((tcp_socket_t *)fd_table[fd].socket_ptr);
  fd_table[fd].type = FD_TYPE_FREE;
  return 0;
}

int sys_lseek(int fd, int offset, int whence) {
  if (fd < 0 || fd >= 32 || fd_table[fd].type != FD_TYPE_FILE) return -1;
  kfile_t *f = &fd_table[fd];
  int new_offset;

  if (whence == 0) {
    new_offset = offset;
  } else if (whence == 1) {
    new_offset = (int)f->offset + offset;
  } else if (whence == 2) {
    new_offset = (int)f->size + offset;
  } else {
    return -1;
  }

  if (new_offset < 0) return -1;
  if ((uint32_t)new_offset > f->size) new_offset = (int)f->size;
  f->offset = (uint32_t)new_offset;
  return fd_table[fd].offset;
}

static int sys_present_frame(const uint32_t *src, uint32_t src_w, uint32_t src_h,
                             int dst_x, int dst_y) {
  uint32_t scale_x;
  uint32_t scale_y;
  uint32_t scale;
  uint32_t out_w;
  uint32_t out_h;

  if (!framebuffer || !src || src_w == 0 || src_h == 0 ||
      screen_width == 0 || screen_height == 0) {
    return -1;
  }

  scale_x = screen_width / src_w;
  scale_y = screen_height / src_h;
  scale = scale_x < scale_y ? scale_x : scale_y;
  if (scale == 0) scale = 1;

  out_w = src_w * scale;
  out_h = src_h * scale;
  if (dst_x < 0) dst_x = (int)(screen_width - out_w) / 2;
  if (dst_y < 0) dst_y = (int)(screen_height - out_h) / 2;

  memset32(framebuffer, 0, screen_width * screen_height);

  for (uint32_t y = 0; y < out_h; y++) {
    int py = dst_y + (int)y;
    if (py < 0 || (uint32_t)py >= screen_height) continue;

    uint32_t sy = y / scale;
    for (uint32_t x = 0; x < out_w; x++) {
      int px = dst_x + (int)x;
      if (px < 0 || (uint32_t)px >= screen_width) continue;

      uint32_t sx = x / scale;
      framebuffer[(uint32_t)py * screen_width + (uint32_t)px] =
          src[sy * src_w + sx];
    }
  }

  return 0;
}



extern int sys_waitpid(int pid, int *status, int options);

void syscall_handler(registers_t *regs) {
  uint32_t call_num = regs->eax;
  switch (call_num) {
    case 1: sys_exit(regs->ebx); break;
    case 2: regs->eax = sys_brk(regs->ebx); break;
    case 3: regs->eax = sys_read(regs->ebx, (void *)regs->ecx, regs->edx); break;
    case 4: regs->eax = sys_write(regs->ebx, (void *)regs->ecx, regs->edx); break;
    case 5: regs->eax = sys_open((char *)regs->ebx, regs->ecx); break;
    case 6: regs->eax = sys_close(regs->ebx); break;
    case 7: regs->eax = sys_waitpid((int)regs->ebx, (int *)regs->ecx, (int)regs->edx); break;
    case 8: regs->eax = timer_ticks; break;
    case 10: regs->eax = keyboard_get_event((void *)regs->ebx); break;
    case 11: regs->eax = keyboard_is_pressed((uint8_t)regs->ebx); break;
    case 12: {
        uint32_t *info = (uint32_t *)regs->ebx;
        if (!info || !framebuffer || screen_width == 0 || screen_height == 0) {
          regs->eax = (uint32_t)-1;
          break;
        }
        if (current_task && current_task->user_mode) {
          graphics_exclusive_acquire(current_task->id);
        }
        info[0] = (uint32_t)framebuffer;
        info[1] = screen_width;
        info[2] = screen_height;
        info[3] = screen_width * 4;
        info[4] = 32;
        regs->eax = 0;
        break;
    }
    case 13:
      if (regs->ebx) {
        if (current_task && current_task->user_mode) {
          graphics_exclusive_acquire(current_task->id);
        }
        regs->eax = sys_present_frame((const uint32_t *)regs->ebx, regs->ecx,
                                      regs->edx, (int)regs->esi,
                                      (int)regs->edi);
      } else {
        refresh_screen();
        regs->eax = 0;
      }
      break;
    case 19: regs->eax = sys_lseek(regs->ebx, regs->ecx, regs->edx); break;
    case 20: {
        const char *name = (const char *)regs->ebx;
        const void *buffer = (const void *)regs->ecx;
        uint32_t size = regs->edx;
        if (sdfs_is_mounted() && name && buffer) {
            uint32_t w = sdfs_write_file(name, (uint8_t *)buffer, size);
            regs->eax = (int)w;
        } else {
            regs->eax = -1;
        }
        break;
    }
    case 45: regs->eax = sys_brk(regs->ebx); break;
    default: break;
  }
}

void init_syscalls() {
  memset(fd_table, 0, sizeof(fd_table));
}
