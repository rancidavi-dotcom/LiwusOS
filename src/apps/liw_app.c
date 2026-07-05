// liw.c - LiwusOS Package Manager
// Compilado como parte do kernel ou como app nativo?
// App nativo é o ideal, mas precisa de libc.
// O "syscall.c" expõe socket/connect/etc.
// A "libliw" deve encapsular isso.

// Como não tenho um toolchain de userspace separado configurado pra valer (só o
// kernel build), vou criar este código para ser compilado *junto com o kernel*
// por enquanto, mas executado como parte de um test ou Shell Command. Espera...
// o user quer "app nativo". O "test.c" foi compilado com `gcc -m32 -nostdlib
// ...`. Posso fazer o mesmo para `liw`.

// Vou assumir que `liw` será um executável separado.

/* liw.c */
// We need headers. Since we are bare metal app:
typedef unsigned int size_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

#define NULL ((void *)0)

// Syscalls declarations (User space side)
int syscall0(int num);
int syscall1(int num, int p1);
int syscall2(int num, int p1, int p2);
int syscall3(int num, int p1, int p2, int p3);
int syscall4(int num, int p1, int p2, int p3, int p4);

// Syscall Numbers
#define SYS_EXIT 1
#define SYS_WRITE 4
#define SYS_OPEN 5
// #define SYS_CLOSE 6
#define SYS_EXECVE 11
#define SYS_SOCKET 12
#define SYS_CONNECT 13
#define SYS_SEND 14
#define SYS_RECV 15

// Wrappers
void exit(int status) { syscall1(SYS_EXIT, status); }
int write(int fd, const char *buf, int count) {
  return syscall3(SYS_WRITE, fd, (int)buf, count);
}
int socket(int domain, int type, int proto) {
  return syscall3(SYS_SOCKET, domain, type, proto);
}
int connect(int sockfd, uint32_t ip, uint16_t port) {
  // Manual sockaddr
  uint8_t addr[16];
  addr[0] = 0;
  addr[1] = 2;                                           // AF_INET
  *(uint16_t *)(addr + 2) = ((port >> 8) | (port << 8)); // htons
  *(uint32_t *)(addr + 4) = ((ip >> 24) | ((ip >> 8) & 0xFF00) |
                             ((ip << 8) & 0xFF0000) | (ip << 24)); // htonl
  return syscall3(SYS_CONNECT, sockfd, (int)addr, 16);
}
int send(int sockfd, void *buf, int len) {
  return syscall4(SYS_SEND, sockfd, (int)buf, len, 0);
}
int recv(int sockfd, void *buf, int len) {
  return syscall4(SYS_RECV, sockfd, (int)buf, len, 0);
}

int strlen(const char *s) {
  int len = 0;
  while (s[len])
    len++;
  return len;
}
void print(const char *s) { write(1, s, strlen(s)); }

// HTTP GET
void http_get(const char *host, const char *path) {
  print("Resolving host...\n");
  // Hardcoded IP for "rancidavi-dotcom" (GitHub) is obviously impossible
  // without DNS. I'll use a hardcoded IP for a test server or assume DNS
  // resolves in kernel magic (which it doesn't). Let's rely on
  // `net_resolve_host` inside kernel? No, userspace must pass IP. Hack: User
  // passes IP in argv? Let's hardcode an IP for now: 185.199.108.133
  // (raw.githubusercontent.com)
  uint32_t ip = 0xB9C76C85; // 185.199.108.133
  // Or 10.0.2.2 (Gateway/Host)

  print("Connecting to GitHub (Mock IP)...\n");
  int fd = socket(2, 1, 0);
  if (fd < 0) {
    print("Socket failed\n");
    return;
  }

  if (connect(fd, ip, 80) < 0) {
    print("Connect failed\n");
    return;
  }
  print("Connected!\n");

  char req[512];
  // Simple logic to build request
  // "GET /path HTTP/1.1\r\nHost: host\r\n\r\n"
  // sprintf not available. manual copy.
  // TODO

  char *request = "GET / HTTP/1.1\r\nHost: raw.githubusercontent.com\r\n\r\n";
  send(fd, request, strlen(request));
  print("Sent Request\n");

  char buf[1024];
  while (1) {
    int r = recv(fd, buf, 1024);
    if (r <= 0)
      break;
    // Print progress
    print(".");
    // Write to file? syscall not available for write_file yet, only stdout.
    // We need SYS_WRITE_FILE syscall pointing to FAT32 Write.
  }
  print("\nDone.\n");
}

void _start() {
  print("LiwusOS Package Manager v0.1\n");
  // TODO: Parse args
  http_get("github.com", "/");
  exit(0);
}

// Inline assembly for syscalls
int syscall0(int num) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num));
  return ret;
}
int syscall1(int num, int p1) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(p1));
  return ret;
}
int syscall2(int num, int p1, int p2) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(p1), "c"(p2));
  return ret;
}
int syscall3(int num, int p1, int p2, int p3) {
  int ret;
  asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(p1), "c"(p2), "d"(p3));
  return ret;
}
int syscall4(int num, int p1, int p2, int p3, int p4) {
  int ret;
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(num), "b"(p1), "c"(p2), "d"(p3), "S"(p4));
  return ret;
}
