#ifndef LIWLIB_TIME_H
#define LIWLIB_TIME_H

#include <sys/types.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 100

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

time_t time(time_t *tloc);
clock_t clock(void);
struct tm *gmtime(const time_t *timep);
struct tm *localtime(const time_t *timep);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
time_t mktime(struct tm *tm);
double difftime(time_t time1, time_t time0);

#endif
