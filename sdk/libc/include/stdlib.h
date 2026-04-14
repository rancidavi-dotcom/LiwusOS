#ifndef LIWLIB_STDLIB_H
#define LIWLIB_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void exit(int status);
void abort(void);
char *itoa(int value, char *str, int base);
int atoi(const char *s);
double atof(const char *s);
double strtod(const char *nptr, char **endptr);
int abs(int value);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
char *getenv(const char *name);
char *strdup(const char *s);
int system(const char *command);

#endif
