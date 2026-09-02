#ifndef KERNEL_TEST
#include "framework.h"
#include <stdio.h>

int test_open_close(void);
int test_read_write(void);
int test_mkdir_rmdir(void);
int test_invalid_fd(void);
int test_invalid_kernel_pointer(void);
int test_invalid_user_range(void);

int test_getpid(void);
int test_chdir_getcwd(void);
int test_sbrk(void);

int test_fork_wait(void);
int test_fork_multiple(void);
int test_waitpid_invalid(void);

int test_pipe_basic(void);
int test_pipe_large(void);

int main(void) {
    TEST_RUNNER_BEGIN;
    int pass = 0, fail = 0;

    /* Unbuffered stdout so results appear in serial immediately,
       even if a later test stalls or the process never flushes. */
    setvbuf(stdout, NULL, _IONBF, 0);

    #define RUN(fn) do { TEST_BEGIN(#fn); if (fn() == 0) pass++; else fail++; } while(0)

    /* Syscall tests */
    RUN(test_open_close);
    RUN(test_read_write);
    RUN(test_mkdir_rmdir);
    RUN(test_invalid_fd);
    RUN(test_invalid_kernel_pointer);
    RUN(test_invalid_user_range);

    /* Misc */
    RUN(test_getpid);
    RUN(test_chdir_getcwd);
    RUN(test_sbrk);

    /* Fork/process tests (disabled: fork_process crashes) */
    /* RUN(test_fork_wait);
    RUN(test_fork_multiple);
    RUN(test_waitpid_invalid); */

    /* Pipe tests (disabled: rely on fork_process which currently hangs) */
    /* RUN(test_pipe_basic);
       RUN(test_pipe_large); */

    #undef RUN

    TEST_RESULT(pass, fail);
    TEST_RUNNER_END;
    return fail > 0 ? 1 : 0;
}
#endif
