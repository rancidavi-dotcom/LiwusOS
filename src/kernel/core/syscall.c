#include "syscall.h"
#include "elf.h"
#include "sdfs.h"
#include "initrd.h"
#include "io.h"
#include "kheap.h"
#include "keyboard.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "timer.h"
#include "vga.h"
#include "vmm.h"
#include "vfs.h"
#include "termios.h"
#include "pmm.h"

extern int elf_class(void *file_buffer);
extern uint64_t elf32_load_file(void *file_buffer);
extern uint64_t elf64_load_file(void *file_buffer);

static char *launch_last_error = "nenhum erro";
const char *get_launch_last_error() { return launch_last_error; }

struct termios kernel_termios = {
    .c_iflag = BRKINT | ICRNL | IXON,
    .c_oflag = OPOST,
    .c_cflag = CS8,
    .c_lflag = ECHO | ICANON | ISIG | IEXTEN,
    .c_cc = {0},
};

// Internal buffer for ANSI escape sequence delivery from raw-mode reads
static char stdin_esc_buf[8];
static int stdin_esc_len = 0;
static int stdin_esc_pos = 0;

void sys_exit(int status) {
  serial_print("[syscall] sys_exit called with status ");
  char sbuf[16];
  itoa(status, sbuf, 10);
  serial_print(sbuf);
  serial_print("\n");
  sys_exit_process(status);
}

int sys_execve_is_64 = 0;

uint64_t sys_execve(const char *filename, char *const argv[], char *const envp[]) {
  (void)envp;
  const uint32_t max_args = 16;
  uint32_t size = 0;
  void *addr = NULL;

  char sdfs_path[256];
  if (filename[0] == '/') {
    strncpy(sdfs_path, filename, 255);
  } else {
    sdfs_path[0] = '/';
    strncpy(sdfs_path + 1, filename, 254);
  }
  sdfs_path[255] = '\0';
  addr = sdfs_read_file(sdfs_path, &size);

  if (!addr) {
    size = 0;
    addr = initrd_get_file(filename, &size);
  }

  if (!addr) {
    launch_last_error = "arquivo nao encontrado";
    return (uint64_t)-1;
  }

  int eclass = elf_class(addr);
  if (eclass != ELFCLASS32 && eclass != ELFCLASS64) {
    launch_last_error = "ELF class invalido";
    return (uint64_t)-1;
  }

  uint64_t entry;
  if (eclass == ELFCLASS32) {
    entry = elf32_load_file(addr);
    sys_execve_is_64 = 0;
  } else {
    entry = elf64_load_file(addr);
    sys_execve_is_64 = 1;
  }
  if (entry == 0) {
    launch_last_error = "formato ELF invalido";
    return (uint64_t)-1;
  }

  uint64_t stack_size = 64 * 1024;
  uint64_t stack_top = 0xC0000000;
  uint64_t stack_base = stack_top - stack_size;
  for (uint64_t vaddr = stack_base; vaddr < stack_top; vaddr += 4096) {
    void *phys = pmm_alloc_block();
    vmm_map_page(phys, (void *)vaddr, 0x07);
    memset((void *)vaddr, 0, 4096);
  }

  uint8_t *stack_bytes = (uint8_t *)stack_top;

  int argc = 0;
  if (argv) {
    while (argv[argc] && argc < (int)max_args) argc++;
  }

  if (eclass == ELFCLASS32) {
    uint32_t arg_ptrs[16];
    for (int i = 0; i < argc; i++) {
      size_t len = strlen(argv[i]) + 1;
      stack_bytes -= len;
      memcpy(stack_bytes, argv[i], len);
      arg_ptrs[i] = (uint32_t)(uint64_t)stack_bytes;
    }

    uint32_t *esp = (uint32_t *)((uint64_t)stack_bytes & ~0xFULL);
    esp--; *esp = 0; // envp NULL
    esp--; *esp = 0; // argv NULL
    for (int i = argc - 1; i >= 0; i--) {
      esp--; *esp = arg_ptrs[i];
    }
    esp--; *esp = (uint32_t)argc;

    extern uint64_t execve_new_esp;
    execve_new_esp = (uint64_t)esp;
  } else {
    uint64_t arg_ptrs[16];
    for (int i = 0; i < argc; i++) {
      size_t len = strlen(argv[i]) + 1;
      stack_bytes -= len;
      memcpy(stack_bytes, argv[i], len);
      arg_ptrs[i] = (uint64_t)stack_bytes;
    }

    uint64_t *rsp = (uint64_t *)((uint64_t)stack_bytes & ~0xFULL);
    rsp--; *rsp = 0; // auxv NULL value
    rsp--; *rsp = 0; // auxv NULL type
    rsp--; *rsp = 0; // envp NULL
    rsp--; *rsp = 0; // argv NULL
    for (int i = argc - 1; i >= 0; i--) {
      rsp--; *rsp = arg_ptrs[i];
    }
    rsp--; *rsp = (uint64_t)argc;

    extern uint64_t execve_new_esp;
    execve_new_esp = (uint64_t)rsp;
  }

  serial_print("EXEC: "); serial_print(filename);
  serial_print(" argc="); char nbuf[12]; itoa(argc, nbuf, 10); serial_print(nbuf);
  serial_print(" class="); serial_print(eclass == ELFCLASS32 ? "32" : "64");
  serial_print("\n");

  launch_last_error = "ok";
  return entry;
}

uint64_t execve_new_esp = 0;

int launch_initrd_program(const char *filename) {
  return launch_initrd_program_argv(filename, NULL);
}

// ============================================================
// Path resolution helpers (CWD-aware)
// ============================================================

/*
 * Resolve a path that may be relative to the current task's CWD.
 * If the path starts with '/', it's absolute. Otherwise, prepend CWD.
 * Returns the resolved path in a static buffer (caller should copy).
 */
static const char *resolve_path(const char *path) {
  static char resolved[512];
  if (!path) return NULL;
  if (path[0] == '/') {
    strncpy(resolved, path, 511);
    resolved[511] = '\0';
    return resolved;
  }
  // Relative path — prepend CWD
  if (current_task && current_task->cwd[0]) {
    int cwd_len = strlen(current_task->cwd);
    memcpy(resolved, current_task->cwd, cwd_len);
    // Ensure trailing slash
    if (resolved[cwd_len - 1] != '/') {
      resolved[cwd_len] = '/';
      cwd_len++;
    }
    strncpy(resolved + cwd_len, path, 511 - cwd_len);
    resolved[511] = '\0';
  } else {
    strncpy(resolved, path, 511);
    resolved[511] = '\0';
  }
  return resolved;
}

extern int sys_execve_is_64;

extern page_directory_t *current_directory;
extern page_directory_t *vmm_create_directory(void);
extern void switch_page_directory(page_directory_t *dir);

int launch_initrd_program_argv(const char *filename, char *const argv[]) {
  // Isolate memory space!
  page_directory_t *old_dir = current_directory;
  page_directory_t *new_dir = vmm_create_directory();
  
  // Temporarily update current_task's directory so the timer interrupt
  // doesn't revert current_directory while sys_execve maps memory!
  page_directory_t *task_old_dir = current_task ? current_task->page_directory : old_dir;
  if (current_task) current_task->page_directory = new_dir;
  
  switch_page_directory(new_dir);
  serial_print("[kernel] launch_initrd_program_argv calling sys_execve\n");
  uint64_t ret = sys_execve(filename, argv, NULL);
  if (ret == (uint64_t)-1) {
    if (current_task) current_task->page_directory = task_old_dir;
    switch_page_directory(old_dir);
    return -1;
  }
  if (execve_new_esp == 0) {
    if (current_task) current_task->page_directory = task_old_dir;
    switch_page_directory(old_dir);
    return -1;
  }
  
  int pid;
  if (sys_execve_is_64)
    pid = create_user_task_64_named((uint64_t)ret, execve_new_esp, filename);
  else
    pid = create_user_task_named((uint64_t)ret, execve_new_esp, filename);
  
  sys_execve_is_64 = 0;
  execve_new_esp = 0;
  last_foreground_pid = pid;
  serial_print("[kernel] launch_initrd_program_argv created pid=");
  char pidbuf[16];
  itoa(pid, pidbuf, 10);
  serial_print(pidbuf);
  serial_print("\n");
  
  // Set the correct directory for the newly spawned task
  extern task_t *task_list;
  task_t *t = task_list->next;
  while (t && t->id != pid && t != task_list) { t = t->next; }
  if (t && t->id == pid) {
    t->page_directory = new_dir;
  }

  // Debug page tables for the entry point (ret)
  {
    uint64_t v = ret;
    pml4_t *pml4 = new_dir->pml4_virt;
    serial_print("VMM DEBUG: mapping for entry point ");
    serial_print_hex(v);
    serial_print("\n");
    serial_print("  PML4[0]: "); serial_print_hex(pml4->entries[0]); serial_print("\n");
    if (pml4->entries[0] & 1) {
      pdp_t *pdp = (pdp_t *)(pml4->entries[0] & ~0xfff);
      serial_print("  PDP[0]: "); serial_print_hex(pdp->entries[0]); serial_print("\n");
      if (pdp->entries[0] & 1) {
        pd_t *pd = (pd_t *)(pdp->entries[0] & ~0xfff);
        uint64_t pd_i = (v >> 21) & 0x1ff;
        serial_print("  PD[");
        char pbuf[10];
        itoa(pd_i, pbuf, 10);
        serial_print(pbuf);
        serial_print("]: ");
        serial_print_hex(pd->entries[pd_i]);
        serial_print("\n");
        if (pd->entries[pd_i] & 1) {
          if (pd->entries[pd_i] & 0x80) {
            serial_print("  (2MB Page mapping)\n");
          } else {
            pt_t *pt = (pt_t *)(pd->entries[pd_i] & ~0xfff);
            uint64_t pt_i = (v >> 12) & 0x1ff;
            serial_print("  PT[");
            itoa(pt_i, pbuf, 10);
            serial_print(pbuf);
            serial_print("]: ");
            serial_print_hex(pt->entries[pt_i]);
            serial_print("\n");
          }
        }
      }
    }
  }

  // Restore the old directory for the caller (compositor)
  if (current_task) current_task->page_directory = task_old_dir;
  switch_page_directory(old_dir);
  
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
#define FD_TYPE_PIPE 3

#define fd_table (current_task->file_descriptors)

static bool validate_user_range(const void *ptr, size_t size) {
  if (!ptr) return false;
  uint64_t addr = (uint64_t)ptr;
  if (addr >= 0xC0000000 || (addr + size) > 0xC0000000 || (addr + size) < addr) {
    return false;
  }
  return true;
}

static bool validate_user_string(const char *str) {
  if (!str) return false;
  uint64_t addr = (uint64_t)str;
  if (addr >= 0xC0000000) return false;
  while (addr < 0xC0000000) {
    if (*(const char *)addr == '\0') return true;
    addr++;
  }
  return false;
}

#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define O_APPEND 0x0008

int sys_open(const char *filename, int flags) {
  if (current_task && current_task->user_mode) {
    if (!validate_user_string(filename)) return -1;
  }
  int fd = -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      fd = i;
      break;
    }
  }
  if (fd == -1) return -1;

  const char *resolved = resolve_path(filename);
  if (!resolved) return -1;

  // Try VFS
  fs_node_t *node = vfs_open(resolved);
  
  if (!node && (flags & O_CREAT)) {
      node = vfs_create(resolved, flags);
  }

  if (node) {
    serial_print("[syscall] sys_open success: ");
    serial_print(filename);
    serial_print(" fd=");
    char fdbuf[16];
    itoa(fd, fdbuf, 10);
    serial_print(fdbuf);
    serial_print("\n");
    if (flags & O_TRUNC) {
      vfs_truncate(node);
    }
    fd_table[fd].type = FD_TYPE_FILE;
    fd_table[fd].socket_ptr = (void *)node;
    fd_table[fd].offset = (flags & O_APPEND) ? node->length : 0;
    fd_table[fd].size = node->length;
    fd_table[fd].base_addr = 0;
    fd_table[fd].owned_buffer = 0;
    return fd;
  }

  /* Fallback: try initrd */
  {
    uint32_t initrd_size = 0;
    const char *initrd_path = resolved;
    if (initrd_path && initrd_path[0] == '/')
      initrd_path++;
    void *initrd_data = initrd_get_file(initrd_path, &initrd_size);
    if (initrd_data && initrd_size > 0) {
      fd_table[fd].type = FD_TYPE_FILE;
      fd_table[fd].socket_ptr = NULL;
      fd_table[fd].offset = 0;
      fd_table[fd].size = initrd_size;
      fd_table[fd].base_addr = (uint64_t)initrd_data;
      fd_table[fd].owned_buffer = 0;
      serial_print("[syscall] sys_open initrd fallback: ");
      serial_print(filename);
      serial_print("\n");
      return fd;
    }
  }

  serial_print("[syscall] sys_open failed: ");
  serial_print(filename);
  serial_print("\n");
  return -1;
}

int sys_read(int fd, void *buf, uint32_t count) {
  if (current_task && current_task->user_mode) {
    if (!validate_user_range(buf, count)) return -1;
  }
  if (fd >= 0 && fd < 32 && fd_table[fd].type == FD_TYPE_PIPE) {
    pipe_t *p = (pipe_t *)fd_table[fd].socket_ptr;
    if (!p) return -1;
    while (p->bytes_avail == 0) {
      if (p->refcount <= 0) return 0; // EOF
      switch_task();
    }
    uint32_t to_read = count < p->bytes_avail ? count : p->bytes_avail;
    for (uint32_t i = 0; i < to_read; i++) {
      ((char *)buf)[i] = p->buffer[p->read_pos];
      p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
    }
    p->bytes_avail -= to_read;
    return (int)to_read;
  }
  if (fd == 0) {
    serial_print("[syscall] sys_read called from stdin\n");
    if (count == 0) return 0;

    // Drain any buffered escape sequence bytes first
    if (stdin_esc_pos < stdin_esc_len) {
        ((char *)buf)[0] = stdin_esc_buf[stdin_esc_pos++];
        if (stdin_esc_pos >= stdin_esc_len) {
            stdin_esc_len = 0;
            stdin_esc_pos = 0;
        }
        return 1;
    }

    char c;
    int icanon = kernel_termios.c_lflag & ICANON;
    cc_t vmin = kernel_termios.c_cc[VMIN];
    cc_t vtime = kernel_termios.c_cc[VTIME];

    if (!icanon && vmin == 0 && vtime == 0) {
        if (!keyboard_pop_char(&c) && !serial_pop_char(&c)) return 0;
    } else if (!icanon && vmin == 0 && vtime > 0) {
        // Non-blocking read with timeout (for ESC sequence detection)
        int timeout_ticks = vtime * 10; // vtime in deciseconds, 100 Hz timer
        int start = (int)timer_ticks;
        while (!keyboard_pop_char(&c) && !serial_pop_char(&c)) {
            if ((int)(timer_ticks - start) >= timeout_ticks) {
                return 0; // timeout
            }
            switch_task();
        }
    } else {
        while (!keyboard_pop_char(&c) && !serial_pop_char(&c)) {
            switch_task();
        }
    }

    // Convert Carriage Return (from serial) to Newline if ICRNL is set
    if ((kernel_termios.c_iflag & ICRNL) && c == '\r') {
        c = '\n';
    }

    if (icanon) {
        // ISIG: Ctrl+C kills the current process
        if ((kernel_termios.c_lflag & ISIG) && c == 3) {
            int my_pid = current_task ? current_task->id : -1;
            if (my_pid > 0) {
                sys_kill_by_pid(my_pid);
                switch_task();
            }
            return -1;
        }
        // Cooked mode: echo always
        vga_putc(c);
        write_serial(c);
        ((char *)buf)[0] = c;
        return 1;
    }

    // Raw mode: translate special keys to ANSI escape sequences
    if ((kernel_termios.c_lflag & ISIG) && (unsigned char)c == 3) {
        // SIGINT via Ctrl+C
        int my_pid = current_task ? current_task->id : -1;
        if (my_pid > 0) {
            sys_kill_by_pid(my_pid);
            switch_task();
        }
        return -1;
    }
    if (kernel_termios.c_lflag & ECHO) {
        vga_putc(c);
        write_serial(c);
    }

    const char *esc_seq = NULL;
    int esc_len = 0;
    if ((unsigned char)c == 128) { esc_seq = "\x1b[A"; esc_len = 3; }   // UP
    else if ((unsigned char)c == 129) { esc_seq = "\x1b[B"; esc_len = 3; } // DOWN
    else if ((unsigned char)c == 130) { esc_seq = "\x1b[D"; esc_len = 3; } // LEFT
    else if ((unsigned char)c == 131) { esc_seq = "\x1b[C"; esc_len = 3; } // RIGHT

    if (esc_seq) {
        // Buffer the escape sequence and return first byte
        memcpy(stdin_esc_buf, esc_seq, (size_t)esc_len);
        stdin_esc_len = esc_len;
        stdin_esc_pos = 1;
        ((char *)buf)[0] = stdin_esc_buf[0];
        return 1;
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

extern void write_serial(char a);

int sys_write(int fd, const void *buf, uint32_t count) {
  if (current_task && current_task->user_mode) {
    if (!validate_user_range(buf, count)) return -1;
  }
  if (fd == 1 || fd == 2) {
    for (uint32_t i = 0; i < count; i++) {
      char c = ((const char*)buf)[i];
      vga_putc(c);
      write_serial(c);
    }
    return count;
  }
  if (fd < 0 || fd >= 32) return -1;
  kfile_t *f = &fd_table[fd];
  if (f->type == FD_TYPE_PIPE) {
    pipe_t *p = (pipe_t *)f->socket_ptr;
    if (!p) return -1;
    uint32_t space = PIPE_BUF_SIZE - p->bytes_avail;
    if (space == 0) return 0;
    if (count > space) count = space;
    for (uint32_t i = 0; i < count; i++) {
      p->buffer[p->write_pos] = ((const char *)buf)[i];
      p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
    }
    p->bytes_avail += count;
    return (int)count;
  }
  if (f->type == FD_TYPE_FILE && f->socket_ptr) {
    uint32_t w = write_fs((fs_node_t *)f->socket_ptr, f->offset, count, (uint8_t *)buf);
    f->offset += w;
    fs_node_t *node = (fs_node_t *)f->socket_ptr;
    f->size = node->length;
    return (int)w;
  }
  return -1;
}

int sys_close(int fd) {
  if (fd < 0 || fd >= 32) return -1;
  kfile_t *f = &fd_table[fd];

  if (f->type == FD_TYPE_PIPE) {
    pipe_t *p = (pipe_t *)f->socket_ptr;
    if (p) {
      p->refcount--;
      if (p->refcount <= 0) kfree(p);
    }
  }
  f->type = FD_TYPE_FREE;
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

// syscalls 12 e 13 foram removidos (framebuffer GUI)

// Termios syscalls
int sys_tcgetattr(int fd, struct termios *t) {
  (void)fd;
  if (!t) return -1;
  memcpy(t, &kernel_termios, sizeof(struct termios));
  return 0;
}

int sys_tcsetattr(int fd, int action, const struct termios *t) {
  (void)fd;
  (void)action;
  if (!t) return -1;
  memcpy(&kernel_termios, t, sizeof(struct termios));
  return 0;
}

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TIOCGWINSZ 0x5413

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int sys_ioctl(int fd, unsigned long request, void *argp) {
  if (request == TCGETS) {
    return sys_tcgetattr(fd, (struct termios *)argp);
  }
  if (request == TCSETS || request == TCSETSW || request == TCSETSF) {
    return sys_tcsetattr(fd, 0, (const struct termios *)argp);
  }
  if (request == TIOCGWINSZ && argp) {
    struct winsize *ws = (struct winsize *)argp;
    ws->ws_row = 25;
    ws->ws_col = 80;
    ws->ws_xpixel = 0;
    ws->ws_ypixel = 0;
    return 0;
  }
  return -1;
}

extern int sys_waitpid(int pid, int *status, int options);

static int do_execve(registers_t *regs, const char *filename, char *const argv[], char *const envp[]) {
  (void)envp;
  uint32_t size = 0;
  void *addr = NULL;
  char fname_buf[256];

  if (!filename) return -1;
  strncpy(fname_buf, filename, 255);
  fname_buf[255] = '\0';

  char sdfs_path[256];
  if (fname_buf[0] == '/') {
    strncpy(sdfs_path, fname_buf, 255);
  } else {
    sdfs_path[0] = '/';
    strncpy(sdfs_path + 1, fname_buf, 254);
  }
  sdfs_path[255] = '\0';
  addr = sdfs_read_file(sdfs_path, &size);
  if (!addr) {
    size = 0;
    addr = initrd_get_file(fname_buf, &size);
  }
  if (!addr) return -1;

  int eclass = elf_class(addr);
  if (eclass != ELFCLASS32 && eclass != ELFCLASS64) return -1;

  uint64_t entry;
  if (eclass == ELFCLASS32)
    entry = elf32_load_file(addr);
  else
    entry = elf64_load_file(addr);
  if (entry == 0) return -1;

  uint64_t stack_size = 64 * 1024;
  uint64_t stack_top = 0xC0000000;
  uint64_t stack_base = stack_top - stack_size;
  for (uint64_t vaddr = stack_base; vaddr < stack_top; vaddr += 4096) {
    void *phys = pmm_alloc_block();
    vmm_map_page(phys, (void *)vaddr, 0x07);
    memset((void *)vaddr, 0, 4096);
  }

  uint8_t *stack_bytes = (uint8_t *)stack_top;
  int argc = 0;
  const uint32_t max_args = 16;
  if (argv) {
    while (argv[argc] && argc < (int)max_args) argc++;
  }

  if (eclass == ELFCLASS32) {
    uint32_t arg_ptrs[16];
    for (int i = 0; i < argc; i++) {
      size_t len = strlen(argv[i]) + 1;
      stack_bytes -= len;
      memcpy(stack_bytes, argv[i], len);
      arg_ptrs[i] = (uint32_t)(uint64_t)stack_bytes;
    }
    uint32_t *esp = (uint32_t *)((uint64_t)stack_bytes & ~0xFULL);
    esp--; *esp = 0; // envp NULL
    esp--; *esp = 0; // argv NULL
    for (int i = argc - 1; i >= 0; i--) {
      esp--; *esp = arg_ptrs[i];
    }
    esp--; *esp = (uint32_t)argc;
    regs->rsp = (uint64_t)esp;
  } else {
    uint64_t arg_ptrs[16];
    for (int i = 0; i < argc; i++) {
      size_t len = strlen(argv[i]) + 1;
      stack_bytes -= len;
      memcpy(stack_bytes, argv[i], len);
      arg_ptrs[i] = (uint64_t)stack_bytes;
    }
    uint64_t *rsp = (uint64_t *)((uint64_t)stack_bytes & ~0xFULL);
    rsp--; *rsp = 0; // auxv NULL
    rsp--; *rsp = 0; // auxv NULL
    rsp--; *rsp = 0; // envp NULL
    rsp--; *rsp = 0; // argv NULL
    for (int i = argc - 1; i >= 0; i--) {
      rsp--; *rsp = arg_ptrs[i];
    }
    rsp--; *rsp = (uint64_t)argc;
    regs->rsp = (uint64_t)rsp;
  }

  regs->rip = entry;
  return 0;
}

#include "../gui/scene/node.h"
#include "../gui/widgets/window_node.h"
#include "../gui/widgets/button.h"
#include "../gui/widgets/label.h"
#include "../gui/widgets/panel.h"
#include "../gui/widgets/image_node.h"
#include "../gui/render/compositor.h"

extern compositor_t *g_compositor;

// 120: canvas_create() -> creates a window node for the app
uint32_t sys_gui_canvas_create(int width, int height, const char* title) {
    if (!g_compositor || !g_compositor->scene_root) return 0;
    
    int win_x = (g_compositor->renderer->screen_w - width) / 2;
    int win_y = (g_compositor->renderer->screen_h - height) / 2;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    
    node_t *win = window_node_create("app_win", win_x, win_y, width, height, title ? title : "App");
    if (!win) return 0;
    
    // Bind to current process
    if (current_task) {
        window_node_set_pid(win, current_task->id);
    }
    
    // Default layout for apps
    win->layout_type = LAYOUT_ABSOLUTE;
    win->padding[0] = 30; // Title bar space
    
    node_add_child(g_compositor->scene_root, win);
    return win->id;
}

// 121: node_create() -> creates a generic node (label, button)
uint32_t sys_gui_node_create(node_type_t type, const char* text) {
    node_t *n = NULL;
    if (type == NODE_LABEL) {
        n = label_create("app_lbl", 0, 0, text ? text : "", 0xFFFFFFFF);
    } else if (type == NODE_BUTTON) {
        n = button_create("app_btn", 0, 0, 100, 30, text ? text : "");
    } else if (type == NODE_PANEL) {
        n = panel_create("app_pnl", 0, 0, 100, 100, 0x88000000);
    }
    
    if (n) return n->id;
    return 0;
}

// 122: canvas_add() -> adds a node to a parent
int sys_gui_canvas_add(uint32_t parent_id, uint32_t child_id) {
    if (!g_compositor || !g_compositor->scene_root) return -1;
    node_t *parent = node_find_by_id(g_compositor->scene_root, parent_id);
    node_t *child = node_find_by_id(g_compositor->scene_root, child_id);
    
    if (parent && child) {
        node_add_child(parent, child);
        node_mark_dirty(parent, NODE_DIRTY_LAYOUT | NODE_DIRTY_PAINT);
        return 0;
    }
    return -1;
}

// 123: node_move() -> set local x, y
int sys_gui_node_move(uint32_t node_id, int x, int y) {
    if (!g_compositor || !g_compositor->scene_root) return -1;
    node_t *n = node_find_by_id(g_compositor->scene_root, node_id);
    if (n) {
        node_set_position(n, x, y);
        return 0;
    }
    return -1;
}

// 124: camera_zoom()
int sys_gui_camera_zoom(float zoom) {
    if (!g_compositor || !g_compositor->camera) return -1;
    g_compositor->camera->zoom_fp = (int)(zoom * 65536.0f);
    compositor_invalidate_full(g_compositor);
    return 0;
}

// 125: image_create() -> creates an image node with pixel buffer
uint32_t sys_gui_image_create(uint32_t parent_id, int width, int height,
                               const uint32_t *pixels) {
    if (!g_compositor || !g_compositor->scene_root) return 0;
    if (width <= 0 || height <= 0 || !pixels) return 0;

    /* Find the parent node (the window canvas) */
    node_t *parent = node_find_by_id(g_compositor->scene_root, parent_id);
    if (!parent) return 0;

    /* Create image node positioned at (0,0) inside the parent */
    node_t *img = image_node_create("app_img", 0, 0, width, height, pixels);
    if (!img) return 0;

    /* Bind to current process */
    if (current_task) {
        /* image nodes don't need PID binding — they're children of a window
         * that is already bound. But we store it for reference. */
    }

    node_add_child(parent, img);
    node_mark_dirty(parent, NODE_DIRTY_LAYOUT | NODE_DIRTY_PAINT);
    return img->id;
}

// 126: image_update() -> updates pixel buffer of an existing image node
int sys_gui_image_update(uint32_t node_id, const uint32_t *pixels,
                          uint32_t count) {
    if (!g_compositor || !g_compositor->scene_root) return -1;
    node_t *n = node_find_by_id(g_compositor->scene_root, node_id);
    if (!n || n->type != NODE_IMAGE) return -1;
    if (!pixels || count == 0) return -1;

    return image_node_update(n, pixels, count);
}

// ============================================================
// New syscalls for POSIX compatibility (BusyBox support)
// ============================================================

int sys_getpid(void) {
  if (!current_task) return 1;
  return current_task->id;
}

// Kernel-side stat struct that matches newlib's sys/stat.h on x86_64
struct kernel_stat {
  uint64_t st_dev;
  uint64_t st_ino;
  uint32_t st_mode;
  uint64_t st_nlink;
  uint32_t st_uid;
  uint32_t st_gid;
  uint64_t st_rdev;
  uint64_t st_size;
  int64_t  st_atime;
  int64_t  st_atimensec;
  int64_t  st_mtime;
  int64_t  st_mtimensec;
  int64_t  st_ctime;
  int64_t  st_ctimensec;
  int64_t  st_blksize;
  int64_t  st_blocks;
  int64_t  st_spare4[2];
};

// S_IFMT constants matching newlib
#define K_S_IFMT   0170000
#define K_S_IFDIR  0040000
#define K_S_IFREG  0100000
#define K_S_IFCHR  0020000
#define K_S_IRUSR  0000400
#define K_S_IWUSR  0000200
#define K_S_IXUSR  0000100
#define K_S_IRGRP  0000040
#define K_S_IWGRP  0000020
#define K_S_IXGRP  0000010
#define K_S_IROTH  0000004
#define K_S_IWOTH  0000002
#define K_S_IXOTH  0000001

int sys_stat(const char *path, struct kernel_stat *st) {
  if (!path || !st) return -1;
  const char *resolved = resolve_path(path);
  if (!resolved) return -1;
  
  memset(st, 0, sizeof(struct kernel_stat));
  
  fs_node_t *node = vfs_open(resolved);
  if (!node) return -1;
  
  st->st_dev = 1;
  st->st_ino = node->inode;
  st->st_nlink = 1;
  st->st_uid = 0;
  st->st_gid = 0;
  st->st_rdev = 0;
  st->st_size = node->length;
  st->st_blksize = 4096;
  st->st_blocks = (node->length + 511) / 512;
  
  if (node->flags & FS_DIRECTORY) {
    st->st_mode = K_S_IFDIR | K_S_IRUSR | K_S_IWUSR | K_S_IXUSR
                | K_S_IRGRP | K_S_IXGRP | K_S_IROTH | K_S_IXOTH;
  } else {
    st->st_mode = K_S_IFREG | K_S_IRUSR | K_S_IWUSR
                | K_S_IRGRP | K_S_IWGRP | K_S_IROTH | K_S_IWOTH;
  }
  
  st->st_atime = (int64_t)(timer_ticks / 100);
  st->st_mtime = st->st_atime;
  st->st_ctime = st->st_atime;
  
  return 0;
}

int sys_fstat(int fd, struct kernel_stat *st) {
  if (!st) return -1;
  memset(st, 0, sizeof(struct kernel_stat));
  
  if (fd >= 0 && fd <= 2) {
    // stdin/stdout/stderr — character device
    st->st_dev = 1;
    st->st_ino = fd + 1;
    st->st_mode = K_S_IFCHR | K_S_IRUSR | K_S_IWUSR;
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_size = 0;
    st->st_blksize = 512;
    st->st_blocks = 0;
    st->st_atime = (int64_t)(timer_ticks / 100);
    st->st_mtime = st->st_atime;
    st->st_ctime = st->st_atime;
    return 0;
  }
  
  if (fd < 0 || fd >= 32 || fd_table[fd].type != FD_TYPE_FILE) return -1;
  
  kfile_t *f = &fd_table[fd];
  st->st_dev = 1;
  st->st_ino = fd + 1;
  st->st_nlink = 1;
  st->st_uid = 0;
  st->st_gid = 0;
  st->st_rdev = 0;
  st->st_size = f->size;
  st->st_blksize = 4096;
  st->st_blocks = (f->size + 511) / 512;
  
  if (f->socket_ptr) {
    fs_node_t *node = (fs_node_t *)f->socket_ptr;
    if (node->flags & FS_DIRECTORY)
      st->st_mode = K_S_IFDIR | K_S_IRUSR | K_S_IWUSR | K_S_IXUSR;
    else
      st->st_mode = K_S_IFREG | K_S_IRUSR | K_S_IWUSR;
  }
  
  st->st_atime = (int64_t)(timer_ticks / 100);
  st->st_mtime = st->st_atime;
  st->st_ctime = st->st_atime;
  
  return 0;
}

int sys_unlink(const char *path) {
  if (!path) return -1;
  const char *resolved = resolve_path(path);
  if (!resolved) return -1;
  return sdfs_delete(resolved);
}

int sys_mkdir(const char *path, uint32_t mode) {
  (void)mode;
  if (!path) return -1;
  const char *resolved = resolve_path(path);
  if (!resolved) return -1;
  return sdfs_create_dir(resolved);
}

int sys_rmdir(const char *path) {
  if (!path) return -1;
  const char *resolved = resolve_path(path);
  if (!resolved) return -1;
  return sdfs_delete(resolved);
}

int sys_chdir(const char *path) {
  if (!path || !current_task) return -1;
  const char *resolved = resolve_path(path);
  if (!resolved) return -1;
  
  // Verify the path is a directory
  fs_node_t *node = vfs_open(resolved);
  if (!node) return -1;
  if (!(node->flags & FS_DIRECTORY)) return -1;
  
  strncpy(current_task->cwd, resolved, 255);
  current_task->cwd[255] = '\0';
  
  // Normalize: remove trailing slash unless it's just "/"
  int len = strlen(current_task->cwd);
  if (len > 1 && current_task->cwd[len - 1] == '/') {
    current_task->cwd[len - 1] = '\0';
  }
  
  return 0;
}

int sys_getcwd(char *buf, uint32_t size) {
  if (!buf || !current_task) return -1;
  strncpy(buf, current_task->cwd, size - 1);
  buf[size - 1] = '\0';
  return 0;
}

int sys_kill(int pid, int sig) {
  (void)sig;
  if (pid <= 0) return -1;
  sys_kill_by_pid(pid);
  return 0;
}

int sys_gettimeofday(void *tv, void *tz) {
  (void)tz;
  if (!tv) return -1;
  struct {
    int64_t tv_sec;
    int64_t tv_usec;
  } *t = (void *)tv;
  
  uint64_t ticks = timer_ticks;
  t->tv_sec = (int64_t)(ticks / 100);
  t->tv_usec = (int64_t)((ticks % 100) * 10000);
  return 0;
}

// Minimal getdents: returns buffer filled with struct dirent entries
// struct dirent from newlib (64-bit):
struct kernel_dirent {
  uint64_t d_ino;
  int64_t  d_off;
  uint16_t d_reclen;
  uint8_t  d_type;
  char     d_name[256];
};

#define K_DT_UNKNOWN 0
#define K_DT_DIR     4
#define K_DT_REG     8

int sys_getdents(uint32_t fd, void *buf, uint32_t count) {
  serial_print("[syscall] sys_getdents fd=");
  char dbuf[16];
  itoa(fd, dbuf, 10);
  serial_print(dbuf);
  serial_print(" count=");
  itoa(count, dbuf, 10);
  serial_print(dbuf);
  serial_print("\n");

  if (fd < 0 || fd >= 32 || fd_table[fd].type != FD_TYPE_FILE) {
    serial_print("[syscall] sys_getdents invalid fd\n");
    return -1;
  }
  kfile_t *f = &fd_table[fd];
  if (!f->socket_ptr) return -1;
  
  fs_node_t *node = (fs_node_t *)f->socket_ptr;
  if (!(node->flags & FS_DIRECTORY)) return -1;
  
  // Use f->offset as directory entry index
  uint32_t index = f->offset;
  uint32_t pos = 0;
  
  while (pos + (uint32_t)sizeof(struct kernel_dirent) <= count) {
    struct dirent *vfs_dirent = readdir_fs(node, index);
    if (!vfs_dirent) {
      serial_print("[syscall] sys_getdents EOF\n");
      break;
    }
    
    struct kernel_dirent *kd = (struct kernel_dirent *)((uint8_t *)buf + pos);
    memset(kd, 0, sizeof(struct kernel_dirent));
    
    kd->d_ino = index + 1;
    kd->d_off = sizeof(struct kernel_dirent);
    kd->d_reclen = sizeof(struct kernel_dirent);
    
    strncpy(kd->d_name, vfs_dirent->name, 255);
    kd->d_name[255] = '\0';
    
    pos += sizeof(struct kernel_dirent);
    index++;
  }
  
  // Update offset with how many entries we read
  f->offset = index;
  
  return (int)pos;
}

static void pipe_add_ref(int fd) {
  if (fd >= 0 && fd < 32 && fd_table[fd].type == FD_TYPE_PIPE) {
    pipe_t *p = (pipe_t *)fd_table[fd].socket_ptr;
    if (p) p->refcount++;
  }
}

int sys_dup(int oldfd) {
  if (oldfd < 0 || oldfd >= 32 || fd_table[oldfd].type == FD_TYPE_FREE) return -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      memcpy(&fd_table[i], &fd_table[oldfd], sizeof(kfile_t));
      pipe_add_ref(i);
      return i;
    }
  }
  return -1;
}

int sys_dup2(int oldfd, int newfd) {
  if (oldfd < 0 || oldfd >= 32 || newfd < 0 || newfd >= 32) return -1;
  if (fd_table[oldfd].type == FD_TYPE_FREE) return -1;
  if (newfd == oldfd) return newfd;
  if (fd_table[newfd].type != FD_TYPE_FREE) sys_close(newfd);
  memcpy(&fd_table[newfd], &fd_table[oldfd], sizeof(kfile_t));
  pipe_add_ref(newfd);
  return newfd;
}

int sys_pipe(int pipefd[2]) {
  pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
  if (!p) return -1;
  memset(p, 0, sizeof(pipe_t));
  p->refcount = 2;

  int rfd = -1, wfd = -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      if (rfd == -1) rfd = i;
      else if (wfd == -1) { wfd = i; break; }
    }
  }
  if (rfd == -1 || wfd == -1) {
    kfree(p);
    return -1;
  }

  fd_table[rfd].type = FD_TYPE_PIPE;
  fd_table[rfd].base_addr = 1; // read end
  fd_table[rfd].socket_ptr = (void *)p;
  fd_table[rfd].offset = 0;
  fd_table[rfd].size = 0;

  fd_table[wfd].type = FD_TYPE_PIPE;
  fd_table[wfd].base_addr = 2; // write end
  fd_table[wfd].socket_ptr = (void *)p;
  fd_table[wfd].offset = 0;
  fd_table[wfd].size = 0;

  pipefd[0] = rfd;
  pipefd[1] = wfd;
  return 0;
}

void syscall_handler(registers_t *regs) {
  uint32_t call_num = regs->rax;
  switch (call_num) {
    case 1: sys_exit(regs->rdi); break;
    case 2: regs->rax = sys_brk(regs->rdi); break;
    case 3: regs->rax = sys_read(regs->rdi, (void *)regs->rsi, regs->rdx); break;
    case 4: regs->rax = sys_write(regs->rdi, (void *)regs->rsi, regs->rdx); break;
    case 5: regs->rax = sys_open((char *)regs->rdi, regs->rsi); break;
    case 6: regs->rax = sys_close(regs->rdi); break;
    case 7: regs->rax = sys_waitpid((int)regs->rdi, (int *)regs->rsi, (int)regs->rdx); break;
    case 8: regs->rax = timer_ticks; break;
    case 10: regs->rax = keyboard_get_event((void *)regs->rdi); break;
    case 11: regs->rax = keyboard_is_pressed((uint8_t)regs->rdi); break;
    case 14: regs->rax = fork_process(regs); break;
    case 15:
      regs->rax = do_execve(regs, (const char *)regs->rdi,
                            (char *const *)regs->rsi,
                            (char *const *)regs->rdx);
      break;
    case 16: regs->rax = sys_tcgetattr((int)regs->rdi, (struct termios *)regs->rsi); break;
    case 17: regs->rax = sys_tcsetattr((int)regs->rdi, (int)regs->rsi, (const struct termios *)regs->rdx); break;
    case 18: regs->rax = sys_ioctl((int)regs->rdi, (unsigned long)regs->rsi, (void *)regs->rdx); break;
    case 19: regs->rax = sys_lseek(regs->rdi, regs->rsi, regs->rdx); break;
    case 20: regs->rax = sys_getpid(); break;
    case 21: regs->rax = sys_gettimeofday((void *)regs->rdi, (void *)regs->rsi); break;
    case 22: regs->rax = sys_stat((const char *)regs->rdi, (struct kernel_stat *)regs->rsi); break;
    case 23: regs->rax = sys_fstat((int)regs->rdi, (struct kernel_stat *)regs->rsi); break;
    case 24: regs->rax = sys_unlink((const char *)regs->rdi); break;
    case 25: regs->rax = sys_mkdir((const char *)regs->rdi, (uint32_t)regs->rsi); break;
    case 26: regs->rax = sys_chdir((const char *)regs->rdi); break;
    case 27: regs->rax = sys_getcwd((char *)regs->rdi, (uint32_t)regs->rsi); break;
    case 28: regs->rax = sys_kill((int)regs->rdi, (int)regs->rsi); break;
    case 29: regs->rax = sys_rmdir((const char *)regs->rdi); break;
    case 30: regs->rax = sys_getdents((uint32_t)regs->rdi, (void *)regs->rsi, (uint32_t)regs->rdx); break;
    case 31: regs->rax = sys_pipe((int *)regs->rdi); break;
    case 32: regs->rax = sys_dup((int)regs->rdi); break;
    case 33: regs->rax = sys_dup2((int)regs->rdi, (int)regs->rsi); break;
    
    // Fase 5: OSDev GUI Syscalls (Modern Scene Graph SDK)
    case 120: regs->rax = sys_gui_canvas_create((int)regs->rdi, (int)regs->rsi, (const char *)regs->rdx); break;
    case 121: regs->rax = sys_gui_node_create((node_type_t)regs->rdi, (const char *)regs->rsi); break;
    case 122: regs->rax = sys_gui_canvas_add((uint32_t)regs->rdi, (uint32_t)regs->rsi); break;
    case 123: regs->rax = sys_gui_node_move((uint32_t)regs->rdi, (int)regs->rsi, (int)regs->rdx); break;
    case 124: {
        // Float in register rdi is tricky, but for simplicity let's assume it was passed as integer scaled by 1000
        float zoom = (float)((int)regs->rdi) / 1000.0f;
        regs->rax = sys_gui_camera_zoom(zoom);
        break;
    }

    // Image node syscalls — pixel buffer rendering for apps like Doom
    case 125: regs->rax = sys_gui_image_create((uint32_t)regs->rdi, (int)regs->rsi, (int)regs->rdx, (const uint32_t *)regs->r10); break;
    case 126: regs->rax = sys_gui_image_update((uint32_t)regs->rdi, (const uint32_t *)regs->rsi, (uint32_t)regs->rdx); break;
  }
}

void init_syscalls() {
  kernel_termios.c_cc[VMIN] = 1;
  kernel_termios.c_cc[VTIME] = 0;
}
