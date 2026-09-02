#ifndef KERNEL_TEST
#include "framework.h"
#include <unistd.h>
#include <sys/wait.h>

int test_child_exit_code(void) {
    int pid = fork();
    ASSERT(pid >= 0, "fork ok");

    if (pid == 0) {
        _exit(7);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT(status == 7, "exit code 7");
    PASS("test_child_exit_code");
}

int test_exec_ls(void) {
    /* exec a simple program that doesn't exist - should fail gracefully */
    int pid = fork();
    ASSERT(pid >= 0, "fork ok");

    if (pid == 0) {
        char *args[] = { "nonexistent", 0 };
        execve("/nonexistent.elf", args, (void *)0);
        _exit(1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT(status != 0, "exec nonexistent returns non-zero exit");

    PASS("test_exec_ls");
}

int test_orphan_reparent(void) {
    /* Fork a child, then fork grandchild. Kill child. Grandchild should
     * be reparented to init (pid 1). We just verify no crash. */
    int p1 = fork();
    ASSERT(p1 >= 0, "fork p1");

    if (p1 == 0) {
        int p2 = fork();
        if (p2 == 0) {
            _exit(0);
        }
        _exit(0);
    }

    int status = 0;
    waitpid(p1, &status, 0);
    ASSERT(status == 0, "child exited ok");

    PASS("test_orphan_reparent");
}

#endif
