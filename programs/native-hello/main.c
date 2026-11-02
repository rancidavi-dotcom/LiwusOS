#include "liwus/syscall.h"
#include <stddef.h>

static size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void _start(void) {
    const char *msg = "Hello from native LiwusOS ABI!\n";
    liw_write(1, msg, strlen(msg));
    int pid = liw_getpid();
    (void)pid;
    liw_exit(0);
}
