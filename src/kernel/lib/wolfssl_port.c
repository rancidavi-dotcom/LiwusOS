#include <stddef.h>
#include <stdint.h>
#include "kheap.h"
#include "kernel/timer.h"

/* Minimal struct tm definition for gmtime_r */
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
};

typedef long time_t;

typedef unsigned char byte;
typedef unsigned int word32;

/* Simple gmtime implementation - converts time_t to UTC struct tm */
struct tm *gmtime(const time_t *timer) {
    static struct tm result;
    time_t t = *timer;
    
    /* Days since epoch */
    time_t days = t / 86400;
    time_t rem = t % 86400;
    
    result.tm_hour = rem / 3600;
    rem %= 3600;
    result.tm_min = rem / 60;
    result.tm_sec = rem % 60;
    
    /* Days since epoch */
    int wday = (4 + days) % 7; /* 1970-01-01 was Thursday */
    if (wday < 0) wday += 7;
    result.tm_wday = wday;
    
    /* Year calculation */
    int year = 1970;
    while (days >= 365) {
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (days < leap) break;
        days -= leap;
        year++;
    }
    result.tm_year = year - 1900;
    result.tm_yday = days;
    
    /* Month calculation */
    static const int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
    int month = 0;
    while (month < 12) {
        int mdays = month_days[month] + (month == 1 && leap ? 1 : 0);
        if (days < mdays) break;
        days -= mdays;
        month++;
    }
    result.tm_mon = month;
    result.tm_mday = days + 1;
    
    return &result;
}

typedef unsigned char byte;
typedef unsigned int word32;

int liwusos_generate_seed(byte *output, word32 sz) {
    for (word32 i = 0; i < sz; i += sizeof(uint32_t)) {
        uint64_t entropy = timer_ticks;
        entropy ^= (entropy << 13);
        entropy ^= (entropy >> 7);
        size_t chunk = sz - i < sizeof(uint32_t) ? sz - i : sizeof(uint32_t);
        __builtin_memcpy(output + i, &entropy, chunk);
    }
    return 0;
}

int wc_GenerateSeed(void *seed, byte *output, word32 sz) {
    (void)seed;
    for (word32 i = 0; i < sz; i += sizeof(uint32_t)) {
        uint64_t entropy = timer_ticks;
        entropy ^= (entropy << 13);
        entropy ^= (entropy >> 7);
        size_t chunk = sz - i < sizeof(uint32_t) ? sz - i : sizeof(uint32_t);
        __builtin_memcpy(output + i, &entropy, chunk);
    }
    return 0;
}

struct tm *liwusos_gmtime_r(const time_t *timer, struct tm *result) {
    struct tm *tmp = gmtime(timer);
    if (!tmp) return NULL;
    *result = *tmp;
    return result;
}

/* POSIX stubs for BearSSL */

time_t time(time_t *t) {
    time_t now = (time_t)(timer_ticks / 100);
    if (t) *t = now;
    return now;
}

int *__errno_location(void) {
    static int errno_val = 0;
    return &errno_val;
}

int open(const char *pathname, int flags, ...) {
    (void)pathname;
    (void)flags;
    return -1; /* Not supported in kernel */
}

int read(int fd, void *buf, size_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -1; /* Not supported in kernel */
}

int close(int fd) {
    (void)fd;
    return -1; /* Not supported in kernel */
}