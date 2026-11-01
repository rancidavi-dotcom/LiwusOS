#include "liwus.h"
#include <stddef.h>

static unsigned long length(const char *text) {
    unsigned long n = 0;
    while (text[n]) n++;
    return n;
}

static void say(const char *text) {
    unsigned long left = length(text);
    while (left) {
        long written = liw_write(1, text, left);
        if (written <= 0) return;
        text += written;
        left -= (unsigned long)written;
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    char *shell_argv[] = { "liwus_shell", NULL };

    say("[init] user process supervisor started\n");
    for (;;) {
        long pid = liw_fork();
        if (pid < 0) {
            say("[init] fork failed; retrying\n");
            for (volatile unsigned long delay = 0; delay < 10000000; delay++)
                __asm__ volatile("pause");
            continue;
        }
        if (pid == 0) {
            liw_execve("liwus_shell", shell_argv, NULL);
            say("[init] could not load liwus_shell\n");
            liw_exit(127);
        }

        int status = -1;
        if (liw_waitpid((int)pid, &status, 0) < 0) {
            say("[init] waitpid failed; restarting shell\n");
            continue;
        }
        say("[init] shell exited with status ");
        char digits[12];
        unsigned int value = (unsigned int)status;
        int count = 0;
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value && count < (int)sizeof(digits));
        while (count) liw_write(1, &digits[--count], 1);
        say("; restarting\n");
    }
}
