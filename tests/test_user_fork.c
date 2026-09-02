#ifndef KERNEL_TEST
#include "framework.h"
#include <unistd.h>
#include <sys/wait.h>

int test_fork_wait(void) {
    int pid = fork();
    ASSERT(pid >= 0, "fork returned >= 0");

    if (pid == 0) {
        /* Child: exit with code 42 */
        _exit(42);
    }

    /* Parent: wait for child */
    int status = 0;
    int ret = waitpid(pid, &status, 0);
    ASSERT(ret == pid, "waitpid returned child pid");
    ASSERT(status == 42, "child exit code is 42");

    PASS("test_fork_wait");
}

int test_fork_multiple(void) {
    int pids[5];
    for (int i = 0; i < 5; i++) {
        pids[i] = fork();
        ASSERT(pids[i] >= 0, "fork ok");
        if (pids[i] == 0) {
            _exit(i + 10);
        }
    }

    for (int i = 0; i < 5; i++) {
        int status = 0;
        int ret = waitpid(pids[i], &status, 0);
        ASSERT(ret == pids[i], "waitpid ok");
        ASSERT(status == i + 10, "exit code correct");
    }

    PASS("test_fork_multiple");
}

int test_waitpid_invalid(void) {
    int ret = waitpid(-999, (void *)0, 0);
    ASSERT(ret == -1, "waitpid bad pid returns -1");
    PASS("test_waitpid_invalid");
}

#endif
