#include "liwus/liwus.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 3) {
        static const char message[] = "mv: expected source and destination\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 2;
    }
    struct liw_stat source, destination;
    if (liw_stat(argv[1], &source) < 0) {
        static const char message[] = "mv: cannot inspect source\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    if ((source.st_mode & 0170000u) == 0040000u) {
        static const char message[] = "mv: directory moves are unsupported\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    if (liw_stat(argv[2], &destination) == 0) {
        static const char message[] = "mv: destination exists\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    if (liw_rename(argv[1], argv[2]) < 0) {
        static const char message[] = "mv: rename failed\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    return 0;
}
