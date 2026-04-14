#include "string.h"
#include <stddef.h>
#include <stdint.h>

size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len])
    len++;
  return len;
}

char *strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];
  for (; i < n; i++)
    dest[i] = '\0';
  return dest;
}

char *strcat(char *dest, const char *src) {
  char *rdest = dest;
  while (*dest)
    dest++;
  while ((*dest++ = *src++))
    ;
  return rdest;
}

char *strncat(char *dest, const char *src, size_t n) {
  size_t dest_len = strlen(dest);
  size_t i;
  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[dest_len + i] = src[i];
  dest[dest_len + i] = '\0';
  return dest;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s1 == *s2) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strchr(const char *s, int c) {
  while (*s) {
    if (*s == (char)c)
      return (char *)s;
    s++;
  }
  if (c == '\0')
    return (char *)s;
  return NULL;
}

char *strrchr(const char *s, int c) {
    char *last = NULL;
    do {
        if (*s == (char)c) last = (char *)s;
    } while (*s++);
    return last;
}

char *strstr(const char *haystack, const char *needle) {
  if (!*needle)
    return (char *)haystack;
  for (; *haystack; haystack++) {
    if (*haystack != *needle)
      continue;
    const char *h = haystack;
    const char *n = needle;
    while (*h && *n && *h == *n) {
      h++;
      n++;
    }
    if (!*n)
      return (char *)haystack;
  }
  return NULL;
}

void *memset(void *dest, int val, size_t len) {
  unsigned char *ptr = (unsigned char *)dest;
  while (len-- > 0)
    *ptr++ = (unsigned char)val;
  return dest;
}

void *memset32(void *dest, uint32_t val, size_t len) {
  uint32_t *ptr = (uint32_t *)dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}

void *memcpy(void *dest, const void *src, size_t len) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  while (len--)
    *d++ = *s++;
  return dest;
}

void *memmove(void *dest, const void *src, size_t len) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  if (d < s) {
    while (len--)
      *d++ = *s++;
  } else {
    char *lasts = (char *)s + (len - 1);
    char *lastd = d + (len - 1);
    while (len--)
      *lastd-- = *lasts--;
  }
  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  while (n--) {
    if (*p1 != *p2)
      return *p1 - *p2;
    p1++;
    p2++;
  }
  return 0;
}

char *itoa(int n, char *s, int base) {
  static char digits[] = "0123456789ABCDEF";
  char buf[32];
  int i = 0, sign;
  if ((sign = n) < 0 && base == 10)
    n = -n;
  do {
    buf[i++] = digits[n % base];
  } while ((n /= base) > 0);
  if (sign < 0 && base == 10)
    buf[i++] = '-';
  int j = 0;
  while (i > 0)
    s[j++] = buf[--i];
  s[j] = '\0';
  return s;
}

void int_to_str(uint32_t n, char *s) {
    itoa((int)n, s, 10);
}

void hex_to_str(uint8_t n, char *out) {
  static char digits[] = "0123456789ABCDEF";
  out[0] = digits[(n >> 4) & 0xF];
  out[1] = digits[n & 0xF];
  out[2] = '\0';
}
