#ifndef KERNEL_TEST
#include "framework.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int test_open_close(void) {
    mkdir("/sandbox", 0755);
    int fd = open("/sandbox/test_oc.txt", O_CREAT | O_WRONLY, 0644);
    ASSERT(fd >= 0, "open for write");

    close(fd);

    fd = open("/sandbox/test_oc.txt", O_RDONLY);
    ASSERT(fd >= 0, "open for read");
    close(fd);

    unlink("/sandbox/test_oc.txt");
    rmdir("/sandbox");
    PASS("test_open_close");
}

int test_read_write(void) {
    mkdir("/sandbox", 0755);
    int fd = open("/sandbox/test_rw.txt", O_CREAT | O_WRONLY, 0644);
    ASSERT(fd >= 0, "open rw");

    const char *msg = "Hello LiwusOS from userspace!";
    int len = 0;
    while (msg[len]) len++;

    int written = write(fd, msg, len);
    ASSERT(written == len, "wrote all bytes");
    close(fd);

    fd = open("/sandbox/test_rw.txt", O_RDONLY);
    ASSERT(fd >= 0, "open for read");

    char buf[64] = {0};
    int got = read(fd, buf, sizeof(buf));
    ASSERT(got == len, "read correct size");
    int match = 1;
    for (int i = 0; i < len; i++) {
        if (buf[i] != msg[i]) { match = 0; break; }
    }
    ASSERT(match, "data matches");
    close(fd);

    unlink("/sandbox/test_rw.txt");
    rmdir("/sandbox");
    PASS("test_read_write");
}

int test_mkdir_rmdir(void) {
    int ret = mkdir("/testdir", 0755);
    ASSERT(ret == 0, "mkdir");

    struct stat st;
    ret = stat("/testdir", &st);
    ASSERT(ret == 0, "stat dir");
    ASSERT(st.st_mode & 0040000, "is directory");

    ret = rmdir("/testdir");
    ASSERT(ret == 0, "rmdir");
    PASS("test_mkdir_rmdir");
}

int test_invalid_fd(void) {
    char buf[4];
    int n = read(99, buf, 4);
    ASSERT(n == -1, "read bad fd returns -1");

    n = write(99, "x", 1);
    ASSERT(n == -1, "write bad fd returns -1");

    int ret = close(99);
    ASSERT(ret == -1, "close bad fd returns -1");

    PASS("test_invalid_fd");
}

int test_invalid_kernel_pointer(void) {
    /* Attempt to write to a kernel address (0x1000000+). The kernel
     * must reject this rather than faulting. */
    const char *kernel_ptr = (const char *)0x2000000ULL;
    int n = write(1, kernel_ptr, 4);
    ASSERT(n == -1, "write to kernel address rejected");

    /* Attempt to read from kernel address */
    char rbuf[4];
    n = read(0, (void *)kernel_ptr, 4);
    ASSERT(n == -1, "read from kernel address rejected");

    /* Attempt to open a kernel-space string */
    int fd = open((const char *)0x2000000ULL, 0);
    ASSERT(fd == -1, "open kernel-space string rejected");

    /* Attempt stat with kernel-space struct */
    struct stat st;
    n = stat((const char *)0x2000000ULL, &st);
    ASSERT(n == -1, "stat kernel-space path rejected");

    PASS("test_invalid_kernel_pointer");
}

int test_invalid_user_range(void) {
    /* Userspace-heap address that is way beyond mapped range. The kernel
     * should reject via page-table validation (if mapped check is active)
     * or at worst return -1 cleanly. */
    const char *far = (const char *)0x00000000ULL;
    int n = write(1, far, 4);
    ASSERT(n == -1, "write to unmapped low addr rejected");

    PASS("test_invalid_user_range");
}

#endif
