#ifndef LIBLIW_H
#define LIBLIW_H

// LiwusOS Standard Library
// Compatible with GCC cross-compiler or -m32 host compiler

#define NULL ((void *)0)

typedef unsigned int size_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

// Syscall Numbers
#define SYS_EXIT 1
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_EXECVE 11
#define SYS_SOCKET 12
#define SYS_CONNECT 13
#define SYS_SEND 14
#define SYS_RECV 15
#define SYS_SAVE_FILE 16

// Syscall Wrappers (Inline Assembly)
static inline int syscall0(int num) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num));
  return ret;
}

static inline int syscall1(int num, int p1) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(p1));
  return ret;
}

static inline int syscall2(int num, int p1, int p2) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(p1), "c"(p2));
  return ret;
}

static inline int syscall3(int num, int p1, int p2, int p3) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(p1), "c"(p2), "d"(p3));
  return ret;
}

static inline int syscall4(int num, int p1, int p2, int p3, int p4) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(num), "b"(p1), "c"(p2), "d"(p3), "S"(p4));
  return ret;
}

// Standard Functions
static inline void exit(int status) {
  syscall1(SYS_EXIT, status);
  while (1)
    ;
}

static inline int print(const char *str) {
  int len = 0;
  while (str[len])
    len++;
  return syscall3(SYS_WRITE, 1, (int)str, len);
}

#endif
