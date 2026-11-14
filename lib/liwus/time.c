#include "liwus/time.h"
#include "liwus/syscall.h"
#include "liwus/errno.h"

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (!tv) return -1;
    struct liw_timeval ltv;
    long r = liw_gettimeofday(&ltv, tz);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    tv->tv_sec = ltv.tv_sec;
    tv->tv_usec = ltv.tv_usec;
    return 0;
}

time_t time(time_t *tloc) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return (time_t)-1;
    if (tloc) *tloc = tv.tv_sec;
    return tv.tv_sec;
}

static struct tm tm_buffer;

static struct tm *localtime_into(const time_t *timer, struct tm *result) {
    if (!timer || !result) return NULL;
    time_t t = *timer;
    result->tm_sec = (int)(t % 60);
    t /= 60;
    result->tm_min = (int)(t % 60);
    t /= 60;
    result->tm_hour = (int)(t % 24);
    time_t days = t / 24;

    result->tm_wday = (int)((days + 4) % 7);

    long year = 1970;
    while (1) {
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        long days_in_year = leap ? 366 : 365;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }
    result->tm_year = (int)(year - 1900);
    result->tm_yday = (int)days;

    static const int days_per_month[2][12] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
    };
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    int mon = 0;
    while (mon < 12 && days >= days_per_month[leap][mon]) {
        days -= days_per_month[leap][mon];
        mon++;
    }
    result->tm_mon = mon;
    result->tm_mday = (int)(days + 1);
    result->tm_isdst = 0;
    result->tm_gmtoff = 0;
    return result;
}

struct tm *localtime(const time_t *timer) {
    return localtime_into(timer, &tm_buffer);
}

struct tm *localtime_r(const time_t *timer, struct tm *result) {
    return localtime_into(timer, result);
}
