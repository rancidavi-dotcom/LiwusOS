#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern unsigned int __liw_sys_get_ticks(void);

static struct lconv global_lconv = {"."};

int setjmp(jmp_buf env) { return __builtin_setjmp(env); }

void longjmp(jmp_buf env, int val) {
  (void)val;
  __builtin_longjmp(env, 1);
}

char *setlocale(int category, const char *locale) {
  (void)category;
  (void)locale;
  return "C";
}

struct lconv *localeconv(void) { return &global_lconv; }

time_t time(time_t *tloc) {
  time_t now = (time_t)(__liw_sys_get_ticks() / 100U);
  if (tloc) {
    *tloc = now;
  }
  return now;
}

clock_t clock(void) { return (clock_t)__liw_sys_get_ticks(); }

static struct tm static_tm;

struct tm *gmtime(const time_t *timep) {
  (void)timep;
  return &static_tm;
}

struct tm *localtime(const time_t *timep) {
  (void)timep;
  return &static_tm;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
  (void)tm;
  (void)format;
  if (max > 0) s[0] = '\0';
  return 0;
}

time_t mktime(struct tm *tm) {
  (void)tm;
  return 0;
}

double difftime(time_t time1, time_t time0) {
  return (double)(time1 - time0);
}

int isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }

sighandler_t signal(int sig, sighandler_t handler) {
  (void)sig;
  return handler;
}
