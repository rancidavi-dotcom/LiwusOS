#include "liwus/syscall.h"
#include "liwus/errno.h"

int errno = 0;

extern int main(int argc, char **argv, char **envp);
extern char **environ;

void liw_crt_start(const unsigned long *initial_stack) {
    if (!initial_stack) {
        liw_exit(main(0, (char **)0, (char **)0));
    }
    int argc = (int)initial_stack[0];
    char **argv = (char **)&initial_stack[1];
    char **envp = argv + argc + 1;
    environ = envp;
    liw_exit(main(argc, argv, envp));
}
