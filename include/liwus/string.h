#ifndef LIWUS_STRING_H
#define LIWUS_STRING_H

#include <stddef.h>

void *liw_memcpy(void *destination, const void *source, size_t count);
void *liw_memset(void *destination, int value, size_t count);
size_t liw_strlen(const char *text);
int liw_strcmp(const char *left, const char *right);

#endif
