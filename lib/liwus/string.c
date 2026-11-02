#include "liwus/string.h"

void *liw_memcpy(void *destination, const void *source, size_t count) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    for (size_t i = 0; i < count; i++) out[i] = in[i];
    return destination;
}

void *liw_memset(void *destination, int value, size_t count) {
    unsigned char *out = (unsigned char *)destination;
    for (size_t i = 0; i < count; i++) out[i] = (unsigned char)value;
    return destination;
}

size_t liw_strlen(const char *text) {
    size_t length = 0;
    if (!text) return 0;
    while (text[length]) length++;
    return length;
}

int liw_strcmp(const char *left, const char *right) {
    if (!left || !right) return left ? 1 : right ? -1 : 0;
    while (*left && *left == *right) { left++; right++; }
    return (unsigned char)*left - (unsigned char)*right;
}
