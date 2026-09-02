#ifndef KERNEL_TEST
#include "framework.h"
#include <unistd.h>

int test_getpid(void) {
    int pid = getpid();
    ASSERT(pid > 0, "getpid returns > 0");
    PASS("test_getpid");
}

int test_chdir_getcwd(void) {
    char buf[256];
    char *r = getcwd(buf, sizeof(buf));
    ASSERT(r != NULL, "getcwd succeeds");

    int ret = chdir("/");
    ASSERT(ret == 0, "chdir /");

    r = getcwd(buf, sizeof(buf));
    ASSERT(r != NULL, "getcwd after chdir");

    PASS("test_chdir_getcwd");
}

int test_sbrk(void) {
    void *old = sbrk(0);
    ASSERT(old != (void *)-1, "sbrk(0) ok");

    void *new_brk = sbrk(4096);
    ASSERT(new_brk != (void *)-1, "sbrk(4096) ok");
    ASSERT((unsigned long)new_brk == (unsigned long)old + 4096, "brk advanced by 4096");

    PASS("test_sbrk");
}

#endif
