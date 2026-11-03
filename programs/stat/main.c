#include "liwus/liwus.h"

static int write_text(const char *text) {
    return liw_write_all(1, text, liw_strlen(text)) < 0 ? -1 : 0;
}

static int write_unsigned(uint64_t value, unsigned base) {
    char digits[24];
    size_t length = 0;
    do {
        unsigned digit = (unsigned)(value % base);
        digits[length++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value && length < sizeof(digits));
    while (length) {
        char c = digits[--length];
        if (liw_write_all(1, &c, 1) < 0) return -1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        static const char message[] = "usage: stat PATH\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 2;
    }
    struct liw_stat info;
    if (liw_stat(argv[1], &info) < 0) {
        static const char message[] = "stat: cannot inspect path\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 1;
    }

    const char *type = (info.st_mode & 0170000u) == 0040000u
        ? "directory" : "regular file";
    if (write_text("Path: ") < 0 || write_text(argv[1]) < 0 ||
        write_text("\nType: ") < 0 || write_text(type) < 0 ||
        write_text("\nSize: ") < 0 || write_unsigned(info.st_size, 10) < 0 ||
        write_text(" bytes\nMode: ") < 0 ||
        write_unsigned(info.st_mode & 0777u, 8) < 0 ||
        write_text("\n") < 0)
        return 1;
    return 0;
}
