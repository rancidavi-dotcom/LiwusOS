#include "liwus/stdlib.h"
#include <stddef.h>

double atof(const char *nptr) {
    return strtod(nptr, NULL);
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, NULL, 10);
}

long atol(const char *nptr) {
    return strtol(nptr, NULL, 10);
}

static int char_to_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                s++;
                base = 16;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    unsigned long long result = 0;
    const char *start = s;
    for (;;) {
        int d = char_to_digit(*s);
        if (d < 0 || d >= base) break;
        result = result * (unsigned long long)base + (unsigned long long)d;
        s++;
    }

    if (endptr) {
        *endptr = (char *)(s == start ? nptr : s);
    }
    return result;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtoull(nptr, endptr, base);
}

long long strtoll(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    unsigned long long res = strtoull(s, endptr, base);
    return neg ? -(long long)res : (long long)res;
}

long strtol(const char *nptr, char **endptr, int base) {
    return (long)strtoll(nptr, endptr, base);
}

double strtod(const char *nptr, char **endptr) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    double res = 0.0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10.0 + (*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            res += (*s - '0') * frac;
            frac *= 0.1;
            s++;
        }
    }

    if (endptr) *endptr = (char *)s;
    return neg ? -res : res;
}
