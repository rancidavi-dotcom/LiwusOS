#include "liwus/syscall.h"

extern int main(int argc, char **argv, char **envp);

void liw_crt_start(const unsigned long *initial_stack) {
    if (!initial_stack) {
        liw_exit(main(0, (char **)0, (char **)0));
    }
    int argc = (int)initial_stack[0];
    char **argv = (char **)&initial_stack[1];
    char **envp = argv + argc + 1;
    /* Skip envp if it points past a reasonable area; envp may be NULL */
    liw_exit(main(argc, argv, envp));
}
