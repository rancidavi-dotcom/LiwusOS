#include "liwus/string.h"
#include "liwus/stdlib.h"

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

/* Standard aliases */
void *memcpy(void *dest, const void *src, size_t n) {
    return liw_memcpy(dest, src, n);
}

void *memset(void *s, int c, size_t n) {
    return liw_memset(s, c, n);
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char target = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == target) return (void *)(p + i);
    }
    return NULL;
}

size_t strlen(const char *s) {
    return liw_strlen(s);
}

int strcmp(const char *s1, const char *s2) {
    return liw_strcmp(s1, s2);
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

static unsigned char lower_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

int strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : s2 ? -1 : 0;
    while (*s1 && lower_ascii((unsigned char)*s1) == lower_ascii((unsigned char)*s2)) {
        s1++;
        s2++;
    }
    return lower_ascii((unsigned char)*s1) - lower_ascii((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = lower_ascii((unsigned char)s1[i]);
        unsigned char c2 = lower_ascii((unsigned char)s2[i]);
        if (c1 != c2 || s1[i] == '\0') {
            return (int)c1 - (int)c2;
        }
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    char *orig = dest;
    while ((*dest++ = *src++));
    return orig;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *orig = dest;
    for (size_t i = 0; i < n; i++) {
        if (*src) *dest++ = *src++;
        else *dest++ = '\0';
    }
    return orig;
}

char *strcat(char *dest, const char *src) {
    char *orig = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return orig;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *orig = dest;
    while (*dest) dest++;
    for (size_t i = 0; i < n && *src; i++) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return orig;
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    return (ch == '\0') ? (char *)s : (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

char *strpbrk(const char *s, const char *accept) {
    if (!s || !accept) return NULL;
    for (; *s; s++) {
        for (const char *a = accept; *a; a++) {
            if (*s == *a) return (char *)s;
        }
    }
    return NULL;
}

char *strerror(int errnum) {
    switch (errnum) {
        case 0: return "Success";
        case 1: return "Operation not permitted";
        case 2: return "No such file or directory";
        case 5: return "Input/output error";
        case 9: return "Bad file descriptor";
        case 12: return "Out of memory";
        case 13: return "Permission denied";
        case 14: return "Bad address";
        case 16: return "Device or resource busy";
        case 17: return "File exists";
        case 20: return "Not a directory";
        case 21: return "Is a directory";
        case 22: return "Invalid argument";
        case 28: return "No space left on device";
        case 32: return "Broken pipe";
        case 34: return "Numerical result out of range";
        case 38: return "Function not implemented";
        default: return "Unknown error";
    }
}
