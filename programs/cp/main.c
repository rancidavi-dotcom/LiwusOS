#include "liwus/liwus.h"

static int fail(const char *message) {
    (void)liw_write_all(2, message, liw_strlen(message));
    return 1;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 3)
        return fail("cp: expected source and destination\n");

    struct liw_stat source, destination;
    if (liw_stat(argv[1], &source) < 0)
        return fail("cp: cannot inspect source\n");
    if ((source.st_mode & 0170000u) == 0040000u)
        return fail("cp: directory copy is unsupported\n");
    if (liw_stat(argv[2], &destination) == 0 &&
        source.st_dev == destination.st_dev &&
        source.st_ino == destination.st_ino)
        return fail("cp: source and destination are the same file\n");

    int input = liw_open(argv[1], LIW_O_RDONLY);
    if (input < 0) return fail("cp: cannot open source\n");
    int output = liw_open(argv[2], LIW_O_WRONLY | LIW_O_CREAT | LIW_O_EXCL);
    if (output < 0) {
        liw_close(input);
        return fail("cp: cannot create destination (it may exist)\n");
    }

    unsigned char buffer[512];
    int result = 0;
    for (;;) {
        long count = liw_read(input, buffer, sizeof(buffer));
        if (count < 0 || (unsigned long)count > sizeof(buffer)) {
            result = fail("cp: read failed\n");
            break;
        }
        if (count == 0) break;
        if (liw_write_all(output, buffer, (size_t)count) < 0) {
            result = fail("cp: write failed\n");
            break;
        }
    }
    if (liw_close(input) < 0 && result == 0)
        result = fail("cp: source close failed\n");
    if (liw_close(output) < 0 && result == 0)
        result = fail("cp: destination close failed\n");
    if (result != 0 && liw_unlink(argv[2]) < 0)
        (void)liw_write_all(2, "cp: partial destination could not be removed\n",
                            liw_strlen("cp: partial destination could not be removed\n"));
    return result;
}
