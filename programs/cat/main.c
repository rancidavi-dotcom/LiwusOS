#include "liwus/liwus.h"

static int fail(const char *message) {
    (void)liw_write_all(2, message, liw_strlen(message));
    return 1;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) return fail("cat: expected one file\n");

    int fd = liw_open(argv[1], 0);
    if (fd < 0) return fail("cat: cannot open file\n");
    struct liw_stat info;
    if (liw_fstat(fd, &info) < 0) {
        liw_close(fd);
        return fail("cat: cannot inspect file\n");
    }
    if ((info.st_mode & 0170000) == 0040000) {
        liw_close(fd);
        return fail("cat: path is a directory\n");
    }

    unsigned char buffer[512];
    for (;;) {
        long count = liw_read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            liw_close(fd);
            return fail("cat: read failed\n");
        }
        if (count == 0) break;
        if ((unsigned long)count > sizeof(buffer) ||
            liw_write_all(1, buffer, (size_t)count) < 0) {
            liw_close(fd);
            return fail("cat: write failed\n");
        }
    }
    if (liw_close(fd) < 0) return fail("cat: close failed\n");
    return 0;
}
