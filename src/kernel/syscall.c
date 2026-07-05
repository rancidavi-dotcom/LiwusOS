#include "syscall.h"
#include "elf.h"
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
#include "vga.h"
#include "vmm.h"
#include "vfs.h"
#include "termios.h"

extern int elf_class(void *file_buffer);
extern uint64_t elf32_load_file(void *file_buffer);
extern uint64_t elf64_load_file(void *file_buffer);
extern task_t *current_task;

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
  (void)status;
  sys_exit_process(0);
}

int sys_execve_is_64 = 0;

int sys_execve(const char *filename, char *const argv[], char *const envp[]) {
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
    return -1;
  }

  int eclass = elf_class(addr);
  if (eclass != ELFCLASS32 && eclass != ELFCLASS64) {
    launch_last_error = "ELF class invalido";
    return -1;
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
    return -1;
  }

  uint64_t stack_size = 64 * 1024;
  uint64_t stack_top = 0xC0000000;
  uint64_t stack_base = stack_top - stack_size;
  for (uint64_t vaddr = stack_base; vaddr < stack_top; vaddr += 4096) {
    void *phys = kmalloc_a(4096);
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
    esp--; *esp = 0;
    for (int i = argc - 1; i >= 0; i--) {
      esp--; *esp = arg_ptrs[i];
    }
    uint32_t argv_ptr_addr = (uint32_t)(uint64_t)esp;
    esp--; *esp = 0;
    esp--; *esp = argv_ptr_addr;
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
    rsp--; *rsp = 0;
    for (int i = argc - 1; i >= 0; i--) {
      rsp--; *rsp = arg_ptrs[i];
    }
    uint64_t argv_ptr_addr = (uint64_t)rsp;
    rsp--; *rsp = 0;
    rsp--; *rsp = argv_ptr_addr;
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

extern int sys_execve_is_64;

extern page_directory_t *current_directory;
extern page_directory_t *vmm_create_directory(void);
extern void switch_page_directory(page_directory_t *dir);

int launch_initrd_program_argv(const char *filename, char *const argv[]) {
  // Isolate memory space!
  page_directory_t *old_dir = current_directory;
  page_directory_t *new_dir = vmm_create_directory();
  switch_page_directory(new_dir);

  int ret = sys_execve(filename, argv, NULL);
  if (ret == -1) {
    switch_page_directory(old_dir);
    return -1;
  }
  if (execve_new_esp == 0) {
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
  
  // Set the correct directory for the newly spawned task
  extern task_t *task_list;
  task_t *t = task_list->next;
  while (t && t->id != pid && t != task_list) { t = t->next; }
  if (t && t->id == pid) {
    t->page_directory = new_dir;
  }

  // Restore the old directory for the caller (compositor)
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

typedef struct {
  int type;
  uint32_t base_addr;
  uint32_t size;
  uint32_t offset;
  int owned_buffer;
  void *socket_ptr;
} kfile_t;

static kfile_t fd_table[32];

#define O_CREAT 0x0200 // Definido conforme padrão comum, ajuste se necessário

int sys_open(const char *filename, int flags) {
  int fd = -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      fd = i;
      break;
    }
  }
  if (fd == -1) return -1;

  // Try VFS
  fs_node_t *node = vfs_open(filename);
  
  if (!node && (flags & O_CREAT)) {
      node = vfs_create(filename, flags);
  }

  if (node) {
    fd_table[fd].type = FD_TYPE_FILE;
    fd_table[fd].socket_ptr = (void *)node;
    fd_table[fd].offset = 0;
    fd_table[fd].size = node->length;
    fd_table[fd].base_addr = 0;
    fd_table[fd].owned_buffer = 0;
    return fd;
  }

  return -1;
}

int sys_read(int fd, void *buf, uint32_t count) {
  if (fd == 0) {
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

    if (!icanon && vmin == 0 && vtime > 0) {
        // Non-blocking read with timeout (for ESC sequence detection)
        int timeout_ticks = vtime * 10; // vtime in deciseconds, 100 Hz timer
        int start = (int)timer_ticks;
        while (!keyboard_pop_char(&c)) {
            if ((int)(timer_ticks - start) >= timeout_ticks) {
                return 0; // timeout
            }
            switch_task();
        }
    } else {
        while (!keyboard_pop_char(&c)) {
            switch_task();
        }
    }

    if (icanon) {
        // Cooked mode: echo always
        vga_putc(c);
        ((char *)buf)[0] = c;
        return 1;
    }

    // Raw mode: translate special keys to ANSI escape sequences
    if (kernel_termios.c_lflag & ECHO) {
        vga_putc(c);
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

// TIOCGWINSZ
#define TIOCGWINSZ 0x5413

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int sys_ioctl(int fd, unsigned long request, void *argp) {
  (void)fd;
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
    void *phys = kmalloc_a(4096);
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
    esp--; *esp = 0;
    for (int i = argc - 1; i >= 0; i--) {
      esp--; *esp = arg_ptrs[i];
    }
    uint32_t argv_ptr_addr = (uint32_t)(uint64_t)esp;
    esp--; *esp = 0;
    esp--; *esp = argv_ptr_addr;
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
    rsp--; *rsp = 0;
    for (int i = argc - 1; i >= 0; i--) {
      rsp--; *rsp = arg_ptrs[i];
    }
    uint64_t argv_ptr_addr = (uint64_t)rsp;
    rsp--; *rsp = 0;
    rsp--; *rsp = argv_ptr_addr;
    rsp--; *rsp = (uint64_t)argc;
    regs->rsp = (uint64_t)rsp;
  }

  regs->rip = entry;
  return 0;
}

#include "lgx.h"

// Retorna o ID da janela criada (ou -1 se falhar)
int sys_gui_create_window(int width, int height) {
    // 0 = standard flags, centered for now
    int win_x = (current_video_mode.width - width) / 2;
    int win_y = (current_video_mode.height - height) / 2;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    
    window_t* win = window_create(win_x, win_y, width, height, 0);
    if (!win) return -1;
    
    // Mapear buffer para USER SPACE (0x07 = Present | Read/Write | User)
    uint64_t virt = (uint64_t)win->buffer;
    uint64_t num_pages = (width * height * 4 + 4095) / 4096;
    for(uint64_t i=0; i<num_pages; i++) {
        vmm_map_page((void*)(virt + i*4096), (void*)(virt + i*4096), 0x07);
    }
    
    return win->id;
}

uint32_t* sys_gui_get_buffer(int win_id) {
    // Pesquisa a janela na lista
    extern window_t* window_list_head;
    window_t* curr = window_list_head;
    while (curr) {
        if (curr->id == (uint32_t)win_id) return curr->buffer;
        curr = curr->next;
    }
    return NULL;
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
    
    // Fase 5: OSDev GUI Syscalls
    case 120: regs->rax = sys_gui_create_window((int)regs->rdi, (int)regs->rsi); break;
    case 121: regs->rax = (uint64_t)sys_gui_get_buffer((int)regs->rdi); break;
    case 122: /* refresh */ break; // No momento, o loop já faz a 60fps automaticamente
    default: break;
  }
}

void init_syscalls() {
  memset(fd_table, 0, sizeof(fd_table));
  kernel_termios.c_cc[VMIN] = 1;
  kernel_termios.c_cc[VTIME] = 0;
}
