#include <string.h>

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c)
            return (void *)p;
        ++p;
    }
    return NULL;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        ++p1; ++p2;
    }
    return 0;
}

void *memcpy(void *restrict s1, const void *restrict s2, size_t n)
{
    char *dest = s1;
    const char *src = s2;
    while (n--) *dest++ = *src++;
    return s1;
}

void *memmove(void *s1, const void *s2, size_t n)
{
    char *dest = s1;
    const char *src = s2;
    if (dest <= src) {
        while (n--) *dest++ = *src++;
    } else {
        src += n; dest += n;
        while (n--) *--dest = *--src;
    }
    return s1;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

char *strcat(char *restrict s1, const char *restrict s2)
{
    char *p = s1;
    while (*p) ++p;
    while ((*p++ = *s2++));
    return s1;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        ++s;
    }
    return (c == '\0') ? (char *)s : NULL;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) { ++s1; ++s2; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char *strcpy(char *restrict s1, const char *restrict s2)
{
    char *p = s1;
    while ((*p++ = *s2++));
    return s1;
}

size_t strcspn(const char *s1, const char *s2)
{
    size_t n = 0;
    while (*s1) {
        const char *p = s2;
        while (*p) { if (*s1 == *p++) return n; }
        ++s1; ++n;
    }
    return n;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) ++n;
    return n;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n-- && *s1 && *s1 == *s2) { ++s1; ++s2; }
    return (n == (size_t)-1) ? 0 : ((unsigned char)*s1 - (unsigned char)*s2);
}

char *strncpy(char *restrict s1, const char *restrict s2, size_t n)
{
    char *p = s1;
    while (n-- > 0) {
        *p++ = *s2 ? *s2++ : '\0';
    }
    return s1;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        ++s;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

size_t strspn(const char *s1, const char *s2)
{
    size_t n = 0;
    while (*s1) {
        const char *p = s2;
        int found = 0;
        while (*p) { if (*s1 == *p++) { found = 1; break; } }
        if (!found) break;
        ++s1; ++n;
    }
    return n;
}

char *strstr(const char *s1, const char *s2)
{
    if (!*s2) return (char *)s1;
    while (*s1) {
        const char *p1 = s1, *p2 = s2;
        while (*p1 && *p2 && *p1 == *p2) { ++p1; ++p2; }
        if (!*p2) return (char *)s1;
        ++s1;
    }
    return NULL;
}
