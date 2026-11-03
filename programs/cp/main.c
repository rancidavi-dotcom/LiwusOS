#include "liwus/liwus.h"

static int fail(const char *message) {
    (void)liw_write_all(2, message, liw_strlen(message));
    return 1;
}

static int copy_to_directory(const char *directory, const char *source,
                             char *output, size_t capacity) {
    size_t directory_length = liw_strlen(directory);
    size_t source_length = liw_strlen(source);
    size_t base = source_length;
    while (base > 0 && source[base - 1] == '/') base--;
    size_t name_start = base;
    while (name_start > 0 && source[name_start - 1] != '/') name_start--;
    size_t name_length = base - name_start;
    if (!name_length) return -1;

    int add_slash = directory_length > 0 &&
                    directory[directory_length - 1] != '/';
    size_t total = directory_length + (size_t)add_slash + name_length;
    if (total + 1 > capacity) return -1;

    size_t at = 0;
    for (; at < directory_length; at++) output[at] = directory[at];
    if (add_slash) output[at++] = '/';
    for (size_t i = 0; i < name_length; i++)
        output[at++] = source[name_start + i];
    output[at] = '\0';
    return 0;
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
    char destination_path[256];
    const char *output_path = argv[2];
    int destination_exists = liw_stat(argv[2], &destination) == 0;
    if (destination_exists &&
        (destination.st_mode & 0170000u) == 0040000u) {
        if (copy_to_directory(argv[2], argv[1], destination_path,
                              sizeof(destination_path)) != 0)
            return fail("cp: destination path is too long or source has no name\n");
        output_path = destination_path;
        destination_exists = liw_stat(output_path, &destination) == 0;
    }
    if (destination_exists && source.st_dev == destination.st_dev &&
        source.st_ino == destination.st_ino)
        return fail("cp: source and destination are the same file\n");

    int input = liw_open(argv[1], LIW_O_RDONLY);
    if (input < 0) return fail("cp: cannot open source\n");
    int output = liw_open(output_path, LIW_O_WRONLY | LIW_O_CREAT | LIW_O_EXCL);
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
    if (result != 0 && liw_unlink(output_path) < 0)
        (void)liw_write_all(2, "cp: partial destination could not be removed\n",
                            liw_strlen("cp: partial destination could not be removed\n"));
    return result;
}
