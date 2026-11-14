#ifndef LIWUS_STDLIB_H
#define LIWUS_STDLIB_H

#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

int atoi(const char *nptr);
long atol(const char *nptr);
double atof(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);

void exit(int status);
void abort(void);
int abs(int j);
int system(const char *command);
char *getenv(const char *name);
extern char **environ;

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
char *realpath(const char *path, char *resolved_path);

#endif
