#include "syscall.h"
#include "elf.h"
#include "sdfs.h"
#include "initrd.h"
#include "io.h"
#include "kheap.h"
#include "keyboard.h"
#include "mouse.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "kernel_layout.h"
#include "timer.h"
#include "vga.h"
#include "vmm.h"
#include "vfs.h"
#include "termios.h"
#include "usercopy.h"
#include "vma.h"
#include "kstat.h"
#include "elf_loader.h"

extern uint64_t last_elf_heap_start;
extern int last_elf_load_error;

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
static const char *resolve_path(const char *path);
static int user_stack_push64(uint64_t *sp, uint64_t value);
static int user_stack_push32(uint64_t *sp, uint32_t value);

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
  const char *resolved_filename = resolve_path(filename);
  if (!resolved_filename) return (uint64_t)-1;
  strncpy(sdfs_path, resolved_filename, sizeof(sdfs_path) - 1);
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

  int eclass = elf_class(addr, size);
  if (eclass != ELFCLASS32 && eclass != ELFCLASS64) {
    launch_last_error = "ELF class invalido";
    return (uint64_t)-1;
  }

  uint64_t entry;
  if (eclass == ELFCLASS32) {
    entry = elf32_load_file_in(current_directory, addr, size);
    sys_execve_is_64 = 0;
  } else {
    entry = elf64_load_file_in(current_directory, addr, size);
    sys_execve_is_64 = 1;
  }
  if (entry == 0) {
    launch_last_error = "formato ELF invalido";
    return (uint64_t)-1;
  }

  uint64_t stack_size = 64 * 1024;
  uint64_t stack_top = USER_STACK_TOP;
  uint64_t stack_base = stack_top - stack_size;
  for (uint64_t vaddr = stack_base; vaddr < stack_top; vaddr += 4096) {
    void *phys = kmalloc_a(4096);
    if (!phys) return (uint64_t)-1;
    memset(phys, 0, 4096);
    if (vmm_map_page_in(current_directory, phys, (void *)vaddr,
                        PTE_P | PTE_W | PTE_U | PTE_NX) < 0) {
      pmm_free_block(phys);
      return (uint64_t)-1;
    }
  }
  if (vma_insert(current_directory, stack_base, stack_top,
                 VMA_PROT_READ | VMA_PROT_WRITE,
                 VMA_PRIVATE | VMA_ANON | VMA_STACK, 0, 0) < 0)
    return (uint64_t)-1;

  uint64_t stack_cursor = stack_top;

  int argc = 0;
  if (argv) {
    while (argv[argc] && argc < (int)max_args) argc++;
  }

  if (eclass == ELFCLASS32) {
    uint32_t arg_ptrs[16];
    for (int i = 0; i < argc; i++) {
      size_t len = strlen(argv[i]) + 1;
      stack_cursor -= len;
      if (copy_to_user((void *)stack_cursor, argv[i], len) < 0)
        return (uint64_t)-1;
      arg_ptrs[i] = (uint32_t)stack_cursor;
    }

    uint64_t esp = stack_cursor & ~0xFULL;
    if (user_stack_push32(&esp, 0) < 0 || user_stack_push32(&esp, 0) < 0)
      return (uint64_t)-1;
    for (int i = argc - 1; i >= 0; i--) {
      if (user_stack_push32(&esp, arg_ptrs[i]) < 0) return (uint64_t)-1;
    }
    if (user_stack_push32(&esp, (uint32_t)argc) < 0) return (uint64_t)-1;

    extern uint64_t execve_new_esp;
    execve_new_esp = esp;
  } else {
    uint64_t arg_ptrs[16];
    for (int i = 0; i < argc; i++) {
      size_t len = strlen(argv[i]) + 1;
      stack_cursor -= len;
      if (copy_to_user((void *)stack_cursor, argv[i], len) < 0)
        return (uint64_t)-1;
      arg_ptrs[i] = stack_cursor;
    }

    uint64_t rsp = stack_cursor & ~0xFULL;
    for (int i = 0; i < 4; i++)
      if (user_stack_push64(&rsp, 0) < 0) return (uint64_t)-1;
    for (int i = argc - 1; i >= 0; i--) {
      if (user_stack_push64(&rsp, arg_ptrs[i]) < 0) return (uint64_t)-1;
    }
    if (user_stack_push64(&rsp, (uint64_t)argc) < 0) return (uint64_t)-1;

    extern uint64_t execve_new_esp;
    execve_new_esp = rsp;
  }

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
  if (current_task && current_task->process->cwd[0]) {
    int cwd_len = strlen(current_task->process->cwd);
    memcpy(resolved, current_task->process->cwd, cwd_len);
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
  page_directory_t *task_old_dir = current_task ? current_task->process->mm : old_dir;
  if (current_task) current_task->process->mm = new_dir;
  
  switch_page_directory(new_dir);
  serial_print("[kernel] launch_initrd_program_argv calling sys_execve\n");
  uint64_t ret = sys_execve(filename, argv, NULL);
  if (ret == (uint64_t)-1) {
    if (current_task) current_task->process->mm = task_old_dir;
    switch_page_directory(old_dir);
    return -1;
  }
  if (execve_new_esp == 0) {
    if (current_task) current_task->process->mm = task_old_dir;
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
  last_elf_heap_start = 0;
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
    t->process->mm = new_dir;
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
      pdp_t *pdp = (pdp_t *)phys_to_virt(pml4->entries[0] & ~0xfff);
      serial_print("  PDP[0]: "); serial_print_hex(pdp->entries[0]); serial_print("\n");
      if (pdp->entries[0] & 1) {
        pd_t *pd = (pd_t *)phys_to_virt(pdp->entries[0] & ~0xfff);
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
            pt_t *pt = (pt_t *)phys_to_virt(pd->entries[pd_i] & ~0xfff);
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
  if (current_task) current_task->process->mm = task_old_dir;
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

#define PIPE_BUF_SIZE 4096

// Table de descritores de arquivos por processo.
// A arquitetura define task_t.file_descriptors[32], e o fork() já copia
// essa table com refcount de pipes. Esta static funciona apenas como
// fallback para quando não há um processo user ativo (ex.: durante o boot
// ou para kernel tasks) — o caminho principal é o per-processo.
static kfile_t fd_table[32];
static uint64_t live_file_descriptions = 0;
static uint64_t live_pipe_objects = 0;

// Retorna a fd table do processo corrente. Se houver um current_task
// (qualquer tarefa, inclusive kernel tasks), usa a table isolada dele;
// caso contrário cai na static global.
static kfile_t *get_fd_table(void) {
  if (current_task && current_task->process)
    return current_task->process->file_descriptors;
  return fd_table;
}

void sys_fd_get(kfile_t *file) {
  if (!file) return;
  if (file->type == FD_TYPE_FILE && file->description) {
    file->description->references++;
  } else if (file->type == FD_TYPE_PIPE && file->socket_ptr) {
    uint64_t flags = task_scheduler_lock();
    pipe_t *p = (pipe_t *)file->socket_ptr;
    p->references++;
    if (file->base_addr == 1) p->readers++;
    else if (file->base_addr == 2) p->writers++;
    task_scheduler_unlock(flags);
  }
}

void sys_fd_put(kfile_t *file) {
  if (!file || file->type == FD_TYPE_FREE) return;
  if (file->type == FD_TYPE_FILE && file->description) {
    file_description_t *description = file->description;
    file->description = NULL;
    if (--description->references == 0) {
      vfs_node_put(description->node);
      kfree(description);
      if (live_file_descriptions) live_file_descriptions--;
    }
  } else if (file->type == FD_TYPE_PIPE && file->socket_ptr) {
    pipe_t *p = (pipe_t *)file->socket_ptr;
    uint64_t flags = task_scheduler_lock();
    if (file->base_addr == 1 && p->readers > 0) p->readers--;
    if (file->base_addr == 2 && p->writers > 0) p->writers--;
    wait_queue_wake_locked(&p->read_waiters, 0, 0, false, UINT32_MAX);
    wait_queue_wake_locked(&p->write_waiters, 0, 0, false, UINT32_MAX);
    int free_pipe = p->references > 0 && --p->references == 0;
    task_scheduler_unlock(flags);
    if (free_pipe) {
      kfree(p);
      if (live_pipe_objects) live_pipe_objects--;
    }
  } else if (file->type == FD_TYPE_FILE && file->socket_ptr) {
    /* Compatibility for kernel-created file entries without a description. */
    vfs_node_put((fs_node_t *)file->socket_ptr);
  }
  memset(file, 0, sizeof(*file));
}

static pipe_t *pipe_pin_fd(int fd, int endpoint) {
  uint64_t flags = task_scheduler_lock();
  kfile_t *table = get_fd_table();
  pipe_t *pipe = NULL;
  if (fd >= 0 && fd < 32 && table[fd].type == FD_TYPE_PIPE &&
      table[fd].base_addr == (uint32_t)endpoint && table[fd].socket_ptr) {
    pipe = (pipe_t *)table[fd].socket_ptr;
    pipe->references++;
    if (endpoint == 1) pipe->readers++;
    else pipe->writers++;
  }
  task_scheduler_unlock(flags);
  return pipe;
}

typedef struct {
  pipe_t *pipe;
  int endpoint;
} pipe_wait_pin_t;

static int pipe_unpin_locked(pipe_t *pipe, int endpoint) {
  if (endpoint == 1 && pipe->readers > 0) pipe->readers--;
  if (endpoint == 2 && pipe->writers > 0) pipe->writers--;
  wait_queue_wake_locked(&pipe->read_waiters, 0, 0, false, UINT32_MAX);
  wait_queue_wake_locked(&pipe->write_waiters, 0, 0, false, UINT32_MAX);
  return pipe->references > 0 && --pipe->references == 0;
}

static void pipe_unpin(pipe_t *pipe, int endpoint) {
  if (!pipe) return;
  uint64_t flags = task_scheduler_lock();
  int free_pipe = pipe_unpin_locked(pipe, endpoint);
  task_scheduler_unlock(flags);
  if (free_pipe) {
    kfree(pipe);
    if (live_pipe_objects) live_pipe_objects--;
  }
}

static void pipe_unpin_current(pipe_wait_pin_t *pin) {
  if (!pin || !pin->pipe) return;
  uint64_t flags = task_scheduler_lock();
  if (current_task && current_task->wait_cancel_context == pin) {
    current_task->wait_cancel = NULL;
    current_task->wait_cancel_context = NULL;
  }
  int free_pipe = pipe_unpin_locked(pin->pipe, pin->endpoint);
  task_scheduler_unlock(flags);
  if (free_pipe) {
    kfree(pin->pipe);
    if (live_pipe_objects) live_pipe_objects--;
  }
}

static void pipe_wait_cancel(void *context) {
  pipe_wait_pin_t *pin = (pipe_wait_pin_t *)context;
  if (pin) pipe_unpin(pin->pipe, pin->endpoint);
}

void task_process_release_io(process_t *process) {
  if (!process) return;
  for (int i = 0; i < 32; i++)
    sys_fd_put(&process->file_descriptors[i]);
}

void task_release_io(task_t *task) {
  if (!task) return;
  task_process_release_io(task->process);
}

uint64_t sys_io_live_file_descriptions(void) {
  return live_file_descriptions;
}

uint64_t sys_io_live_pipe_objects(void) {
  return live_pipe_objects;
}

static uint32_t *file_offset(kfile_t *file) {
  return file->description ? &file->description->offset : &file->offset;
}

static uint32_t file_size(kfile_t *file) {
  return file->description ? file->description->size : file->size;
}

#define O_CREAT 0x0200 // Definido conforme padrão comum, ajuste se necessário
#define O_TRUNC 0x0400
#define O_APPEND 0x0008

int sys_open(const char *filename, int flags) {
  kfile_t *fd_table = get_fd_table();
  int fd = -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      fd = i;
      break;
    }
  }
  if (fd == -1) return -1;

  char fname[256];
  if (!filename) return -EFAULT;
  if (strncpy_from_user(fname, filename, 255) < 0) return -EFAULT;
  fname[255] = '\0';

  const char *resolved = resolve_path(fname);
  if (!resolved) return -1;

  // Try VFS
  fs_node_t *node = vfs_open(resolved);
  
  if (!node && (flags & O_CREAT)) {
      node = vfs_create(resolved, flags);
  }

  if (node) {
    if (flags & O_TRUNC) {
      vfs_truncate(node);
    }
    file_description_t *description = (file_description_t *)kmalloc(sizeof(*description));
    if (!description) {
      vfs_node_put(node);
      return -1;
    }
    memset(description, 0, sizeof(*description));
    description->node = node;
    description->offset = (flags & O_APPEND) ? node->length : 0;
    description->size = node->length;
    description->references = 1;
    live_file_descriptions++;
    if (node->open) node->open(node);
    memset(&fd_table[fd], 0, sizeof(fd_table[fd]));
    fd_table[fd].type = FD_TYPE_FILE;
    fd_table[fd].socket_ptr = (void *)node;
    fd_table[fd].description = description;
    fd_table[fd].offset = description->offset;
    fd_table[fd].size = node->length;
    fd_table[fd].base_addr = 0;
    fd_table[fd].owned_buffer = 0;
    return fd;
  }
  serial_print("[syscall] sys_open failed: ");
  serial_print(fname);
  serial_print("\n");
  return -1;
}

int sys_read(int fd, void *buf, uint32_t count) {
  /* Validate the whole destination before a device/pipe/file can consume data. */
  if (count && validate_user_write(buf, count) < 0) return -EFAULT;
  if (count == 0) return 0;
  kfile_t *fd_table = get_fd_table();
  if (fd >= 0 && fd < 32 && fd_table[fd].type == FD_TYPE_PIPE) {
    pipe_t *p = pipe_pin_fd(fd, 1);
    if (!p) return -1;
    pipe_wait_pin_t wait_pin = {p, 1};
    uint8_t chunk_buffer[256];
    uint32_t got = 0;
    while (got < count) {
      uint64_t flags = task_scheduler_lock();
      if (p->bytes_avail == 0) {
        if (got || p->writers == 0) {
          task_scheduler_unlock(flags);
          break; /* Return available data, or EOF after the last writer. */
        }
        int rc = wait_queue_block_locked(&p->read_waiters, 0, 0, false,
                                         pipe_wait_cancel, &wait_pin);
        task_scheduler_unlock(flags);
        if (rc < 0) {
          pipe_unpin_current(&wait_pin);
          return -1;
        }
        switch_task();
        continue;
      }

      uint32_t amount = count - got;
      if (amount > p->bytes_avail) amount = p->bytes_avail;
      if (amount > sizeof(chunk_buffer)) amount = sizeof(chunk_buffer);
      uint32_t first = PIPE_BUF_SIZE - p->read_pos;
      if (amount > first) amount = first;
      memcpy(chunk_buffer, &p->buffer[p->read_pos], amount);
      p->read_pos = (p->read_pos + amount) % PIPE_BUF_SIZE;
      p->bytes_avail -= amount;
      wait_queue_wake_locked(&p->write_waiters, 0, 0, false, UINT32_MAX);
      task_scheduler_unlock(flags);

      if (copy_to_user((char *)buf + got, chunk_buffer, amount) < 0) {
        pipe_unpin_current(&wait_pin);
        return got ? (int)got : -EFAULT;
      }
      got += amount;
    }
    pipe_unpin_current(&wait_pin);
    return (int)got;
  }
  if (fd == 0 && fd_table[0].type == FD_TYPE_FREE) {
    // Drain any buffered escape sequence bytes first
    if (stdin_esc_pos < stdin_esc_len) {
        char bc = stdin_esc_buf[stdin_esc_pos++];
        if (copy_to_user(buf, &bc, 1) < 0) return -EFAULT;
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
            task_sleep_ms(10);
        }
    } else {
        while (!keyboard_pop_char(&c) && !serial_pop_char(&c)) {
            task_sleep_ms(10);
        }
    }

    // Convert Carriage Return (from serial) to Newline if ICRNL is set
    if ((kernel_termios.c_iflag & ICRNL) && c == '\r') {
        c = '\n';
    }

    if (icanon) {
        // Interrupt the selected foreground process. With no foreground
        // child, deliver Ctrl+C to the shell so it can cancel its input line.
        if ((kernel_termios.c_lflag & ISIG) && c == 3) {
            if (foreground_job_id >= 0) {
                task_kill_job(foreground_job_id);
                switch_task();
                return -1;
            }
            int foreground_pid = last_foreground_pid;
            if (foreground_pid > 0) {
                sys_kill_by_pid(foreground_pid);
                switch_task();
                return -1;
            }
            if (copy_to_user(buf, &c, 1) < 0) return -EFAULT;
            return 1;
        }
        // Cooked mode: echo always
        vga_putc(c);
        write_serial(c);
        if (copy_to_user(buf, &c, 1) < 0) return -EFAULT;
        return 1;
    }

    // Raw mode: translate special keys to ANSI escape sequences
    if ((kernel_termios.c_lflag & ISIG) && (unsigned char)c == 3) {
        if (foreground_job_id >= 0) {
            task_kill_job(foreground_job_id);
            switch_task();
            return -1;
        }
        int foreground_pid = last_foreground_pid;
        if (foreground_pid > 0) {
            sys_kill_by_pid(foreground_pid);
            switch_task();
            return -1;
        }
        if (copy_to_user(buf, &c, 1) < 0) return -EFAULT;
        return 1;
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
        char bc = stdin_esc_buf[0];
        if (copy_to_user(buf, &bc, 1) < 0) return -EFAULT;
        return 1;
    }

    if (copy_to_user(buf, &c, 1) < 0) return -EFAULT;
    return 1;
  }
  if (fd < 0 || fd >= 32 || fd_table[fd].type != FD_TYPE_FILE) return -1;
  kfile_t *f = &fd_table[fd];
  uint32_t *offset = file_offset(f);
  uint32_t size = file_size(f);
  if (f->socket_ptr) {
    fs_node_t *node = (fs_node_t *)f->socket_ptr;
    if (node->length > size) size = node->length;
    uint32_t available = *offset < size ? size - *offset : 0;
    uint32_t to_read = count < available ? count : available;
    if (!to_read) return 0;
    uint8_t tmp[512];
    uint32_t done = 0;
    while (done < to_read) {
      uint32_t chunk = to_read - done;
      if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
      uint32_t r = read_fs(node, *offset + done, chunk, tmp);
      if (!r) break;
      if (copy_to_user((char *)buf + done, tmp, r) < 0)
        return done ? (int)done : -EFAULT;
      done += r;
      if (r < chunk) break;
    }
    *offset += done;
    f->offset = *offset;
    return (int)done;
  }
  uint32_t available = *offset < size ? size - *offset : 0;
  uint32_t to_read = count < available ? count : available;
  if (to_read && copy_to_user(buf, (void *)(uintptr_t)(f->base_addr + *offset), to_read) < 0)
    return -EFAULT;
  *offset += to_read;
  f->offset = *offset;
  return (int)to_read;
}

extern void write_serial(char a);

int sys_write(int fd, const void *buf, uint32_t count) {
  if (count && validate_user_pointer(buf, count) < 0) return -EFAULT;
  char tmp[64];
  uint32_t done = 0;
  kfile_t *fd_table = get_fd_table();
  if ((fd == 1 || fd == 2) && fd_table[fd].type == FD_TYPE_FREE) {
    while (done < count) {
      uint32_t chunk = (count - done) < sizeof(tmp) ? (count - done) : (uint32_t)sizeof(tmp);
      if (copy_from_user(tmp, (const char *)buf + done, chunk) < 0) return -EFAULT;
      for (uint32_t i = 0; i < chunk; i++) { vga_putc(tmp[i]); write_serial(tmp[i]); }
      done += chunk;
    }
    return count;
  }
  if (fd < 0 || fd >= 32) return -1;
  kfile_t *f = &fd_table[fd];
  if (f->type == FD_TYPE_PIPE) {
    pipe_t *p = pipe_pin_fd(fd, 2);
    if (!p) return -1;
    pipe_wait_pin_t wait_pin = {p, 2};
    uint32_t written = 0;
    uint8_t chunk_buffer[sizeof(tmp)];
    while (written < count) {
      uint32_t amount = count - written;
      if (amount > sizeof(chunk_buffer)) amount = sizeof(chunk_buffer);
      if (copy_from_user(chunk_buffer, (const char *)buf + written,
                         amount) < 0) {
        pipe_unpin_current(&wait_pin);
        return written ? (int)written : -EFAULT;
      }

      uint64_t flags = task_scheduler_lock();
      if (p->readers == 0) {
        task_scheduler_unlock(flags);
        pipe_unpin_current(&wait_pin);
        return written ? (int)written : -EPIPE;
      }
      uint32_t space = PIPE_BUF_SIZE - p->bytes_avail;
      if (space == 0) {
        int rc = wait_queue_block_locked(&p->write_waiters, 0, 0, false,
                                         pipe_wait_cancel, &wait_pin);
        task_scheduler_unlock(flags);
        if (rc < 0) {
          pipe_unpin_current(&wait_pin);
          return written ? (int)written : -1;
        }
        switch_task();
        continue;
      }
      if (amount > space) amount = space;
      for (uint32_t i = 0; i < amount; i++) {
        p->buffer[p->write_pos] = chunk_buffer[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
      }
      p->bytes_avail += amount;
      wait_queue_wake_locked(&p->read_waiters, 0, 0, false, UINT32_MAX);
      task_scheduler_unlock(flags);
      written += amount;
    }
    pipe_unpin_current(&wait_pin);
    return (int)written;
  }
  if (f->type == FD_TYPE_FILE && f->socket_ptr) {
    fs_node_t *node = (fs_node_t *)f->socket_ptr;
    uint32_t *offset = file_offset(f);
    while (done < count) {
      uint32_t chunk = (count - done) < sizeof(tmp) ? (count - done) : (uint32_t)sizeof(tmp);
      if (copy_from_user(tmp, (const char *)buf + done, chunk) < 0) return -EFAULT;
      uint32_t w = write_fs(node, *offset, chunk, (uint8_t *)tmp);
      *offset += w;
      done += w;
      if (w == 0) break;
    }
    f->offset = *offset;
    f->size = node->length;
    if (f->description) f->description->size = node->length;
    return (int)done;
  }
  return -1;
}

int sys_close(int fd) {
  kfile_t *fd_table = get_fd_table();
  if (fd < 0 || fd >= 32 || fd_table[fd].type == FD_TYPE_FREE) return -1;
  kfile_t *f = &fd_table[fd];
  sys_fd_put(f);
  return 0;
}

int sys_lseek(int fd, int offset, int whence) {
  kfile_t *fd_table = get_fd_table();
  if (fd < 0 || fd >= 32 || fd_table[fd].type != FD_TYPE_FILE) return -1;
  kfile_t *f = &fd_table[fd];
  uint32_t *offp = file_offset(f);
  uint32_t size = file_size(f);
  int new_offset;

  if (whence == 0) {
    new_offset = offset;
  } else if (whence == 1) {
    new_offset = (int)*offp + offset;
  } else if (whence == 2) {
    new_offset = (int)size + offset;
  } else {
    return -1;
  }

  if (new_offset < 0) return -1;
  if ((uint32_t)new_offset > size) new_offset = (int)size;
  *offp = (uint32_t)new_offset;
  f->offset = *offp;
  return (int)*offp;
}

// syscalls 12 e 13 foram removidos (framebuffer GUI)

// Termios syscalls
int sys_tcgetattr(int fd, struct termios *t) {
  (void)fd;
  if (!t) return -1;
  struct termios kt;
  memcpy(&kt, &kernel_termios, sizeof(struct termios));
  if (copy_to_user(t, &kt, sizeof(struct termios)) < 0) return -EFAULT;
  return 0;
}

int sys_tcsetattr(int fd, int action, const struct termios *t) {
  (void)fd;
  (void)action;
  if (!t) return -1;
  struct termios kt;
  if (copy_from_user(&kt, t, sizeof(struct termios)) < 0) return -EFAULT;
  memcpy(&kernel_termios, &kt, sizeof(struct termios));
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
    struct winsize ws;
    ws.ws_row = 25;
    ws.ws_col = 80;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    if (copy_to_user(argp, &ws, sizeof(ws)) < 0) return -EFAULT;
    return 0;
  }
  return -1;
}

extern int sys_waitpid(int pid, int *status, int options);

#define EXEC_VECTOR_MAX 32
#define EXEC_STRING_MAX 256
#define EXEC_STRINGS_BYTES 8192
#define EXEC_STACK_SIZE (64 * 1024)
#define EXEC_STACK_TOP USER_STACK_TOP

static int exec_copy_string(char *dst, const char *user, uint32_t capacity,
                            uint32_t *copied) {
  if (!dst || !user || capacity == 0) return -EFAULT;
  for (uint32_t i = 0; i < capacity; i++) {
    if (copy_from_user(&dst[i], user + i, 1) < 0) return -EFAULT;
    if (dst[i] == '\0') {
      *copied = i + 1;
      return 0;
    }
  }
  return -E2BIG;
}

static int exec_copy_vector(char *const user_vec[], char *storage,
                            uint32_t *used, char *kernel_vec[EXEC_VECTOR_MAX],
                            uint32_t *count) {
  *count = 0;
  if (!user_vec) return 0;
  for (uint32_t i = 0; i < EXEC_VECTOR_MAX; i++) {
    char *user_string = NULL;
    if (copy_from_user(&user_string, &user_vec[i], sizeof(user_string)) < 0)
      return -EFAULT;
    if (!user_string) return 0;
    if (*used >= EXEC_STRINGS_BYTES) return -E2BIG;
    uint32_t copied = 0;
    uint32_t cap = EXEC_STRINGS_BYTES - *used;
    if (cap > EXEC_STRING_MAX) cap = EXEC_STRING_MAX;
    int rc = exec_copy_string(storage + *used, user_string, cap, &copied);
    if (rc < 0) return rc;
    kernel_vec[i] = storage + *used;
    *used += copied;
    *count = i + 1;
  }
  return -E2BIG; /* no NULL terminator within the bounded vector */
}

static int user_stack_push64(uint64_t *sp, uint64_t value) {
  *sp -= sizeof(value);
  return copy_to_user((void *)*sp, &value, sizeof(value));
}

static int user_stack_push32(uint64_t *sp, uint32_t value) {
  *sp -= sizeof(value);
  return copy_to_user((void *)*sp, &value, sizeof(value));
}

static int exec_build_stack(page_directory_t *mm, int elf_class_value,
                            char *argv[EXEC_VECTOR_MAX], uint32_t argc,
                            char *envp[EXEC_VECTOR_MAX], uint32_t envc,
                            uint64_t *stack_out) {
  uint64_t stack_base = EXEC_STACK_TOP - EXEC_STACK_SIZE;
  for (uint64_t va = stack_base; va < EXEC_STACK_TOP; va += PAGE_SIZE) {
    void *phys = pmm_alloc_block();
    if (!phys) return -ENOMEM;
    memset(phys, 0, PAGE_SIZE);
    if (vmm_map_page_in(mm, phys, (void *)va,
                        PTE_P | PTE_W | PTE_U | PTE_NX) < 0) {
      pmm_free_block(phys);
      return -ENOMEM;
    }
    if (validate_user_pointer((void *)va, PAGE_SIZE) < 0) {
      pmm_free_block(phys);
      return -ENOMEM;
    }
  }
  if (vma_insert(mm, stack_base, EXEC_STACK_TOP,
                 VMA_PROT_READ | VMA_PROT_WRITE,
                 VMA_PRIVATE | VMA_ANON | VMA_STACK, 0, 0) < 0)
    return -ENOMEM;

  uint64_t argv_addr[EXEC_VECTOR_MAX], env_addr[EXEC_VECTOR_MAX];
  uint64_t cursor = EXEC_STACK_TOP;
  for (int i = (int)envc - 1; i >= 0; i--) {
    uint32_t len = strlen(envp[i]) + 1;
    cursor -= len;
    if (copy_to_user((void *)cursor, envp[i], len) < 0) return -EFAULT;
    env_addr[i] = cursor;
  }
  for (int i = (int)argc - 1; i >= 0; i--) {
    uint32_t len = strlen(argv[i]) + 1;
    cursor -= len;
    if (copy_to_user((void *)cursor, argv[i], len) < 0) return -EFAULT;
    argv_addr[i] = cursor;
  }

  if (elf_class_value == ELFCLASS64) {
    uint64_t sp = cursor & ~0xFULL;
    uint64_t words = 1 + argc + 1 + envc + 1 + 2;
    if ((sp - words * sizeof(uint64_t)) & 0x8)
      if (user_stack_push64(&sp, 0) < 0) return -EFAULT;
    if (user_stack_push64(&sp, 0) < 0 || user_stack_push64(&sp, 0) < 0)
      return -EFAULT; /* AT_NULL */
    if (user_stack_push64(&sp, 0) < 0) return -EFAULT;
    for (int i = (int)envc - 1; i >= 0; i--)
      if (user_stack_push64(&sp, env_addr[i]) < 0) return -EFAULT;
    if (user_stack_push64(&sp, 0) < 0) return -EFAULT;
    for (int i = (int)argc - 1; i >= 0; i--)
      if (user_stack_push64(&sp, argv_addr[i]) < 0) return -EFAULT;
    if (user_stack_push64(&sp, argc) < 0) return -EFAULT;
    *stack_out = sp;
  } else {
    uint64_t sp = cursor & ~0xFULL;
    uint64_t words = 1 + argc + 1 + envc + 1;
    if ((sp - words * sizeof(uint32_t)) & 0x8)
      if (user_stack_push32(&sp, 0) < 0) return -EFAULT;
    if (user_stack_push32(&sp, 0) < 0) return -EFAULT;
    for (int i = (int)envc - 1; i >= 0; i--)
      if (user_stack_push32(&sp, (uint32_t)env_addr[i]) < 0) return -EFAULT;
    if (user_stack_push32(&sp, 0) < 0) return -EFAULT;
    for (int i = (int)argc - 1; i >= 0; i--)
      if (user_stack_push32(&sp, (uint32_t)argv_addr[i]) < 0) return -EFAULT;
    if (user_stack_push32(&sp, argc) < 0) return -EFAULT;
    *stack_out = sp;
  }
  return 0;
}

static int do_execve(registers_t *regs, const char *filename,
                     char *const argv[], char *const envp[]) {
  if (!regs) return -EPERM;
  if ((regs->cs & 3) != 3) {
    serial_print("[execve] rejected CS="); serial_print_hex(regs->cs); serial_print("\n");
    return -EPERM;
  }

  char fname[256];
  uint32_t ignored_len = 0;
  int rc = exec_copy_string(fname, filename, sizeof(fname), &ignored_len);
  if (rc < 0) return rc;

  char *storage = kmalloc(EXEC_STRINGS_BYTES);
  if (!storage) return -ENOMEM;
  char *kargv[EXEC_VECTOR_MAX], *kenvp[EXEC_VECTOR_MAX];
  uint32_t used = 0, argc = 0, envc = 0;
  rc = exec_copy_vector(argv, storage, &used, kargv, &argc);
  if (rc == 0) rc = exec_copy_vector(envp, storage, &used, kenvp, &envc);
  if (rc < 0) { kfree(storage); return rc; }

  char sdfs_path[512];
  const char *resolved = resolve_path(fname);
  if (!resolved) { kfree(storage); return -EFAULT; }
  strncpy(sdfs_path, resolved, sizeof(sdfs_path) - 1);
  sdfs_path[sizeof(sdfs_path) - 1] = '\0';

  uint32_t file_size = 0;
  int owned_file = 0;
  void *file = sdfs_read_file(sdfs_path, &file_size);
  if (file) owned_file = 1;
  else {
    file_size = 0;
    file = initrd_get_file(fname, &file_size);
    if (!file && fname[0] == '/') file = initrd_get_file(fname + 1, &file_size);
  }
  if (!file) { kfree(storage); return -ENOENT; }

  int klass = elf_class(file, file_size);
  if (klass != ELFCLASS32 && klass != ELFCLASS64) {
    if (owned_file) kfree(file);
    kfree(storage);
    return -ENOEXEC;
  }

  page_directory_t *old_dir = current_task->process->mm;
  page_directory_t *new_dir = vmm_create_directory();
  if (!new_dir) {
    serial_print("[execve] new address space allocation failed\n");
    if (owned_file) kfree(file);
    kfree(storage);
    return -ENOMEM;
  }

  current_task->process->mm = new_dir;
  switch_page_directory(new_dir);
  last_elf_heap_start = 0;
  uint64_t entry = klass == ELFCLASS64
      ? elf64_load_file_in(new_dir, file, file_size)
      : elf32_load_file_in(new_dir, file, file_size);
  int load_error = entry ? 0 : last_elf_load_error;
  if (owned_file) kfree(file);
  if (!entry) {
    serial_print("[execve] ELF load failed rc="); serial_print_hex((uint64_t)(int64_t)load_error); serial_print("\n");
    current_task->process->mm = old_dir;
    switch_page_directory(old_dir);
    vmm_free_directory(new_dir);
    last_elf_heap_start = 0;
    kfree(storage);
    return load_error ? load_error : -ENOEXEC;
  }

  uint64_t new_rsp = 0;
  rc = exec_build_stack(new_dir, klass, kargv, argc, kenvp, envc, &new_rsp);
  if (rc < 0) {
    serial_print("[execve] user stack setup failed rc="); serial_print_hex((uint64_t)(int64_t)rc); serial_print("\n");
    current_task->process->mm = old_dir;
    switch_page_directory(old_dir);
    vmm_free_directory(new_dir);
    last_elf_heap_start = 0;
    kfree(storage);
    return rc;
  }

  page_directory_t *old_to_free = old_dir;
  current_task->process->heap_start = last_elf_heap_start;
  current_task->process->heap_end = last_elf_heap_start;
  current_task->process->mmap_top = USER_MMAP_TOP;
  regs->rip = entry;
  regs->rsp = new_rsp;
  if (klass == ELFCLASS64) {
    regs->cs = 0x2B;
    regs->ss = 0x33;
  } else {
    regs->cs = 0x1B;
    regs->ss = 0x23;
  }
  regs->rax = regs->rbx = regs->rcx = regs->rdx = 0;
  regs->rbp = regs->rsi = regs->rdi = 0;
  regs->r8 = regs->r9 = regs->r10 = regs->r11 = 0;
  regs->r12 = regs->r13 = regs->r14 = regs->r15 = 0;
  regs->rflags = (regs->rflags & ~(1ULL << 10)) | 2;
  last_elf_heap_start = 0;
  kfree(storage);
  if (old_to_free && old_to_free != kernel_directory)
    vmm_free_directory(old_to_free);
  return 0;
}

// ============================================================
// New syscalls for POSIX compatibility (BusyBox support)
// ============================================================

int sys_getpid(void) {
  if (!current_task) return 1;
  return current_task->process ? current_task->process->pid : current_task->id;
}

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
  if (!path || !st) return -EFAULT;
  if (validate_user_write(st, sizeof(*st)) < 0) return -EFAULT;
  char kpath[256];
  if (strncpy_from_user(kpath, path, 255) < 0) return -EFAULT;
  kpath[255] = '\0';
  const char *resolved = resolve_path(kpath);
  if (!resolved) return -1;
  
  struct kernel_stat ks;
  memset(&ks, 0, sizeof(ks));
  
  fs_node_t *node = vfs_open(resolved);
  if (!node) return -1;
  
  ks.st_dev = 1;
  ks.st_ino = node->inode;
  ks.st_nlink = 1;
  ks.st_uid = 0;
  ks.st_gid = 0;
  ks.st_rdev = 0;
  ks.st_size = node->length;
  ks.st_blksize = 4096;
  ks.st_blocks = (node->length + 511) / 512;
  
  if (node->flags & FS_DIRECTORY) {
    ks.st_mode = K_S_IFDIR | K_S_IRUSR | K_S_IWUSR | K_S_IXUSR
               | K_S_IRGRP | K_S_IXGRP | K_S_IROTH | K_S_IXOTH;
  } else {
    ks.st_mode = K_S_IFREG | K_S_IRUSR | K_S_IWUSR
               | K_S_IRGRP | K_S_IWGRP | K_S_IROTH | K_S_IWOTH;
  }
  
  ks.st_atime = (int64_t)(timer_ticks / 100);
  ks.st_mtime = ks.st_atime;
  ks.st_ctime = ks.st_atime;
  
  int result = copy_to_user(st, &ks, sizeof(ks)) < 0 ? -EFAULT : 0;
  vfs_node_put(node);
  return result;
}

int sys_fstat(int fd, struct kernel_stat *st) {
  if (!st) return -EFAULT;
  if (validate_user_write(st, sizeof(*st)) < 0) return -EFAULT;
  struct kernel_stat ks;
  memset(&ks, 0, sizeof(ks));
  
  if (fd >= 0 && fd <= 2) {
    // stdin/stdout/stderr — character device
    ks.st_dev = 1;
    ks.st_ino = fd + 1;
    ks.st_mode = K_S_IFCHR | K_S_IRUSR | K_S_IWUSR;
    ks.st_nlink = 1;
    ks.st_uid = 0;
    ks.st_gid = 0;
    ks.st_size = 0;
    ks.st_blksize = 512;
    ks.st_blocks = 0;
    ks.st_atime = (int64_t)(timer_ticks / 100);
    ks.st_mtime = ks.st_atime;
    ks.st_ctime = ks.st_atime;
    if (copy_to_user(st, &ks, sizeof(ks)) < 0) return -EFAULT;
    return 0;
  }
  
  kfile_t *fd_table = get_fd_table();
  if (fd < 0 || fd >= 32 || fd_table[fd].type != FD_TYPE_FILE) return -1;
  
  kfile_t *f = &fd_table[fd];
  ks.st_dev = 1;
  ks.st_ino = fd + 1;
  ks.st_nlink = 1;
  ks.st_uid = 0;
  ks.st_gid = 0;
  ks.st_rdev = 0;
  ks.st_size = file_size(f);
  ks.st_blksize = 4096;
  ks.st_blocks = (file_size(f) + 511) / 512;
  
  if (f->socket_ptr) {
    fs_node_t *node = (fs_node_t *)f->socket_ptr;
    if (node->flags & FS_DIRECTORY)
      ks.st_mode = K_S_IFDIR | K_S_IRUSR | K_S_IWUSR | K_S_IXUSR;
    else
      ks.st_mode = K_S_IFREG | K_S_IRUSR | K_S_IWUSR;
  }
  
  ks.st_atime = (int64_t)(timer_ticks / 100);
  ks.st_mtime = ks.st_atime;
  ks.st_ctime = ks.st_atime;
  
  if (copy_to_user(st, &ks, sizeof(ks)) < 0) return -EFAULT;
  return 0;
}

int sys_unlink(const char *path) {
  if (!path) return -EFAULT;
  char kpath[256];
  if (strncpy_from_user(kpath, path, 255) < 0) return -EFAULT;
  kpath[255] = '\0';
  const char *resolved = resolve_path(kpath);
  if (!resolved) return -1;
  return sdfs_delete(resolved);
}

int sys_mkdir(const char *path, uint32_t mode) {
  (void)mode;
  if (!path) return -EFAULT;
  char kpath[256];
  if (strncpy_from_user(kpath, path, 255) < 0) return -EFAULT;
  kpath[255] = '\0';
  const char *resolved = resolve_path(kpath);
  if (!resolved) return -1;
  return sdfs_create_dir(resolved);
}

int sys_rmdir(const char *path) {
  if (!path) return -EFAULT;
  char kpath[256];
  if (strncpy_from_user(kpath, path, 255) < 0) return -EFAULT;
  kpath[255] = '\0';
  const char *resolved = resolve_path(kpath);
  if (!resolved) return -1;
  return sdfs_delete(resolved);
}

int sys_rename(const char *old_path, const char *new_path) {
  if (!old_path || !new_path) return -EFAULT;
  char old_user_path[256], new_user_path[256];
  char old_resolved_path[512], new_resolved_path[512];
  if (strncpy_from_user(old_user_path, old_path, sizeof(old_user_path) - 1) < 0 ||
      strncpy_from_user(new_user_path, new_path, sizeof(new_user_path) - 1) < 0)
    return -EFAULT;
  old_user_path[sizeof(old_user_path) - 1] = '\0';
  new_user_path[sizeof(new_user_path) - 1] = '\0';

  const char *resolved = resolve_path(old_user_path);
  if (!resolved) return -1;
  strncpy(old_resolved_path, resolved, sizeof(old_resolved_path) - 1);
  old_resolved_path[sizeof(old_resolved_path) - 1] = '\0';

  resolved = resolve_path(new_user_path);
  if (!resolved) return -1;
  strncpy(new_resolved_path, resolved, sizeof(new_resolved_path) - 1);
  new_resolved_path[sizeof(new_resolved_path) - 1] = '\0';
  return sdfs_rename(old_resolved_path, new_resolved_path);
}

int sys_chdir(const char *path) {
  if (!path || !current_task) return -EFAULT;
  char kpath[256];
  if (strncpy_from_user(kpath, path, 255) < 0) return -EFAULT;
  kpath[255] = '\0';
  const char *resolved = resolve_path(kpath);
  if (!resolved) return -1;
  
  // Verify the path is a directory
  fs_node_t *node = vfs_open(resolved);
  if (!node) return -1;
  if (!(node->flags & FS_DIRECTORY)) {
    vfs_node_put(node);
    return -1;
  }
  
  strncpy(current_task->process->cwd, resolved, 255);
  current_task->process->cwd[255] = '\0';
  
  // Normalize: remove trailing slash unless it's just "/"
  int len = strlen(current_task->process->cwd);
  if (len > 1 && current_task->process->cwd[len - 1] == '/') {
    current_task->process->cwd[len - 1] = '\0';
  }
  vfs_node_put(node);
  
  return 0;
}

int sys_getcwd(char *buf, uint32_t size) {
  if (!buf || !current_task || size == 0) return -EFAULT;
  if (validate_user_write(buf, size) < 0) return -EFAULT;
  size_t len = strlen(current_task->process->cwd);
  if (len >= size) len = size - 1;
  if (copy_to_user(buf, current_task->process->cwd, len) < 0) return -EFAULT;
  if (len < size) {
    char zero = '\0';
    if (copy_to_user((char *)buf + len, &zero, 1) < 0) return -EFAULT;
  }
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
  } value;
  
  uint64_t ticks = timer_ticks;
  value.tv_sec = (int64_t)(ticks / 100);
  value.tv_usec = (int64_t)((ticks % 100) * 10000);
  if (copy_to_user(tv, &value, sizeof(value)) < 0) return -EFAULT;
  return 0;
}

// Native fixed-record directory enumeration. The public layout is
// include/liwus/dirent.h; records contain no kernel pointers.
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
  kfile_t *fd_table = get_fd_table();
  if (fd >= 32 || fd_table[fd].type == FD_TYPE_FREE) return -EBADF;
  if (fd_table[fd].type != FD_TYPE_FILE) return -ENOTDIR;
  if (count == 0) return 0;
  if (!buf || validate_user_write(buf, count) < 0) return -EFAULT;

  kfile_t *f = &fd_table[fd];
  if (!f->socket_ptr) return -EBADF;
  
  fs_node_t *node = (fs_node_t *)f->socket_ptr;
  if (!(node->flags & FS_DIRECTORY) || !node->readdir) return -ENOTDIR;
  
  // Use f->offset as directory entry index
  uint32_t *offset = file_offset(f);
  uint32_t index = *offset;
  uint32_t pos = 0;
  
  while (pos <= count && count - pos >= sizeof(struct kernel_dirent)) {
    struct dirent *vfs_dirent = readdir_fs(node, index);
    if (!vfs_dirent) break;

    size_t name_length = 0;
    while (name_length < sizeof(vfs_dirent->name) &&
           vfs_dirent->name[name_length]) name_length++;
    if (name_length == 0 || name_length == sizeof(vfs_dirent->name)) {
      *offset = index;
      return pos ? (int)pos : -EIO;
    }
    
    struct kernel_dirent kd;
    memset(&kd, 0, sizeof(kd));
    
    kd.d_ino = vfs_dirent->ino;
    kd.d_off = (int64_t)index + 1;
    kd.d_reclen = sizeof(struct kernel_dirent);
    fs_node_t *child = finddir_fs(node, vfs_dirent->name);
    if (child) {
      kd.d_type = (child->flags & FS_DIRECTORY) ? K_DT_DIR : K_DT_REG;
      vfs_node_put(child);
    }
    memcpy(kd.d_name, vfs_dirent->name, name_length);
    
    if (copy_to_user((char *)buf + pos, &kd, sizeof(kd)) < 0) {
      *offset = index;
      return pos ? (int)pos : -EFAULT;
    }
    
    pos += sizeof(struct kernel_dirent);
    index++;
  }
  
  // Update offset with how many entries we read
  *offset = index;
  
  if (pos == 0 && index < UINT32_MAX &&
      readdir_fs(node, index) != NULL && count < sizeof(struct kernel_dirent))
    return -EINVAL;
  return (int)pos;
}

int sys_dup(int oldfd) {
  kfile_t *fd_table = get_fd_table();
  if (oldfd < 0 || oldfd >= 32 || fd_table[oldfd].type == FD_TYPE_FREE) return -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      memcpy(&fd_table[i], &fd_table[oldfd], sizeof(kfile_t));
      sys_fd_get(&fd_table[i]);
      return i;
    }
  }
  return -1;
}

int sys_dup2(int oldfd, int newfd) {
  kfile_t *fd_table = get_fd_table();
  if (oldfd < 0 || oldfd >= 32 || newfd < 0 || newfd >= 32) return -1;
  if (fd_table[oldfd].type == FD_TYPE_FREE) return -1;
  if (newfd == oldfd) return newfd;
  if (fd_table[newfd].type != FD_TYPE_FREE) sys_close(newfd);
  memcpy(&fd_table[newfd], &fd_table[oldfd], sizeof(kfile_t));
  sys_fd_get(&fd_table[newfd]);
  return newfd;
}

int sys_pipe(int pipefd[2]) {
  if (!pipefd) return -EFAULT;
  if (validate_user_write(pipefd, 2 * sizeof(int)) < 0) return -EFAULT;
  kfile_t *fd_table = get_fd_table();
  pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
  if (!p) return -1;
  live_pipe_objects++;
  memset(p, 0, sizeof(pipe_t));
  wait_queue_init(&p->read_waiters);
  wait_queue_init(&p->write_waiters);

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
  p->references = 2;
  p->readers = 1;
  p->writers = 1;

  int fds[2];
  fds[0] = rfd;
  fds[1] = wfd;
  if (copy_to_user(pipefd, fds, sizeof(fds)) < 0) {
    sys_fd_put(&fd_table[rfd]);
    sys_fd_put(&fd_table[wfd]);
    return -EFAULT;
  }
  return 0;
}

// ============================================================
// mmap / munmap (POSIX)
// ============================================================// Flags e proteções padrão compatíveis com Linux (usados pelo newlib e
// por programas POSIX reais).
#define MAP_SHARED     0x01
#define MAP_PRIVATE    0x02
#define MAP_FIXED      0x10
#define MAP_ANONYMOUS  0x20
#define MAP_FAILED     ((uint64_t)-1)

#define PROT_NONE      0x0
#define PROT_READ      0x1
#define PROT_WRITE     0x2
#define PROT_EXEC      0x4

// Mapeia anonymous memory. Retorna o endereço virtual mapeado ou
// MAP_FAILED (-1) em erro.
uint64_t sys_mmap(void *addr, uint64_t length, int prot, int flags,
                  int fd, uint64_t offset) {
  (void)fd;
  (void)offset;
  if (!current_task) return MAP_FAILED;
  page_directory_t *mm = current_task->process->mm;
  if (!mm) return MAP_FAILED;
  if (!(flags & MAP_ANONYMOUS)) return MAP_FAILED; // suportado apenas anônimo
  if ((prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC))
    return MAP_FAILED;

  /* Se o usuário passou um hint explícito (MAP_FIXED ou addr != NULL),
   * garante que ele está dentro do espaço de endereço de usuário. */
  if (addr && !is_user_addr(addr)) return MAP_FAILED;

  // Alinha o tamanho para cima em páginas de 4 KB
  if (length > UINT64_MAX - 4095) return MAP_FAILED;
  uint64_t aligned_len = (length + 4095) & ~(uint64_t)4095;
  if (aligned_len == 0) aligned_len = 4096;
  if (aligned_len > USER_ADDR_MAX) return MAP_FAILED;

  // Determina o endereço base
  uint64_t base;
  if ((flags & MAP_FIXED) && addr) {
    base = (uint64_t)addr & ~(uint64_t)4095;
  } else {
    if (vma_find_gap_topdown(mm, current_task->process->mmap_top,
                             USER_ADDR_MIN, aligned_len, &base) < 0)
      return MAP_FAILED;
  }
  if (base + aligned_len < base || base + aligned_len > USER_ADDR_MAX)
    return MAP_FAILED;

  /* MAP_FIXED must not silently replace mappings and leak their frames. */
  for (uint64_t p = base; p < base + aligned_len; p += 4096) {
    if (validate_user_pointer((void *)p, 1) == 0) return MAP_FAILED;
  }

  uint64_t pte_flags = PTE_P | PTE_U | PTE_NX;
  if (prot & PROT_WRITE) pte_flags |= PTE_W;
  if (prot & PROT_EXEC) pte_flags &= ~((uint64_t)PTE_NX);
  if ((prot & PROT_READ) == 0 && (prot & PROT_EXEC) == 0)
    pte_flags &= ~((uint64_t)PTE_W);
  uint64_t alloc_flags = pte_flags | PTE_W;

  uint64_t i = 0;
  for (i = 0; i < aligned_len / 4096; i++) {
    void *phys = pmm_alloc_block();
    if (!phys) {
      // Desfaz mapeamentos já feitos
      for (uint64_t j = 0; j < i; j++) {
        vmm_unmap_page_in(mm, (void *)(base + j * 4096));
      }
      return MAP_FAILED;
    }
    memset(phys, 0, 4096);
    vmm_map_page_in(mm, phys, (void *)(base + (uint64_t)i * 4096),
                    alloc_flags);
    if (validate_user_pointer((void *)(base + i * 4096), 1) < 0) {
      pmm_free_block(phys);
      break;
    }
  }
  if (i != aligned_len / 4096) {
    for (uint64_t j = 0; j < i; j++)
      vmm_unmap_page_in(mm, (void *)(base + j * 4096));
    return MAP_FAILED;
  }

  uint32_t vma_prot = 0;
  if (prot & PROT_READ) vma_prot |= VMA_PROT_READ;
  if (prot & PROT_WRITE) vma_prot |= VMA_PROT_WRITE;
  if (prot & PROT_EXEC) vma_prot |= VMA_PROT_EXEC;
  uint32_t vma_flags = (flags & MAP_SHARED) ? VMA_SHARED : VMA_PRIVATE;
  vma_flags |= VMA_ANON;
  if (vma_insert(mm, base, base + aligned_len, vma_prot,
                 vma_flags, 0, 0) < 0) {
    for (uint64_t j = 0; j < aligned_len / PAGE_SIZE; j++)
      vmm_unmap_page_in(mm, (void *)(base + j * PAGE_SIZE));
    return MAP_FAILED;
  }

  /* The kernel initializes frames while writable, then exposes requested
   * user permissions. This remains valid with CR0.WP enabled. */
  for (uint64_t j = 0; j < aligned_len / PAGE_SIZE; j++) {
    if (vmm_set_page_flags_in(mm, (void *)(base + j * PAGE_SIZE),
                              pte_flags) < 0) {
      for (uint64_t rollback = 0; rollback < aligned_len / PAGE_SIZE; rollback++)
        vmm_unmap_page_in(mm, (void *)(base + rollback * PAGE_SIZE));
      vma_remove(mm, base, base + aligned_len);
      return MAP_FAILED;
    }
  }

  // Atualiza o marcador de região mmap apenas para alocações dinâmicas
  if (!((flags & MAP_FIXED) && addr)) {
    current_task->process->mmap_top = base;
  }

  return base;
}

// Desmapeia uma região mapeada por mmap (apenas páginas de 4 KB).
uint64_t sys_mouse_get(void *out);

int sys_munmap(void *addr, uint64_t length) {
  if (!current_task) return -1;
  page_directory_t *mm = current_task->process->mm;
  if (!mm) return -1;
  if (!addr || !is_user_addr(addr)) return -EFAULT;
  if (length > UINT64_MAX - 4095) return -EINVAL;
  uint64_t base = (uint64_t)addr & ~(uint64_t)4095;
  uint64_t aligned_len = (length + 4095) & ~(uint64_t)4095;
  if (!aligned_len) return -EINVAL;
  if (base + aligned_len > USER_ADDR_MAX) return -EFAULT;
  if (vma_remove(mm, base, base + aligned_len) < 0)
    return -ENOMEM;
  for (uint64_t i = 0; i < aligned_len / 4096; i++) {
    vmm_unmap_page_in(mm, (void *)(base + i * 4096));
  }
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
    case 10: {
      /* keyboard_get_event writes directly to its arg, which may be a
       * user pointer.  Validate and gate it so we don't scribble on
       * arbitrary user/kernel memory. */
      struct {
        uint8_t scancode;
        int pressed;
      } kev, *out = (void *)regs->rdi;
      if (regs->rdi && validate_user_write(out, sizeof(kev)) == 0) {
        int got = keyboard_get_event(&kev);
        if (got) {
          if (copy_to_user(out, &kev, sizeof(kev)) < 0) regs->rax = (uint64_t)-1;
          else regs->rax = 1;
        } else {
          regs->rax = 0;
        }
      } else {
        regs->rax = (uint64_t)-1;
      }
      break;
    }
    case 11: regs->rax = keyboard_is_pressed((uint8_t)regs->rdi); break;
    case 37: regs->rax = sys_mouse_get((void *)regs->rdi); break;
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
    case 34: regs->rax = task_snapshot((void *)regs->rdi, (int)regs->rsi); break;
    case 35: regs->rax = sys_mmap((void *)regs->rdi, regs->rsi, (int)regs->rdx,
                                 (int)regs->rcx, (int)regs->r8, regs->r9); break;
    case 36: regs->rax = sys_munmap((void *)regs->rdi, regs->rsi); break;
    case 38: regs->rax = sys_rename((const char *)regs->rdi,
                                    (const char *)regs->rsi); break;
    case 39: regs->rax = task_set_foreground((int)regs->rdi); break;
    case 40: regs->rax = task_create_job(); break;
    case 41: regs->rax = task_set_foreground_job((int)regs->rdi); break;
  }
}

// ============================================================
// Mouse state for userspace (SYS_MOUSE_GET = 37)
// ============================================================

struct liw_mouse_state {
  int32_t x;
  int32_t y;
  int32_t left;
  int32_t right;
};

uint64_t sys_mouse_get(void *out) {
  if (!out) return (uint64_t)-1;
  if (validate_user_write(out, sizeof(struct liw_mouse_state)) < 0) return (uint64_t)-1;

  struct liw_mouse_state st;
  st.x = get_mouse_x();
  st.y = get_mouse_y();
  st.left = is_left_clicked() ? 1 : 0;
  st.right = is_right_clicked() ? 1 : 0;

  if (copy_to_user(out, &st, sizeof(st)) < 0) return (uint64_t)-1;
  return 0;
}

static void syscall_wrmsr(uint32_t msr, uint64_t value) {
  uint32_t low = (uint32_t)value;
  uint32_t high = (uint32_t)(value >> 32);
  asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

extern void linux_syscall_entry(void);

void init_syscall_cpu(void) {
  uint32_t eax = 0x80000000U, ebx, ecx, edx;
  asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  if (eax < 0x80000001U) return;
  eax = 0x80000001U;
  asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  if (!(edx & (1U << 11))) return; /* SYSCALL/SYSRET */

  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080U));
  syscall_wrmsr(0xC0000080U, (((uint64_t)hi << 32) | lo) | 1ULL);
  syscall_wrmsr(0xC0000081U, ((uint64_t)0x1B << 48) |
                              ((uint64_t)0x08 << 32));
  syscall_wrmsr(0xC0000082U, (uint64_t)linux_syscall_entry);
  syscall_wrmsr(0xC0000084U, (1ULL << 8) | (1ULL << 9) |
                              (1ULL << 10) | (1ULL << 14) | (1ULL << 18));
}

void init_syscalls() {
  memset(fd_table, 0, sizeof(fd_table));
  kernel_termios.c_cc[VMIN] = 1;
  kernel_termios.c_cc[VTIME] = 0;
  init_syscall_cpu();
}
