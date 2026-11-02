#include "liwus/liwus.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        static const char message[] = "mkdir: expected one directory\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 2;
    }
    long rc = liw_mkdir(argv[1], 0755);
    if (rc < 0) {
        static const char message[] = "mkdir: cannot create directory\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }
    return 0;
}
