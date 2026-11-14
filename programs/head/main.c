#include "liwus/liwus.h"

static int fail(const char *message) {
    (void)liw_write_all(2, message, liw_strlen(message));
    return 1;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    int fd = 0;
    int need_close = 0;

    if (argc == 2) {
        fd = liw_open(argv[1], LIW_O_RDONLY);
        if (fd < 0) return fail("head: cannot open file\n");
        struct liw_stat info;
        if (liw_fstat(fd, &info) < 0 ||
            (info.st_mode & 0170000u) == 0040000u) {
            liw_close(fd);
            return fail("head: path is not a regular file\n");
        }
        need_close = 1;
    } else if (argc != 1) {
        return fail("usage: head [FILE]\n");
    }

    unsigned char buffer[512];
    unsigned lines = 0;
    int result = 0;
    while (lines < 10) {
        long count = liw_read(fd, buffer, sizeof(buffer));
        if (count < 0 || (unsigned long)count > sizeof(buffer)) {
            result = fail("head: read failed\n");
            break;
        }
        if (count == 0) break;
        size_t output = (size_t)count;
        for (size_t i = 0; i < (size_t)count; i++) {
            if (buffer[i] == '\n' && ++lines == 10) {
                output = i + 1;
                break;
            }
        }
        if (liw_write_all(1, buffer, output) < 0) {
            result = fail("head: output failed\n");
            break;
        }
    }
    if (need_close && liw_close(fd) < 0 && result == 0)
        result = fail("head: close failed\n");
    return result;
}
