#include "liwus/syscall.h"
#include <stddef.h>

static size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    const char *msg = "Hello from LIWUS OS!\n";
    liw_write(1, msg, strlen(msg));
    return 0;
}
