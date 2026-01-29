#include "string.h"

size_t strlen(const char* str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (*haystack != *needle) continue;
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return (void*)0;
}

void* memset(void* dest, int val, size_t len) {
    unsigned char* ptr = dest;
    while (len-- > 0) *ptr++ = (unsigned char)val;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    unsigned char* d = dest;
    const unsigned char* s = src;
    while (len-- > 0) *d++ = *s++;
    return dest;
}

void hex_to_str(uint8_t val, char* out) {
    const char* hex = "0123456789ABCDEF";
    out[0] = hex[(val >> 4) & 0x0F];
    out[1] = hex[val & 0x0F];
    out[2] = '\0';
}

void int_to_str(uint32_t n, char* out) {
    if (n == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    char buf[11];
    int i = 10;
    buf[i--] = '\0';
    while (n > 0) {
        buf[i--] = (n % 10) + '0';
        n /= 10;
    }
    strcpy(out, &buf[i + 1]);
}