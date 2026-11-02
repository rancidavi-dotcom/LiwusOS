#include "liwus/liwus.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        static const char message[] = "touch: expected one file\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 2;
    }
    int fd = liw_open(argv[1], LIW_O_WRONLY | LIW_O_CREAT | LIW_O_EXCL);
    if (fd >= 0) return liw_close(fd) < 0 ? 1 : 0;

    struct liw_stat info;
    if (liw_stat(argv[1], &info) == 0 &&
        (info.st_mode & 0170000u) != 0040000u)
        return 0; /* Existing regular files are left byte-for-byte unchanged. */
    static const char message[] = "touch: cannot create file\n";
    (void)liw_write_all(2, message, sizeof(message) - 1);
    return 1;
}
