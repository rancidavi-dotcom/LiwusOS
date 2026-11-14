#include "liwus/liwus.h"

static int same(const char *left, const char *right) {
    if (!left || !right) return 0;
    return liw_strcmp(left, right) == 0;
}

static int has_sdk_environment(char **envp) {
    if (!envp) return 0;
    for (size_t i = 0; envp[i]; i++)
        if (same(envp[i], "LIWUS_SDK=0.1")) return 1;
    return 0;
}

static int exercise_file_wrappers(void) {
    static const char directory[] = "/sdk_probe_dir";
    static const char file_path[] = "/sdk_probe_dir/data";
    static const char renamed_path[] = "/sdk_probe_dir/renamed";
    static const char payload[] = "native-sdk-file-probe";
    char original_cwd[128];
    char current_cwd[128];
    struct liw_stat info;
    int directory_created = 0;
    int entered_directory = 0;
    int file_exists = 0;
    int renamed_exists = 0;
    int fd = -1;
    int result = 1;

    if (liw_getcwd(original_cwd, sizeof(original_cwd)) < 0 ||
        liw_mkdir(directory, 0700) < 0)
        goto cleanup;
    directory_created = 1;
    if (liw_stat(directory, &info) < 0 || info.st_mode == 0 ||
        liw_chdir(directory) < 0)
        goto cleanup;
    entered_directory = 1;
    if (liw_getcwd(current_cwd, sizeof(current_cwd)) < 0 ||
        liw_strcmp(current_cwd, directory) != 0)
        goto cleanup;

    fd = (int)liw_open("data", LIW_O_CREAT | LIW_O_TRUNC);
    if (fd < 0) goto cleanup;
    file_exists = 1;
    if (liw_write_all(fd, payload, sizeof(payload) - 1) < 0 ||
        liw_lseek(fd, 0, 0) != 0 ||
        liw_fstat(fd, &info) < 0 || info.st_size != (int64_t)(sizeof(payload) - 1))
        goto cleanup;
    char readback[sizeof(payload)] = {0};
    if (liw_read(fd, readback, sizeof(payload) - 1) != (long)(sizeof(payload) - 1) ||
        liw_strcmp(readback, payload) != 0 || liw_close(fd) < 0)
        goto cleanup;
    fd = -1;

    if (liw_stat("data", &info) < 0 || info.st_size != (int64_t)(sizeof(payload) - 1) ||
        liw_rename("data", "renamed") < 0)
        goto cleanup;
    file_exists = 0;
    renamed_exists = 1;
    if (liw_stat("renamed", &info) < 0 ||
        liw_unlink("renamed") < 0)
        goto cleanup;
    renamed_exists = 0;
    result = 0;

cleanup:
    if (fd >= 0) liw_close(fd);
    if (entered_directory) liw_chdir(original_cwd);
    if (file_exists) liw_unlink(file_path);
    if (renamed_exists) liw_unlink(renamed_path);
    if (directory_created) liw_rmdir(directory);
    return result;
}

static int exercise_directory_wrapper(void) {
    static const char directory[] = "/sdk_enum_dir";
    static const char file_path[] = "/sdk_enum_dir/entry";
    static const char empty_path[] = "/sdk_empty_dir";
    struct liw_dirent entry;
    int dirfd = -1, filefd = -1, emptyfd = -1;
    int made_dir = 0, made_file = 0, made_empty = 0;
    int result = 1;

    if (liw_mkdir(directory, 0700) < 0) goto cleanup;
    made_dir = 1;
    filefd = liw_open(file_path, LIW_O_CREAT | LIW_O_TRUNC);
    if (filefd < 0) goto cleanup;
    made_file = 1;
    if (liw_write_all(filefd, "x", 1) < 0 ||
        liw_getdents((unsigned int)filefd, &entry, sizeof(entry)) != -LIW_ENOTDIR ||
        liw_close(filefd) < 0) goto cleanup;
    filefd = -1;
    dirfd = liw_open(directory, 0);
    if (dirfd < 0 || liw_getdents(31, &entry, sizeof(entry)) != -LIW_EBADF ||
        liw_getdents((unsigned int)dirfd, NULL, sizeof(entry)) != -LIW_EFAULT ||
        liw_getdents((unsigned int)dirfd, &entry, 1) != -LIW_EINVAL)
        goto cleanup;

    void *readonly = liw_mmap(NULL, 4096, LIW_PROT_READ,
        LIW_MAP_PRIVATE | LIW_MAP_ANONYMOUS, -1, 0);
    if (readonly == LIW_MAP_FAILED) goto cleanup;
    int readonly_result = (int)liw_getdents((unsigned int)dirfd, readonly,
                                             sizeof(entry));
    liw_munmap(readonly, 4096);
    if (readonly_result != -LIW_EFAULT) goto cleanup;

    long bytes = liw_getdents((unsigned int)dirfd, &entry, sizeof(entry));
    if (bytes != (long)sizeof(entry) || entry.d_reclen != sizeof(entry) ||
        entry.d_off != 1 || liw_strcmp(entry.d_name, "entry") != 0 ||
        entry.d_type != LIW_DT_REG ||
        liw_getdents((unsigned int)dirfd, &entry, sizeof(entry)) != 0)
        goto cleanup;
    if (liw_close(dirfd) < 0) goto cleanup;
    dirfd = -1;

    if (liw_getdents(31, &entry, sizeof(entry)) != -LIW_EBADF ||
        liw_mkdir(empty_path, 0700) < 0)
        goto cleanup;
    made_empty = 1;
    emptyfd = liw_open(empty_path, 0);
    if (emptyfd < 0 || liw_getdents((unsigned int)emptyfd, &entry,
                                     sizeof(entry)) != 0)
        goto cleanup;
    result = 0;

cleanup:
    if (filefd >= 0) liw_close(filefd);
    if (dirfd >= 0) liw_close(dirfd);
    if (emptyfd >= 0) liw_close(emptyfd);
    if (made_file) liw_unlink(file_path);
    if (made_dir) liw_rmdir(directory);
    if (made_empty) liw_rmdir(empty_path);
    return result;
}

int main(int argc, char **argv, char **envp) {
    if (argc == 2 && argv && same(argv[1], "--sdk-child")) {
        if (!same(argv[0], "native-hello.lxe") || !has_sdk_environment(envp) ||
            liw_getpid() <= 0)
            return 43;
        return 42;
    }

    if (argc == 2 && argv && same(argv[1], "--sdk-probe")) {
        if (!same(argv[0], "native-hello.lxe") || !has_sdk_environment(envp) ||
            liw_getpid() <= 0)
            return 81;
        struct liw_timeval now;
        struct liw_stat standard_output;
        char cwd[128];
        if (liw_gettimeofday(&now, NULL) < 0 || now.tv_sec < 0) return 181;
        if (liw_getcwd(cwd, sizeof(cwd)) < 0 || cwd[0] != '/' ||
            liw_fstat(1, &standard_output) < 0 || standard_output.st_blksize == 0)
            return 83;

        volatile unsigned long *page = (volatile unsigned long *)liw_mmap(
            NULL, 4096, LIW_PROT_READ | LIW_PROT_WRITE,
            LIW_MAP_PRIVATE | LIW_MAP_ANONYMOUS, -1, 0);
        if (page == LIW_MAP_FAILED) return 84;
        *page = 0x4c495755534c5845UL;
        if (*page != 0x4c495755534c5845UL || liw_munmap((void *)page, 4096) < 0)
            return 85;

        int pipefd[2];
        if (liw_pipe(pipefd) < 0 || liw_dup2(pipefd[1], pipefd[1]) != pipefd[1])
            return 86;
        long duplicate = liw_dup(pipefd[1]);
        if (duplicate < 0) return 87;
        const char byte = 'L';
        char received = 0;
        int pipe_ok = liw_write_all((int)duplicate, &byte, 1) == 0 &&
                      liw_read(pipefd[0], &received, 1) == 1 && received == byte;
        long close_duplicate = liw_close((int)duplicate);
        long close_writer = liw_close(pipefd[1]);
        long close_reader = liw_close(pipefd[0]);
        if (!pipe_ok || close_duplicate < 0 || close_writer < 0 || close_reader < 0)
            return 88;
        if (exercise_file_wrappers() != 0) return 89;
        if (exercise_directory_wrapper() != 0) return 93;

        long child = liw_fork();
        if (child < 0) return 90;
        if (child == 0) {
            static char *child_argv[] = { "native-hello.lxe", "--sdk-child", NULL };
            liw_execve("/bin/native-hello.lxe", child_argv, envp);
            liw_exit(91);
        }
        int child_status = -1;
        if (liw_waitpid((int)child, &child_status, 0) != child || child_status != 42)
            return 92;

        static const char probe[] = "[LXE SDK PROBE] crt0/argv/envp/syscalls PASS\n";
        if (liw_write_all(1, probe, sizeof(probe) - 1) < 0) return 82;
        return 0;
    }
    static const char msg[] = "Hello from LIWUS OS SDK v0.1!\n";
    if (liw_write_all(1, msg, liw_strlen(msg)) < 0) return 1;
    return 0;
}
