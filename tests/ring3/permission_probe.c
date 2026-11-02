#include "liwus/liwus.h"

static int fail(int code, const char *message) {
    (void)liw_write_all(2, message, liw_strlen(message));
    return code;
}

static void write_result(long value) {
    char digits[12];
    size_t count = 0;
    unsigned long n = value < 0 ? (unsigned long)(-value) : (unsigned long)value;
    char sign = value < 0 ? '-' : '+';
    (void)liw_write_all(2, &sign, 1);
    do {
        digits[count++] = (char)('0' + n % 10);
        n /= 10;
    } while (n && count < sizeof(digits));
    while (count) (void)liw_write_all(2, &digits[--count], 1);
    (void)liw_write_all(2, "\n", 1);
}

int main(int argc, char **argv, char **envp) {
    (void)argv;
    (void)envp;
    if (argc > 1) return fail(10, "permission probe: argc\n");
    if (liw_mkdir((const char *)1, 0755) != -LIW_EFAULT ||
        liw_unlink((const char *)1) != -LIW_EFAULT ||
        liw_rmdir((const char *)1) != -LIW_EFAULT ||
        liw_rename((const char *)1, "/perm_probe/invalid") != -LIW_EFAULT)
        return fail(31, "permission probe: invalid operation paths\n");

    struct liw_stat info;
    if (liw_stat("/perm_probe/read-allow", &info) < 0 ||
        (info.st_mode & 0777u) != 0644u || info.st_uid != 0)
        return fail(11, "permission probe: stat metadata\n");

    int fd = liw_open("/perm_probe/read-allow", LIW_O_RDONLY);
    if (fd < 0) return fail(12, "permission probe: allowed read open\n");
    char byte;
    if (liw_read(fd, &byte, 1) != 1 || byte != 'R' ||
        liw_write(fd, "x", 1) != -LIW_EBADF || liw_close(fd) < 0)
        return fail(13, "permission probe: read descriptor rights\n");
    if (liw_open("/perm_probe/read-deny", LIW_O_RDONLY) != -LIW_EACCES)
        return fail(14, "permission probe: denied read\n");

    fd = liw_open("/perm_probe/write-allow",
                  LIW_O_WRONLY | LIW_O_TRUNC);
    if (fd < 0 || liw_read(fd, &byte, 1) != -LIW_EBADF ||
        liw_write_all(fd, "W", 1) < 0 || liw_close(fd) < 0)
        return fail(15, "permission probe: allowed write descriptor\n");
    if (liw_open("/perm_probe/write-deny", LIW_O_WRONLY) != -LIW_EACCES)
        return fail(16, "permission probe: denied write\n");

    int dirfd = liw_open("/perm_probe/allow-dir", LIW_O_RDONLY);
    struct liw_dirent entry;
    if (dirfd < 0 || liw_getdents((unsigned int)dirfd, &entry,
                                  sizeof(entry)) <= 0 || liw_close(dirfd) < 0)
        return fail(17, "permission probe: allowed directory enumeration\n");
    if (liw_open("/perm_probe/deny-dir", LIW_O_RDONLY) != -LIW_EACCES ||
        liw_stat("/perm_probe/deny-dir/entry", &info) != -LIW_EACCES)
        return fail(18, "permission probe: denied directory traversal\n");
    if (liw_mkdir("/perm_probe/deny-dir/new-dir", 0755) != -LIW_EACCES)
        return fail(19, "permission probe: denied mkdir\n");
    if (liw_open("/perm_probe/deny-dir/new-file",
                 LIW_O_WRONLY | LIW_O_CREAT | LIW_O_EXCL) != -LIW_EACCES)
        return fail(20, "permission probe: denied create\n");
    if (liw_unlink("/perm_probe/deny-dir/entry") != -LIW_EACCES ||
        liw_rename("/perm_probe/deny-dir/entry",
                   "/perm_probe/allow-dir/moved") != -LIW_EACCES ||
        liw_rename("/perm_probe/allow-dir/rename-src",
                   "/perm_probe/deny-dir/renamed") != -LIW_EACCES)
        return fail(21, "permission probe: denied unlink or rename\n");
    fd = liw_open("/perm_probe/allow-dir/created",
                  LIW_O_RDWR | LIW_O_CREAT | LIW_O_EXCL);
    if (fd < 0 || liw_write_all(fd, "U", 1) < 0)
        return fail(22, "permission probe: allowed create\n");
    if (liw_unlink("/perm_probe/allow-dir/created") < 0 ||
        liw_lseek(fd, 0, 0) != 0 || liw_read(fd, &byte, 1) != 1 ||
        byte != 'U' || liw_close(fd) < 0 ||
        liw_stat("/perm_probe/allow-dir/created", &info) != -LIW_ENOENT)
        return fail(23, "permission probe: allowed unlink\n");
    if (liw_mkdir("/perm_probe/allow-dir/new-dir", 0755) < 0)
        return fail(28, "permission probe: mkdir rmdir fixture\n");
    if (liw_rmdir("/perm_probe/allow-dir/new-dir") < 0)
        return fail(28, "permission probe: remove empty directory\n");
    long nonempty_rc = liw_rmdir("/perm_probe/allow-dir");
    if (nonempty_rc != -LIW_ENOTEMPTY) {
        write_result(nonempty_rc);
        return fail(28, "permission probe: reject non-empty directory\n");
    }

    fd = liw_open("/perm_probe/allow-dir/trunc-test",
                  LIW_O_RDWR | LIW_O_CREAT | LIW_O_EXCL);
    if (fd < 0 || liw_write_all(fd, "T", 1) < 0 ||
        liw_open("/perm_probe/allow-dir/trunc-test",
                 LIW_O_WRONLY | LIW_O_TRUNC) != -LIW_EBUSY ||
        liw_lseek(fd, 0, 0) != 0 || liw_read(fd, &byte, 1) != 1 ||
        byte != 'T' || liw_close(fd) < 0)
        return fail(29, "permission probe: truncate preserves an open file\n");
    fd = liw_open("/perm_probe/allow-dir/trunc-test",
                  LIW_O_WRONLY | LIW_O_TRUNC);
    if (fd < 0 || liw_fstat(fd, &info) < 0 || info.st_size != 0 ||
        liw_close(fd) < 0 ||
        liw_unlink("/perm_probe/allow-dir/trunc-test") < 0)
        return fail(30, "permission probe: copy-on-write truncate\n");

    static char *exec_argv[] = {
        "exec_target", "alpha", "beta", NULL
    };
    static char *exec_env[] = { "MODE=ring3", "COUNT=2", NULL };
    if (liw_execve("/perm_probe/write-deny", exec_argv, exec_env) !=
        -LIW_EACCES)
        return fail(24, "permission probe: denied exec\n");
    long child = liw_fork();
    if (child < 0) return fail(25, "permission probe: fork\n");
    if (child == 0) {
        liw_execve("/perm_probe/exec-allow.lxe", exec_argv, exec_env);
        liw_exit(26);
    }
    int status = -1;
    if (liw_waitpid((int)child, &status, 0) != child || status != 37)
        return fail(27, "permission probe: allowed exec and wait\n");

    (void)liw_write_all(1, "[PERMISSION PROBE] native DAC checks PASS\n",
                        sizeof("[PERMISSION PROBE] native DAC checks PASS\n") - 1);
    return 0;
}
