#ifndef _SYS_RESOURCE_H_
#define _SYS_RESOURCE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <sys/types.h>

#define	RUSAGE_SELF	0
#define	RUSAGE_CHILDREN	-1

#define PRIO_PROCESS    0
#define PRIO_PGRP       1
#define PRIO_USER       2

#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_NOFILE   5
#define RLIMIT_NPROC    6
#define RLIMIT_MEMLOCK  7
#define RLIMIT_AS       9
#define RLIMIT_LOCKS    10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE 12
#define RLIMIT_NICE     13
#define RLIMIT_RTPRIO   14
#define RLIMIT_RTTIME   15

#define RLIM_INFINITY (~0UL)

typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
};

int   getrusage (int, struct rusage*);
int   getpriority (int, int);
int   setpriority (int, int, int);
int   getrlimit (int, struct rlimit *);
int   setrlimit (int, const struct rlimit *);

#ifdef __cplusplus
}
#endif
#endif
