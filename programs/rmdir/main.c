#include "liwus/liwus.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        static const char message[] = "usage: rmdir DIRECTORY\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 2;
    }
    struct liw_stat info;
    if (liw_stat(argv[1], &info) < 0 ||
        (info.st_mode & 0170000u) != 0040000u) {
        static const char message[] = "rmdir: not a directory\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    if (liw_rmdir(argv[1]) < 0) {
        static const char message[] = "rmdir: directory is not empty or cannot be removed\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    return 0;
}
