#ifndef LIWLIB_SIGNAL_H
#define LIWLIB_SIGNAL_H

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIGINT 2

typedef void (*sighandler_t)(int);

sighandler_t signal(int sig, sighandler_t handler);

#endif
