#include <stdint.h>
#include <stddef.h>
#include <errno.h>

int errno = 0;

// Syscalls básicas do PLANO
int write(int fd, const void *buf, uint32_t count) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(4), "b"(fd), "c"(buf), "d"(count));
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

void _exit(int code) {
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(code));
    while(1);
}

// Removido exit() daqui para evitar conflito com stdlib.c

int read(int fd, void *buf, uint32_t count) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(3), "b"(fd), "c"(buf), "d"(count));
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

int open(const char *name, int flags) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(5), "b"(name), "c"(flags));
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

int close(int fd) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(6), "b"(fd));
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

long lseek(int fd, long offset, int whence) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(19), "b"(fd), "c"(offset), "d"(whence));
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

int __liw_sys_save_file(const char *name, void *buffer, uint32_t size) {
    int ret;
    // Syscall customizada para salvar arquivo no initrd ou disco
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(20), "b"(name), "c"(buffer), "d"(size));
    return ret;
}

// Syscalls extras necessárias para os apps existentes (Doom, etc)
int __liw_sys_get_ticks() {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(7));
    return ret;
}

void* __liw_sys_brk(void* addr) {
    void* ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(2), "b"(addr));
    return ret;
}

static void* current_brk = NULL;
void* sbrk(intptr_t increment) {
    if (current_brk == NULL) {
        current_brk = __liw_sys_brk(NULL);
    }
    void* old_brk = current_brk;
    void* new_brk = (void*)((uintptr_t)current_brk + increment);
    if (__liw_sys_brk(new_brk) == (void*)-1) {
        errno = ENOMEM;
        return (void*)-1;
    }
    current_brk = new_brk;
    return old_brk;
}

int fork() { return -1; }
int waitpid(int pid, int *status, int options) { (void)pid; (void)status; (void)options; return -1; }

int __liw_sys_get_key_event(void* ev) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(10), "b"(ev));
    return ret;
}

int __liw_sys_key_state(int key) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(11), "b"(key));
    return ret;
}

void __liw_sys_get_fb_info(void* info) {
    __asm__ volatile ("int $0x80" : : "a"(12), "b"(info));
}

void __liw_sys_present_fb() {
    __asm__ volatile ("int $0x80" : : "a"(13));
}
