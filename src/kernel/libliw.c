#include "libliw.h"
#include "string.h"
#include "video.h"

// Protótipos de utilitários internos que podem não estar em headers
extern char *itoa(int value, char *str, int base);

int liw_read_manifest(const char *filename, liw_manifest_t *out_manifest) {
  if (!filename || !out_manifest)
    return -1;
  return 0; // Success mock
}

void print(const char *s) {
  static int y = 400;
  static int x = 10;
  draw_string(x, y, (char *)s, 0x00FF00);
  // Simples newline handling
  if (strchr(s, '\n')) {
    y += 16;
  }
}

void print_int(int n) {
  char buf[32];
  itoa(n, buf, 10);
  print(buf);
}

// Emitting syscalls via inline assembly
int syscall_fork() {
  int a;
  asm volatile("mov $2, %%eax; int $0x80" : "=a"(a));
  return a;
}

int syscall_waitpid(int pid, int *status, int options) {
  int a;
  asm volatile(
      "mov $7, %%eax; mov %1, %%ebx; mov %2, %%ecx; mov %3, %%edx; int $0x80"
      : "=a"(a)
      : "r"(pid), "r"(status), "r"(options));
  return a;
}

void syscall_exit(int status) {
  asm volatile("mov $1, %%eax; mov %0, %%ebx; int $0x80" : : "r"(status));
  while (1)
    ;
}

uint32_t syscall_brk(uint32_t addr) {
  uint32_t a;
  asm volatile("mov $45, %%eax; mov %1, %%ebx; int $0x80"
               : "=a"(a)
               : "r"(addr));
  return a;
}
