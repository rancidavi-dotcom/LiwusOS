#include <ctype.h>
#include <strings.h>

int strcasecmp(const char *s1, const char *s2) {
  while (*s1 && tolower((unsigned char)*s1) == tolower((unsigned char)*s2)) {
    ++s1;
    ++s2;
  }

  return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 &&
         tolower((unsigned char)*s1) == tolower((unsigned char)*s2)) {
    ++s1;
    ++s2;
    --n;
  }

  if (n == 0) {
    return 0;
  }

  return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}
