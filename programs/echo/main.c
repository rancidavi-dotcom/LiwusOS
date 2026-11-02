#include "liwus/liwus.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    for (int i = 1; i < argc; i++) {
        if (i > 1 && liw_write_all(1, " ", 1) < 0) return 1;
        size_t length = liw_strlen(argv[i]);
        if (liw_write_all(1, argv[i], length) < 0) return 1;
    }
    return liw_write_all(1, "\n", 1) < 0 ? 1 : 0;
}
