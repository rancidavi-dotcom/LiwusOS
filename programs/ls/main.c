#include "liwus/liwus.h"

static int fail(const char *message) {
    (void)liw_write_all(2, message, liw_strlen(message));
    return 1;
}

static const char *get_badge(uint64_t res_type, uint32_t mode) {
    if ((mode & 0170000) == 0040000) return "[DIR  ] ";
    switch (res_type) {
        case 1: return "[TEXT ] ";
        case 2: return "[EXEC ] ";
        case 3: return "[IMAGE] ";
        case 4: return "[AUDIO] ";
        case 5: return "[DEV  ] ";
        case 6: return "[IPC  ] ";
        case 7: return "[DIR  ] ";
        default: return "[DATA ] ";
    }
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc > 2) return fail("ls: expected at most one directory\n");

    char cwd[256];
    const char *path = "/";
    if (argc == 2) {
        path = argv[1];
    } else if (liw_getcwd(cwd, sizeof(cwd)) == 0) {
        path = cwd;
    }

    int fd = liw_open(path, 0);
    if (fd < 0) return fail("ls: cannot open directory\n");
    struct liw_stat info;
    if (liw_fstat(fd, &info) < 0) {
        liw_close(fd);
        return fail("ls: cannot inspect directory\n");
    }
    if ((info.st_mode & 0170000) != 0040000) {
        liw_close(fd);
        return fail("ls: path is not a directory\n");
    }

    struct liw_dirent entry;
    for (;;) {
        long bytes = liw_getdents((unsigned int)fd, &entry, sizeof(entry));
        if (bytes < 0) {
            liw_close(fd);
            return fail("ls: directory read failed\n");
        }
        if (bytes == 0) break;
        if (bytes != (long)sizeof(entry) ||
            entry.d_reclen != sizeof(entry) ||
            entry.d_name[sizeof(entry.d_name) - 1] != '\0') {
            liw_close(fd);
            return fail("ls: invalid directory record\n");
        }
        size_t name_length = 0;
        while (name_length < sizeof(entry.d_name) && entry.d_name[name_length])
            name_length++;

        /* Inspeciona o tipo nativo do arquivo via stat */
        char full_path[512];
        size_t plen = liw_strlen(path);
        if (plen + 1 + name_length < sizeof(full_path)) {
            liw_memcpy(full_path, path, plen);
            if (plen > 0 && full_path[plen - 1] != '/' && full_path[plen - 1] != ':') {
                full_path[plen++] = '/';
            }
            liw_memcpy(full_path + plen, entry.d_name, name_length);
            full_path[plen + name_length] = '\0';

            struct liw_stat st;
            const char *badge = "[FILE ] ";
            if (liw_stat(full_path, &st) == 0) {
                badge = get_badge(st.st_rdev, st.st_mode);
            }
            (void)liw_write_all(1, badge, liw_strlen(badge));
        }

        if (name_length == sizeof(entry.d_name) ||
            liw_write_all(1, entry.d_name, name_length) < 0 ||
            liw_write_all(1, "\n", 1) < 0) {
            liw_close(fd);
            return fail("ls: output failed\n");
        }
    }
    if (liw_close(fd) < 0) return fail("ls: close failed\n");
    return 0;
}
