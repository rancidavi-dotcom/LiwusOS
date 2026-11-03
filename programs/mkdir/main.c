#include "liwus/liwus.h"

static int is_directory(const struct liw_stat *info) {
    return info && (info->st_mode & 0170000u) == 0040000u;
}

static int make_parents(const char *path) {
    size_t length = liw_strlen(path);
    if (!length || length >= 256) return -1;

    char work[256];
    for (size_t i = 0; i <= length; i++) work[i] = path[i];
    while (length > 1 && work[length - 1] == '/') work[--length] = '\0';

    struct liw_stat info;
    if (liw_stat(work, &info) == 0) return is_directory(&info) ? 0 : -1;

    size_t component_start = (work[0] == '/') ? 1 : 0;
    for (size_t i = component_start; i <= length; i++) {
        if (i != length && work[i] != '/') continue;
        if (i == component_start) {
            component_start = i + 1;
            continue;
        }

        char saved = work[i];
        work[i] = '\0';
        long rc = liw_mkdir(work, 0755);
        if (rc < 0 && (liw_stat(work, &info) < 0 || !is_directory(&info))) {
            work[i] = saved;
            return -1;
        }
        work[i] = saved;
        component_start = i + 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    int parents = 0;
    const char *path = NULL;
    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && liw_strcmp(argv[1], "-p") == 0) {
        parents = 1;
        path = argv[2];
    } else {
        static const char message[] = "usage: mkdir [-p] DIRECTORY\n";
        (void)liw_write_all(2, message, sizeof(message) - 1);
        return 2;
    }
    long rc = parents ? make_parents(path) : liw_mkdir(path, 0755);
    if (rc < 0) {
        static const char prefix[] = "mkdir: cannot create '";
        static const char suffix[] = "'\n";
        (void)liw_write_all(2, prefix, sizeof(prefix) - 1);
        (void)liw_write_all(2, path, liw_strlen(path));
        (void)liw_write_all(2, suffix, sizeof(suffix) - 1);
        return 1;
    }
    return 0;
}
