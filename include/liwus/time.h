#ifndef LIWUS_TIME_H
#define LIWUS_TIME_H

#include <stdint.h>
#include <stddef.h>

#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
typedef long time_t;
#endif

#ifndef _SUSECONDS_T_DEFINED
#define _SUSECONDS_T_DEFINED
typedef long suseconds_t;
#endif

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff; /* LIWUS civil-time functions currently return UTC. */
};

#ifdef __cplusplus
extern "C" {
#endif

time_t time(time_t *tloc);
int gettimeofday(struct timeval *tv, struct timezone *tz);
struct tm *localtime(const time_t *timer);
struct tm *localtime_r(const time_t *timer, struct tm *result);

#ifdef __cplusplus
}
#endif

#endif /* LIWUS_TIME_H */
