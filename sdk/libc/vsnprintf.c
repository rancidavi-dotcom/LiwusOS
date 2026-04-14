#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    size_t written = 0;
    const char *p = format;
    char *dst = str;

    while (*p && written < size - 1) {
        if (*p == '%' && *(p + 1)) {
            p++;
            if (*p == 's') {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && written < size - 1) {
                    *dst++ = *s++;
                    written++;
                }
            } else if (*p == 'd') {
                int n = va_arg(ap, int);
                char buf[32];
                itoa(n, buf, 10);
                char *s = buf;
                while (*s && written < size - 1) {
                    *dst++ = *s++;
                    written++;
                }
            } else if (*p == 'c') {
                *dst++ = (char)va_arg(ap, int);
                written++;
            } else if (*p == '%') {
                *dst++ = '%';
                written++;
            } else {
                *dst++ = *p;
                written++;
            }
        } else {
            *dst++ = *p;
            written++;
        }
        p++;
    }
    *dst = '\0';
    return (int)written;
}
