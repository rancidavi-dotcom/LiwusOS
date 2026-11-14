#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include <stdarg.h>

static void out_char(char **buf, size_t *rem, size_t *total, char c) {
    if (*buf && *rem > 1) {
        **buf = c;
        (*buf)++;
        (*rem)--;
    }
    (*total)++;
}

static void out_str_len(char **buf, size_t *rem, size_t *total, const char *s, int max_len) {
    if (!s) s = "(null)";
    int i = 0;
    while (*s && (max_len < 0 || i < max_len)) {
        out_char(buf, rem, total, *s++);
        i++;
    }
}

static void out_str(char **buf, size_t *rem, size_t *total, const char *s) {
    out_str_len(buf, rem, total, s, -1);
}

static void out_number(char **buf, size_t *rem, size_t *total, uint64_t val, int base,
                       int is_signed, int upper, int width, int prec, int zero_pad, int left_align) {
    int neg = 0;
    if (is_signed && (int64_t)val < 0) {
        neg = 1;
        val = (uint64_t)(-(int64_t)val);
    }
    char digits[32];
    int idx = 0;
    const char *chars = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (!(prec == 0 && val == 0)) {
        do {
            digits[idx++] = chars[val % base];
            val /= base;
        } while (val);
    }

    int prec_zeros = (prec > idx) ? (prec - idx) : 0;
    if (prec >= 0) zero_pad = 0;
    int content_len = idx + prec_zeros + neg;
    int pad = (width > content_len) ? (width - content_len) : 0;

    if (!left_align && !zero_pad) {
        while (pad-- > 0) out_char(buf, rem, total, ' ');
    }
    if (neg) out_char(buf, rem, total, '-');
    if (!left_align && zero_pad) {
        while (pad-- > 0) out_char(buf, rem, total, '0');
    }
    while (prec_zeros-- > 0) out_char(buf, rem, total, '0');
    while (idx > 0) {
        out_char(buf, rem, total, digits[--idx]);
    }
    if (left_align) {
        while (pad-- > 0) out_char(buf, rem, total, ' ');
    }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    char *p = str;
    size_t rem = size;
    size_t total = 0;

    while (*format) {
        if (*format != '%') {
            out_char(&p, &rem, &total, *format++);
            continue;
        }
        format++; // pula '%'
        if (*format == '\0') break;

        int left_align = 0;
        int zero_pad = 0;
        while (*format == '-' || *format == '0' || *format == '+' || *format == ' ') {
            if (*format == '-') left_align = 1;
            else if (*format == '0') zero_pad = 1;
            format++;
        }
        if (left_align) zero_pad = 0;

        int width = 0;
        if (*format == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left_align = 1;
                width = -width;
            }
            format++;
        } else {
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
        }

        int prec = -1;
        if (*format == '.') {
            format++;
            prec = 0;
            if (*format == '*') {
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;
                format++;
            } else {
                while (*format >= '0' && *format <= '9') {
                    prec = prec * 10 + (*format - '0');
                    format++;
                }
            }
        }

        int is_long = 0;
        if (*format == 'l') {
            is_long = 1;
            format++;
            if (*format == 'l') {
                is_long = 2;
                format++;
            }
        } else if (*format == 'z') {
            is_long = 1;
            format++;
        }

        switch (*format) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int slen = 0;
                while (s[slen] && (prec < 0 || slen < prec)) slen++;
                int pad = (width > slen) ? (width - slen) : 0;
                if (!left_align) {
                    while (pad-- > 0) out_char(&p, &rem, &total, ' ');
                }
                out_str_len(&p, &rem, &total, s, prec);
                if (left_align) {
                    while (pad-- > 0) out_char(&p, &rem, &total, ' ');
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                out_char(&p, &rem, &total, c);
                break;
            }
            case 'd':
            case 'i': {
                int64_t val = (is_long == 2) ? va_arg(ap, long long) :
                              (is_long == 1) ? va_arg(ap, long) : va_arg(ap, int);
                out_number(&p, &rem, &total, (uint64_t)val, 10, 1, 0, width, prec, zero_pad, left_align);
                break;
            }
            case 'u': {
                uint64_t val = (is_long == 2) ? va_arg(ap, unsigned long long) :
                               (is_long == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                out_number(&p, &rem, &total, val, 10, 0, 0, width, prec, zero_pad, left_align);
                break;
            }
            case 'x': {
                uint64_t val = (is_long == 2) ? va_arg(ap, unsigned long long) :
                               (is_long == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                out_number(&p, &rem, &total, val, 16, 0, 0, width, prec, zero_pad, left_align);
                break;
            }
            case 'X': {
                uint64_t val = (is_long == 2) ? va_arg(ap, unsigned long long) :
                               (is_long == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                out_number(&p, &rem, &total, val, 16, 0, 1, width, prec, zero_pad, left_align);
                break;
            }
            case 'p': {
                uint64_t val = (uint64_t)va_arg(ap, void *);
                out_str(&p, &rem, &total, "0x");
                out_number(&p, &rem, &total, val, 16, 0, 0, 0, -1, 0, 0);
                break;
            }
            case '%': {
                out_char(&p, &rem, &total, '%');
                break;
            }
            default:
                out_char(&p, &rem, &total, *format);
                break;
        }
        format++;
    }

    if (str && size > 0) {
        if (rem > 0) {
            *p = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }
    return (int)total;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap);
    return res;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, (size_t)-1, format, ap);
    va_end(ap);
    return res;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, (size_t)-1, format, ap);
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[1024];
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(buf, sizeof(buf), format, ap2);
    va_end(ap2);
    if (len < (int)sizeof(buf)) {
        fwrite(buf, 1, len, stream);
    } else {
        // Aloca buffer sob demanda para mensagens longas
        char *dynamic = (char *)malloc(len + 1);
        if (dynamic) {
            vsnprintf(dynamic, len + 1, format, ap);
            fwrite(dynamic, 1, len, stream);
            free(dynamic);
        }
    }
    return len;
}

int vprintf(const char *format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vfprintf(stream, format, ap);
    va_end(ap);
    return res;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vfprintf(stdout, format, ap);
    va_end(ap);
    return res;
}
