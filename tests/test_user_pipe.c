#ifndef KERNEL_TEST
#include "framework.h"
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

int test_pipe_basic(void) {
    int pipefd[2];
    int ret = pipe(pipefd);
    ASSERT(ret == 0, "pipe() succeeded");

    const char *msg = "pipe_test";
    int len = 0;
    while (msg[len]) len++;

    int pid = fork();
    ASSERT(pid >= 0, "fork ok");

    if (pid == 0) {
        /* Child: write to pipe */
        close(pipefd[0]);
        write(pipefd[1], msg, len);
        close(pipefd[1]);
        _exit(0);
    }

    /* Parent: read from pipe */
    close(pipefd[1]);
    char buf[32] = {0};
    int got = read(pipefd[0], buf, sizeof(buf));
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    ASSERT(got == len, "read correct bytes");
    int match = 1;
    for (int i = 0; i < len; i++) {
        if (buf[i] != msg[i]) { match = 0; break; }
    }
    ASSERT(match, "pipe data matches");

    PASS("test_pipe_basic");
}

int test_pipe_large(void) {
    int pipefd[2];
    int ret = pipe(pipefd);
    ASSERT(ret == 0, "pipe() ok");

    char data[4096];
    for (int i = 0; i < 4096; i++) data[i] = (char)(i & 0xFF);

    int pid = fork();
    ASSERT(pid >= 0, "fork ok");

    if (pid == 0) {
        close(pipefd[0]);
        int total = 0;
        while (total < 4096) {
            int w = write(pipefd[1], data + total, 4096 - total);
            if (w <= 0) break;
            total += w;
        }
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    char buf[4096] = {0};
    int total = 0;
    while (total < 4096) {
        int r = read(pipefd[0], buf + total, 4096 - total);
        if (r <= 0) break;
        total += r;
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    ASSERT(total == 4096, "read 4096 bytes");
    int match = 1;
    for (int i = 0; i < 4096; i++) {
        if (buf[i] != data[i]) { match = 0; break; }
    }
    ASSERT(match, "large pipe data matches");

    PASS("test_pipe_large");
}

#endif
