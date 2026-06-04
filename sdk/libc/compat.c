#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern unsigned int __liw_sys_get_ticks(void);

static struct lconv global_lconv = {"."};

__attribute__((naked)) int setjmp(jmp_buf env) {
  (void)env;
  __asm__ volatile(
      "mov 4(%esp), %edx\n"
      "mov %ebx, 0(%edx)\n"
      "mov %esi, 4(%edx)\n"
      "mov %edi, 8(%edx)\n"
      "mov %ebp, 12(%edx)\n"
      "lea 4(%esp), %ecx\n"
      "mov %ecx, 16(%edx)\n"
      "mov (%esp), %ecx\n"
      "mov %ecx, 20(%edx)\n"
      "xor %eax, %eax\n"
      "ret\n");
}

__attribute__((naked)) void longjmp(jmp_buf env, int val) {
  (void)env;
  (void)val;
  __asm__ volatile(
      "mov 4(%esp), %edx\n"
      "mov 8(%esp), %eax\n"
      "test %eax, %eax\n"
      "jnz 1f\n"
      "mov $1, %eax\n"
      "1:\n"
      "mov 0(%edx), %ebx\n"
      "mov 4(%edx), %esi\n"
      "mov 8(%edx), %edi\n"
      "mov 12(%edx), %ebp\n"
      "mov 16(%edx), %esp\n"
      "jmp *20(%edx)\n");
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
