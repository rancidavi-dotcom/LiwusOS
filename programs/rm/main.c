#include "liwus/liwus.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        static const char message[] = "rm: expected one file\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 2;
    }
    struct liw_stat info;
    long rc = liw_stat(argv[1], &info);
    if (rc < 0) {
        static const char message[] = "rm: cannot inspect path\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    if ((info.st_mode & 0170000u) == 0040000u) {
        static const char message[] = "rm: path is a directory; use rmdir\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    if (liw_unlink(argv[1]) < 0) {
        static const char message[] = "rm: cannot remove file\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    return 0;
}
