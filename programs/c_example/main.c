#include "liwus.h"

static int same(const char *left, const char *right) {
    while (*left && *right && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

int main(int argc, char **argv, char **envp) {
    static const char message[] = "[C APP PASS] standalone freestanding main/argv/syscall\n";
    if (argc == 3 && same(argv[1], "pipe")) {
        long fd = 0;
        for (const char *p = argv[2]; *p; p++) {
            if (*p < '0' || *p > '9') return 72;
            fd = fd * 10 + (*p - '0');
        }
        static const char byte = 'P';
        return liw_write(fd, &byte, 1) == 1 ? 74 : 75;
    }
    if (argc != 3 || !same(argv[0], "c_example") ||
        !same(argv[1], "alpha") || !same(argv[2], "beta") || !envp)
        return 70;
    if (liw_write(1, message, sizeof(message) - 1) != (long)(sizeof(message) - 1))
        return 71;
    return 73;
}
