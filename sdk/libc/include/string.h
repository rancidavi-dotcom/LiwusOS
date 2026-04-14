#ifndef LIWLIB_STRING_H
#define LIWLIB_STRING_H

#include <stddef.h>

#ifndef _PDCLIB_restrict
#define _PDCLIB_restrict restrict
#endif

void *memchr(const void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memcpy(void *restrict s1, const void *restrict s2, size_t n);
void *memmove(void *s1, const void *s2, size_t n);
void *memset(void *s, int c, size_t n);
char *strcat(char *restrict s1, const char *restrict s2);
char *strchr(const char *s, int c);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *restrict s1, const char *restrict s2);
int strcoll(const char *s1, const char *s2);
size_t strcspn(const char *s1, const char *s2);
size_t strlen(const char *s);
int strncmp(const char *s1, const char *s2, size_t n);
char *strncpy(char *restrict s1, const char *restrict s2, size_t n);
char *strpbrk(const char *s1, const char *s2);
char *strrchr(const char *s, int c);
char *strerror(int errnum);
size_t strspn(const char *s1, const char *s2);
char *strstr(const char *s1, const char *s2);

#endif
