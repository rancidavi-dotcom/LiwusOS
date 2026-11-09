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
  uint32_t *d = (uint32_t *)dest;
  const uint32_t *s = (const uint32_t *)src;
  
  // Se estiverem alinhados em 4 bytes, podemos usar cópia de 32 bits
  if (((uintptr_t)dest % 4 == 0) && ((uintptr_t)src % 4 == 0)) {
    size_t dwords = len >> 2;
    size_t bytes = len & 3;
    while (dwords--)
      *d++ = *s++;
    
    char *d8 = (char *)d;
    const char *s8 = (const char *)s;
    while (bytes--)
      *d8++ = *s8++;
  } else {
    char *d8 = (char *)dest;
    const char *s8 = (const char *)src;
    while (len--)
      *d8++ = *s8++;
  }
  return dest;
}

void *memmove(void *dest, const void *src, size_t len) {
  if (dest == src || len == 0) return dest;
  char *d = (char *)dest;
  const char *s = (const char *)src;
  if (d < s) {
    if (((uintptr_t)d % 8 == 0) && ((uintptr_t)s % 8 == 0)) {
      size_t qwords = len >> 3;
      uint64_t *d64 = (uint64_t *)d;
      const uint64_t *s64 = (const uint64_t *)s;
      while (qwords--)
        *d64++ = *s64++;
      d = (char *)d64;
      s = (const char *)s64;
      len &= 7;
    }
    while (len--)
      *d++ = *s++;
  } else {
    if (((uintptr_t)d % 8 == 0) && ((uintptr_t)s % 8 == 0) && (len >= 8)) {
      size_t qwords = len >> 3;
      size_t rem = len & 7;
      char *lasts = (char *)s + (len - 1);
      char *lastd = d + (len - 1);
      while (rem--)
        *lastd-- = *lasts--;
      uint64_t *lastd64 = (uint64_t *)(d + (qwords << 3));
      const uint64_t *lasts64 = (const uint64_t *)(s + (qwords << 3));
      while (qwords--)
        *--lastd64 = *--lasts64;
      return dest;
    }
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

long strtol(const char *nptr, char **endptr, int base) {
  (void)base;
  long val = 0;
  int sign = 1;
  while (*nptr == ' ' || *nptr == '\t') nptr++;
  if (*nptr == '-') { sign = -1; nptr++; }
  else if (*nptr == '+') { nptr++; }
  while (*nptr >= '0' && *nptr <= '9') {
    val = val * 10 + (*nptr++ - '0');
  }
  if (endptr) *endptr = (char *)nptr;
  return val * sign;
}

int atoi(const char *nptr) {
  return (int)strtol(nptr, (void *)0, 10);
}
