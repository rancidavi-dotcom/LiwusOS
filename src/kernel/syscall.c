#include "syscall.h"
#include "fat32.h"
#include "initrd.h"
#include "io.h"
#include "kheap.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "tcp.h"
#include "video.h"
#include "vmm.h"
#include "vfs.h"

extern uint32_t elf_load_file(void *elf_data);
extern int graphics_exclusive_active(void);
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
  void *addr = initrd_get_file(filename, &size);
  if (!addr) addr = fat32_read_file_path(filename, &size);
  if (!addr) {
    launch_last_error = "arquivo nao encontrado";
    return -1;
  }

  uint32_t entry = elf_load_file(addr);
  if (entry == 0) {
    launch_last_error = "formato ELF invalido";
    return -1;
  }

  // 1. Montar 64KB de stack em VA alta de userland (evita colisao com paginas do kernel)
  uint32_t stack_size = 64 * 1024;
  uint32_t stack_top = 0xC0000000;
  uint32_t stack_base = stack_top - stack_size;
  for (uint32_t vaddr = stack_base; vaddr < stack_top; vaddr += 4096) {
    void *phys = kmalloc_a(4096);
    vmm_map_page(phys, (void *)vaddr, 0x07);
    memset((void *)vaddr, 0, 4096);
  }

  // 2. Preparar ponteiro da pilha (topo)
  uint8_t *esp_bytes = (uint8_t *)stack_top;

  int argc = 0;
  if (argv) {
    while (argv[argc] && argc < (int)max_args) argc++;
  }

  // 3. Copiar strings dos argumentos para o topo da pilha
  uint32_t arg_ptrs[16];
  for (int i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    esp_bytes -= len;
    memcpy(esp_bytes, argv[i], len);
    arg_ptrs[i] = (uint32_t)esp_bytes;
  }

  // Alinhamento de 16 bytes (padrão ABI)
  uint32_t *esp = (uint32_t *)((uint32_t)esp_bytes & ~0xF);

  // 4. Montar o array argv (ponteiros para as strings que acabamos de copiar)
  esp--; *esp = 0; // NULL terminator
  for (int i = argc - 1; i >= 0; i--) {
    esp--; *esp = arg_ptrs[i];
  }
  uint32_t argv_ptr_addr = (uint32_t)esp;

  // 5. Colocar argumentos para o main(int argc, char **argv)
  // O crt0.s fará 'call main', então a pilha deve estar pronta para isso.
  esp--; *esp = 0;            // envp (NULL)
  esp--; *esp = argv_ptr_addr; // argv
  esp--; *esp = (uint32_t)argc; // argc

  // Debug Serial
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
  create_user_task_named((uint32_t)ret, execve_new_esp, filename);
  execve_new_esp = 0;
  return 0;
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
  if (whence == 0) fd_table[fd].offset = offset;
  else if (whence == 1) fd_table[fd].offset += offset;
  return fd_table[fd].offset;
}

void syscall_handler(registers_t *regs) {
  uint32_t call_num = regs->eax;
  switch (call_num) {
    case 1: sys_exit(regs->ebx); break;
    case 2: regs->eax = fork_process(regs); break;
    case 3: regs->eax = sys_read(regs->ebx, (void *)regs->ecx, regs->edx); break;
    case 4: regs->eax = sys_write(regs->ebx, (void *)regs->ecx, regs->edx); break;
    case 5: regs->eax = sys_open((char *)regs->ebx, regs->ecx); break;
    case 6: regs->eax = sys_close(regs->ebx); break;
    case 7: regs->eax = sys_waitpid(regs->ebx, (int *)regs->ecx, regs->edx); break;
    case 11: regs->eax = sys_execve((char *)regs->ebx, (char **)regs->ecx, (char **)regs->edx);
             if (regs->eax != (uint32_t)-1) regs->esp = execve_new_esp;
             break;
    case 19: regs->eax = sys_lseek(regs->ebx, regs->ecx, regs->edx); break;
    case 45: { // brk (gerenciamento de heap)
        uint32_t new_brk = regs->ebx;
        uint32_t old_brk = current_task->heap_end;

        if (new_brk == 0) {
            regs->eax = old_brk;
        } else if (new_brk <= old_brk) {
            // Apenas diminui o limite (ou mantém)
            current_task->heap_end = new_brk;
            regs->eax = new_brk;
        } else {
            // Aumenta o heap: mapeia novas páginas se necessário
            uint32_t start_page = (old_brk + 0xFFF) & 0xFFFFF000;
            uint32_t end_page = (new_brk + 0xFFF) & 0xFFFFF000;

            for (uint32_t p = start_page; p < end_page; p += 4096) {
                void *phys = kmalloc_a(4096); // Aloca frame físico
                vmm_map_page(phys, (void *)p, 0x07); // User, RW, Present
                memset((void*)p, 0, 4096);
            }
            current_task->heap_end = new_brk;
            regs->eax = new_brk;
        }
        break;
    }
    default: break;
  }
}

void init_syscalls() {
  memset(fd_table, 0, sizeof(fd_table));
}
