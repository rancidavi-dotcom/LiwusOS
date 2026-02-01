#include "string.h"

size_t strlen(const char *str) {
  if (!str)
    return 0;
  size_t len = 0;
  while (str[len])
    len++;
  return len;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && (*s1 == *s2)) {
    ++s1;
    ++s2;
    --n;
  }
  if (n == 0) {
    return 0;
  } else {
    return *(unsigned char *)s1 - *(unsigned char *)s2;
  }
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
  char *d = dest;
  while (*d)
    d++;
  while ((*d++ = *src++))
    ;
  return dest;
}

char *strstr(const char *haystack, const char *needle) {
  if (!*needle)
    return (char *)haystack;
  for (; *haystack; haystack++) {
    if (*haystack != *needle)
      continue;
    const char *h = haystack, *n = needle;
    while (*h && *n && *h == *n) {
      h++;
      n++;
    }
    if (!*n)
      return (char *)haystack;
  }
  return (void *)0;
}

char *strchr(const char *s, int c) {
  while (*s != (char)c) {
    if (!*s++)
      return (void *)0;
  }
  return (char *)s;
}

void *memset(void *dest, int val, size_t len) {
  uint8_t *ptr = (uint8_t *)dest;
  uint32_t val32 = (uint8_t)val;
  val32 |= val32 << 8;
  val32 |= val32 << 16;
  val32 |= val32 << 24;

  while (len > 0 && ((uintptr_t)ptr & 3) != 0) {
    *ptr++ = (uint8_t)val;
    len--;
  }

  uint32_t *ptr32 = (uint32_t *)ptr;
  while (len >= 4) {
    *ptr32++ = val32;
    len -= 4;
  }

  ptr = (uint8_t *)ptr32;
  while (len > 0) {
    *ptr++ = (uint8_t)val;
    len--;
  }
  return dest;
}

void *memset32(void *dest, uint32_t val, size_t len) {
  uint32_t *ptr = (uint32_t *)dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}

void *memcpy(void *dest, const void *src, size_t len) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  if (((uintptr_t)d & 3) == ((uintptr_t)s & 3)) {
    while (len > 0 && ((uintptr_t)d & 3) != 0) {
      *d++ = *s++;
      len--;
    }
    uint32_t *d32 = (uint32_t *)d;
    const uint32_t *s32 = (const uint32_t *)s;
    while (len >= 4) {
      *d32++ = *s32++;
      len -= 4;
    }
    d = (uint8_t *)d32;
    s = (uint8_t *)s32;
  }

  while (len-- > 0)
    *d++ = *s++;
  return dest;
}

void *memmove(void *dest, const void *src, size_t len) {
  unsigned char *d = dest;
  const unsigned char *s = src;
  if (d < s) {
    while (len--)
      *d++ = *s++;
  } else {
    d += len;
    s += len;
    while (len--)
      *--d = *--s;
  }
  return dest;
}

void hex_to_str(uint8_t val, char *out) {
  const char *hex = "0123456789ABCDEF";
  out[0] = hex[(val >> 4) & 0x0F];
  out[1] = hex[val & 0x0F];
  out[2] = '\0';
}

void int_to_str(uint32_t n, char *out) {
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

char *itoa(int value, char *str, int base) {
  if (base < 2 || base > 36) {
    *str = '\0';
    return str;
  }
  char *ptr = str, *ptr1 = str, tmp_char;
  int tmp_value;

  uint32_t v = (uint32_t)value;
  if (value < 0 && base == 10) {
    v = (uint32_t)-value;
    *ptr++ = '-';
    ptr1++;
  }

  do {
    tmp_value = v;
    v /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrst"
             "uvwxyz"[35 + (tmp_value - v * base)];
  } while (v);

  *ptr-- = '\0';
  while (ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr-- = *ptr1;
    *ptr1++ = tmp_char;
  }
  return str;
}