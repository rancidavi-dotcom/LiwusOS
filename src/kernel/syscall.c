#include "syscall.h"
#include "initrd.h"
#include "kheap.h"
#include "liw_format.h" // Include header
#include "string.h"
#include "task.h"
#include "video.h"
#include "vmm.h"

#include "net.h"
#include "netstack.h" // For globals htons/ntohl
#include "tcp.h"

int sys_save_file(const char *name, void *buffer, uint32_t size);
extern task_t *current_task;

static void *syscalls[20];

void sys_exit(int status) {
  draw_string(10, 520, "SYS_EXIT CALLED", 0xFF0000);
  while (1)
    ;
}

int sys_write(int fd, const char *buf, int count) {
  static int y = 200;
  if (fd == 1 || fd == 2) {
    draw_string(400, y, "Write:", 0x00FF00);
    draw_string(460, y, buf, 0xFFFFFF);
    y += 16;
    return count;
  }
  return 0;
}

extern int fork_process(registers_t *regs);
extern uint32_t elf_load_file(void *file_buffer);

extern void start_user_mode(uint32_t entry, uint32_t stack_ptr);

int sys_execve(const char *filename, char *const argv[], char *const envp[]) {
  // 1. Find File (Try variants)
  uint32_t size;
  void *file = initrd_get_file(filename, &size);

  if (!file) {
    // Try with ./ prefix
    char buf[64];
    strcpy(buf, "./");
    strcat(buf, filename);
    file = initrd_get_file(buf, &size);
  }

  if (!file) {
    draw_string(10, 540, "Exec: File not found", 0xFF0000);
    return -1;
  }

  // Check for LIW Bundle
  liw_header_t *liw = (liw_header_t *)file;
  void *elf_ptr = file;

  if (liw->magic == LIW_MAGIC) {
    draw_string(10, 560, "Exec: Detected .liw bundle!", 0x00FF00);
    elf_ptr = (void *)((uint32_t)file + liw->entry_offset);
  }

  uint32_t entry = elf_load_file(elf_ptr);
  if (entry == 0) {
    draw_string(10, 540, "Exec: ELF Load Failed", 0xFF0000);
    return -1;
  }

  // Set initial heap (above loaded ELF)
  // Simple heuristic: 4MB above load point
  current_task->heap_start = 0x40000000; // Assume standard load
  current_task->heap_end = 0x40000000;

  // 2. Setup User Stack with Arguments
  // We need to construct the stack at 0xBFFFFFFF (downwards)
  // Layout: [RetAddr? Stub] [argc] [argv ptr] [NULL] ... [Arg Strings]

  // Note: We are in kernel mode. We need to write to user addresses.
  // Assuming 0xBFFFFFFF is mapped. (It is, see syscall_handler map loop).
  // But wait, syscall_handler maps NEW stack pages FOR THE NEW PROCESS context?
  // Actually, sys_execve runs in the CURRENT process context (replacing it).
  // The memory map is already active?
  // No, `elf_load_file` likely overwrites current process memory or we need to
  // clear/map new. `elf_load_file` snippet showed mapping pages. We should map
  // stack pages HERE too.

  uint32_t stack_top = 0xBFFFFFFF;
  // Map stack (ensure it exists)
  // syscall_handler was doing it AFTER return, which is weird for execve.
  // Usually execve replaces the image and never returns to the old code.
  // We should set up everything here and jump to start_user_mode OURSELVES.
  // We should NOT return to syscall_handler because syscall_handler tries to
  // return to old EIP. But wait, generic syscall_handler might expect return.
  // Standard way: modify `regs` in place and return.
  // But we need to setup stack content.

  // Let's do the mapping here to be sure.
  for (int i = 0; i < 4; i++) {
    // Checking if mapped? Just map.
    // Assuming identity or simple allocator.
    void *phys = kmalloc_a(4096);
    // vmm_map_page matches generic signature
    vmm_map_page(phys, (void *)(0xBFFFF000 - i * 4096), 0x7);
    memset((void *)(0xBFFFF000 - i * 4096), 0, 4096);
  }

  // Count argc
  int argc = 0;
  if (argv)
    while (argv[argc])
      argc++;

  char *esp = (char *)stack_top;
  uint32_t argv_ptrs[32]; // limit args

  // Copy strings to stack
  if (argv) {
    for (int i = argc - 1; i >= 0; i--) {
      int len = strlen(argv[i]) + 1;
      esp -= len;
      memcpy(esp, argv[i], len);
      argv_ptrs[i] = (uint32_t)esp;
    }
  }

  // Align
  esp = (char *)((uint32_t)esp & ~3);

  // Push argv pointers
  esp -= 4; // NULL terminator
  *(uint32_t *)esp = 0;

  for (int i = argc - 1; i >= 0; i--) {
    esp -= 4;
    *(uint32_t *)esp = argv_ptrs[i];
  }
  uint32_t argv_base = (uint32_t)esp;

  // Push argc, argv
  esp -= 4;
  *(uint32_t *)esp = argv_base;
  esp -= 4;
  *(uint32_t *)esp = argc;

  // Push dummy return address (or exit)
  // esp -= 4; *(uint32_t*)esp = 0; // if main returns

  // NOW: We must return 0 to syscall handler to indicate success?
  // OR we hijack execution flow.
  // If we return, syscall_handler will restore registers.
  // We need to tell syscall_handler to change EIP and ESP.
  // We can't access regs here easily unless passed?
  // syscall_handler called us: `sys_execve(filename, argv, envp)`
  // It checks return value.
  // If ret != -1, it sets `regs->eip = ret`.
  // AND it sets `regs->esp`.

  // PROBLEM: syscall_handler overrides our stack setup!
  // Look at syscall_handler code in view:
  /*
     if (ret != -1) {
       regs->eip = (uint32_t)ret;
       ...
       regs->esp = stack_base + 4096 - 16;
       ...
     }
  */
  // It hardcodes ESP reset! This DESTROYS our arguments.

  // FIX: We must change `sys_execve` to return the new ESP as well?
  // Or modify `syscall_handler` to NOT reset ESP if we already did.
  // Since I can edit `syscall.c` fully, I will change `syscall_handler` logic
  // too.

  // I will store the new ESP in a global or return a struct?
  // Easier: Make `sys_execve` take `registers_t *regs`?
  // No, signature is standard.

  // I will use a global `current_new_esp` to pass this info to handler,
  // OR I will modify syscall_handler heavily.

  // Let's modify syscall_handler logic in the file replace.
  // But wait, `sys_execve` returns `int`.
  // I will use `sys_execve` to JUST load and return entry.
  // BUT `syscall_handler` needs to copy args.
  // `syscall_handler` doesn't know strings from userspace well (pointers
  // valid?) Yes, shared address space for now.

  // BETTER: Move all logic to `syscall_handler` block for execve?
  // No, keep `sys_execve` helper.

  // Strategy:
  // 1. `sys_execve` does loading AND stack setup.
  // 2. `sys_execve` returns entry point.
  // 3. We add a global `uint32_t execve_new_esp` that `sys_execve` sets.
  // 4. `syscall_handler` uses `execve_new_esp` if set.

  extern uint32_t execve_new_esp;
  execve_new_esp = (uint32_t)esp;

  return entry;
}

uint32_t execve_new_esp = 0;

// File Descriptor Types
#define FD_TYPE_FREE 0
#define FD_TYPE_FILE 1
#define FD_TYPE_SOCKET 2

typedef struct {
  int type;
  uint32_t base_addr; // For files
  uint32_t size;      // For files
  uint32_t offset;    // For files
  void *socket_ptr;   // For sockets (tcp_socket_t*)
} kfile_t;

static kfile_t fd_table[32];

void init_fd_table() {
  memset(fd_table, 0, sizeof(fd_table));
  // RESERVED 0,1,2
  fd_table[0].type = FD_TYPE_FILE; // stdin
  fd_table[1].type = FD_TYPE_FILE; // stdout
  fd_table[2].type = FD_TYPE_FILE; // stderr
}

int sys_socket(int domain, int type, int protocol) {
  (void)domain;
  (void)type;
  (void)protocol;
  // Find free FD
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      fd_table[i].type = FD_TYPE_SOCKET;
      fd_table[i].socket_ptr = NULL;
      return i;
    }
  }
  return -1;
}

int sys_connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
  (void)addrlen;
  if (sockfd < 3 || sockfd >= 32)
    return -1;
  if (fd_table[sockfd].type != FD_TYPE_SOCKET)
    return -1;

  // sockaddr hack: assume struct { short family; unsigned short port; unsigned
  // int ip; ... } Mas userspace pode passar struct bruta. Vamos assumir layout:
  // family(2), port(2), ip(4).
  uint8_t *ptr = (uint8_t *)addr;
  uint16_t port = ntohs(*(uint16_t *)(ptr + 2));
  uint32_t ip = ntohl(*(uint32_t *)(ptr + 4));

  tcp_socket_t *sock = tcp_connect(ip, port);
  if (!sock)
    return -1;

  fd_table[sockfd].socket_ptr = sock;
  return 0;
}

int sys_send(int sockfd, const void *buf, size_t len, int flags) {
  (void)flags;
  if (sockfd < 3 || sockfd >= 32)
    return -1;
  if (fd_table[sockfd].type != FD_TYPE_SOCKET)
    return -1;

  tcp_socket_t *sock = (tcp_socket_t *)fd_table[sockfd].socket_ptr;
  if (!sock)
    return -1;

  tcp_send(sock, (const uint8_t *)buf, len);
  return len;
}

int sys_recv(int sockfd, void *buf, size_t len, int flags) {
  (void)flags;
  if (sockfd < 3 || sockfd >= 32)
    return -1;
  if (fd_table[sockfd].type != FD_TYPE_SOCKET)
    return -1;

  tcp_socket_t *sock = (tcp_socket_t *)fd_table[sockfd].socket_ptr;
  if (!sock)
    return -1;

  // TODO: Implement blocking recv in tcp.c and expose it
  // int received = tcp_receive(sock, (uint8_t*)buf, len);
  // return received;
  return 0; // Stub
}

int sys_open(const char *filename, int flags) {
  (void)flags;
  int fd = -1;
  for (int i = 3; i < 32; i++) {
    if (fd_table[i].type == FD_TYPE_FREE) {
      fd = i;
      break;
    }
  }
  if (fd == -1)
    return -1;

  uint32_t size;
  void *addr = initrd_get_file(filename, &size);
  if (!addr)
    return -1;

  fd_table[fd].type = FD_TYPE_FILE;
  fd_table[fd].base_addr = (uint32_t)addr;
  fd_table[fd].size = size;
  fd_table[fd].offset = 0;

  return fd;
}

int sys_close(int fd) {
  if (fd < 3 || fd >= 32)
    return -1;
  if (fd_table[fd].type == FD_TYPE_SOCKET) {
    tcp_close((tcp_socket_t *)fd_table[fd].socket_ptr);
  }
  fd_table[fd].type = FD_TYPE_FREE;
  return 0;
}

int sys_read(int fd, void *buf, int count) {
  if (fd < 0 || fd >= 32)
    return -1;

  if (fd_table[fd].type == FD_TYPE_SOCKET) {
    return sys_recv(fd, buf, count, 0);
  }

  if (fd_table[fd].type != FD_TYPE_FILE)
    return -1;

  if (fd == 0)
    return 0;

  kfile_t *file = &fd_table[fd];
  if (file->offset >= file->size)
    return 0;

  int to_read = count;
  if (file->offset + count > file->size) {
    to_read = file->size - file->offset;
  }

  memcpy(buf, (void *)(file->base_addr + file->offset), to_read);
  file->offset += to_read;
  return to_read;
}

// Update syscall table
void init_syscalls() {
  init_fd_table();
  syscalls[0] = 0;
  syscalls[1] = &sys_exit_process;
  syscalls[3] = &sys_read;
  syscalls[4] = &sys_write;
  syscalls[5] = &sys_open;
  syscalls[6] = &sys_close;
  syscalls[7] = &sys_waitpid;
  syscalls[11] = &sys_execve;
  syscalls[45] = &sys_brk;
  // New Network Syscalls (Linux numbers usually different but...)
  // socket(359?), connect(362?)...
  // I'll pick arbitrary:
  syscalls[12] = &sys_socket;
  syscalls[13] = &sys_connect;
  syscalls[14] = &sys_send;
  syscalls[15] = &sys_recv;
  syscalls[16] = &sys_save_file; // New
}

// Implementation of sys_save_file
#include "fat32.h"
int sys_save_file(const char *name, void *buffer, uint32_t size) {
  // Create logic: Try creating, ignore error if exists (overwrite)
  fat32_create_file(name);
  // Write content
  return fat32_write_file(name, (uint8_t *)buffer, size);
}

void syscall_handler(registers_t *regs) {
  if (regs->eax >= 20)
    return;

  // Handle fork (2)
  if (regs->eax == 2) {
    int pid = fork_process(regs);
    regs->eax = pid;
    return;
  }

  // Handle execve (11)
  if (regs->eax == 11) {
    const char *filename = (const char *)regs->ebx;
    char **argv = (char **)regs->ecx;
    char **envp = (char **)regs->edx;

    int ret = sys_execve(filename, argv, envp);

    if (ret != -1) {
      // Sucesso! ret é o entry point.
      regs->eip = (uint32_t)ret;

      // Stack was setup by sys_execve
      extern uint32_t execve_new_esp;
      if (execve_new_esp != 0) {
        regs->esp = execve_new_esp;
        execve_new_esp = 0; // Reset
      } else {
        // Fallback (old simplified logic)
        uint32_t stack_base = 0xBFFFF000;
        regs->esp = stack_base + 4096 - 16;
      }

      regs->ebp = 0;

      // Reset segments to User Mode
      regs->cs = 0x1B;
      regs->ds = 0x23;
      // es, fs, gs -> ds

      regs->ss = 0x23;
    } else {
      regs->eax = -1;
    }
    return;
  }

  void *location = syscalls[regs->eax];
  if (!location)
    return;

  int ret;
  asm volatile("push %1; \
         push %2; \
         push %3; \
         push %4; \
         push %5; \
         call *%6; \
         add $20, %%esp;"
               : "=a"(ret)
               : "r"(regs->edi), "r"(regs->esi), "r"(regs->edx), "r"(regs->ecx),
                 "r"(regs->ebx), "r"(location));

  regs->eax = ret;
}
