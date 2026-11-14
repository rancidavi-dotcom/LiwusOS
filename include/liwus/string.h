#ifndef LIWUS_STRING_H
#define LIWUS_STRING_H

#include <stddef.h>

void *liw_memcpy(void *destination, const void *source, size_t count);
void *liw_memset(void *destination, int value, size_t count);
size_t liw_strlen(const char *text);
int liw_strcmp(const char *left, const char *right);

/* Standard C aliases and additions */
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strdup(const char *s);
char *strpbrk(const char *s, const char *accept);
char *strerror(int errnum);

#endif
