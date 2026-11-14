#include "libc/signal.h"
#include "liwus/stdio.h"
#include "liwus/stdlib.h"

int sigemptyset(sigset_t *set) {
    if (set) *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (set) *set = ~0UL;
    return 0;
}

int sigaddset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) return -1;
    *set |= (1UL << (signum - 1));
    return 0;
}

int sigdelset(sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) return -1;
    *set &= ~(1UL << (signum - 1));
    return 0;
}

int sigismember(const sigset_t *set, int signum) {
    if (!set || signum < 1 || signum > 64) return -1;
    return (*set & (1UL << (signum - 1))) ? 1 : 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    (void)signum;
    (void)act;
    (void)oldact;
    return 0;
}

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    fprintf(stderr, "Assertion failed: %s (%s: %s: %u)\n",
            assertion ? assertion : "",
            file ? file : "",
            function ? function : "",
            line);
    abort();
}
