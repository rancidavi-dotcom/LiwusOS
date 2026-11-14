#include "liwus/stdio.h"
#include "liwus/string.h"
#include <stdarg.h>
#include <stdint.h>

typedef struct {
    const char *str;
    size_t pos;
    FILE *file;
    int has_unget;
    int unget_char;
} scan_src_t;

static int scan_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int scan_digit_val(int c, int base) {
    int v = -1;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    return (v >= 0 && v < base) ? v : -1;
}

static int scan_getc(scan_src_t *src) {
    if (src->has_unget) {
        src->has_unget = 0;
        return src->unget_char;
    }
    if (src->str) {
        unsigned char c = (unsigned char)src->str[src->pos];
        if (c == '\0') return EOF;
        src->pos++;
        return (int)c;
    }
    return fgetc(src->file);
}

static void scan_ungetc(scan_src_t *src, int c) {
    if (c == EOF) return;
    src->has_unget = 1;
    src->unget_char = c;
}

static void scan_restore_file_unget(scan_src_t *src) {
    if (src->file && src->has_unget && src->file->buf_pos > 0) {
        src->file->buf_pos--;
        src->has_unget = 0;
    }
}

static void skip_ws(scan_src_t *src) {
    int c;
    while ((c = scan_getc(src)) != EOF && scan_isspace(c)) {
    }
    scan_ungetc(src, c);
}

static int vscan_common(scan_src_t *src, const char *fmt, va_list ap) {
    int assigned = 0;
    int any_input = 0;

    while (*fmt) {
        if (scan_isspace((unsigned char)*fmt)) {
            while (scan_isspace((unsigned char)*fmt)) fmt++;
            skip_ws(src);
            continue;
        }

        if (*fmt != '%') {
            int c = scan_getc(src);
            if (c == EOF) return assigned > 0 ? assigned : (any_input ? 0 : EOF);
            any_input = 1;
            if (c != (unsigned char)*fmt) {
                scan_ungetc(src, c);
                return assigned;
            }
            fmt++;
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            int c = scan_getc(src);
            if (c == EOF) return assigned > 0 ? assigned : (any_input ? 0 : EOF);
            any_input = 1;
            if (c != '%') {
                scan_ungetc(src, c);
                return assigned;
            }
            fmt++;
            continue;
        }

        int suppress = 0;
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        int len_mod = 0; /* 1 = l, 2 = ll, -1 = h */
        if (*fmt == 'l') {
            len_mod = 1;
            fmt++;
            if (*fmt == 'l') {
                len_mod = 2;
                fmt++;
            }
        } else if (*fmt == 'h') {
            len_mod = -1;
            fmt++;
        }

        char conv = *fmt;
        if (!conv) break;

        if (conv == 'c') {
            int n = width > 0 ? width : 1;
            char *dst = suppress ? NULL : va_arg(ap, char *);
            int count = 0;
            while (count < n) {
                int c = scan_getc(src);
                if (c == EOF) break;
                any_input = 1;
                if (dst) dst[count] = (char)c;
                count++;
            }
            if (count < n) return assigned > 0 ? assigned : (any_input ? 0 : EOF);
            if (!suppress) assigned++;
            fmt++;
            continue;
        }

        if (conv == '[') {
            fmt++;
            int negate = 0;
            unsigned char table[256];
            memset(table, 0, sizeof(table));
            if (*fmt == '^') {
                negate = 1;
                fmt++;
            }
            int first = 1;
            while (*fmt && (*fmt != ']' || first)) {
                first = 0;
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    unsigned char a = (unsigned char)fmt[0];
                    unsigned char b = (unsigned char)fmt[2];
                    if (a > b) {
                        unsigned char tmp = a;
                        a = b;
                        b = tmp;
                    }
                    for (int ch = a; ch <= b; ch++) table[ch] = 1;
                    fmt += 3;
                } else {
                    table[(unsigned char)*fmt] = 1;
                    fmt++;
                }
            }
            if (*fmt == ']') fmt++;

            char *dst = suppress ? NULL : va_arg(ap, char *);
            int count = 0;
            int max_w = width > 0 ? width : 0x7FFFFFFF;
            while (count < max_w) {
                int c = scan_getc(src);
                if (c == EOF) break;
                any_input = 1;
                int in_set = table[(unsigned char)c] != 0;
                if (negate ? in_set : !in_set) {
                    scan_ungetc(src, c);
                    break;
                }
                if (dst) dst[count] = (char)c;
                count++;
            }
            if (count == 0) return assigned > 0 ? assigned : (any_input ? 0 : EOF);
            if (dst) dst[count] = '\0';
            if (!suppress) assigned++;
            continue;
        }

        skip_ws(src);
        if (conv == 's') {
            char *dst = suppress ? NULL : va_arg(ap, char *);
            int count = 0;
            int max_w = width > 0 ? width : 0x7FFFFFFF;
            while (count < max_w) {
                int c = scan_getc(src);
                if (c == EOF) break;
                any_input = 1;
                if (scan_isspace(c)) {
                    scan_ungetc(src, c);
                    break;
                }
                if (dst) dst[count] = (char)c;
                count++;
            }
            if (count == 0) return assigned > 0 ? assigned : (any_input ? 0 : EOF);
            if (dst) dst[count] = '\0';
            if (!suppress) assigned++;
            fmt++;
            continue;
        }

        int base = 10;
        int is_signed = 0;
        if (conv == 'd') {
            base = 10;
            is_signed = 1;
        } else if (conv == 'i') {
            base = 0;
            is_signed = 1;
        } else if (conv == 'u') {
            base = 10;
        } else if (conv == 'x' || conv == 'X') {
            base = 16;
        } else if (conv == 'o') {
            base = 8;
        } else {
            return assigned;
        }

        int max_w = width > 0 ? width : 0x7FFFFFFF;
        int consumed = 0;
        int c = scan_getc(src);
        if (c == EOF) return assigned > 0 ? assigned : (any_input ? 0 : EOF);
        any_input = 1;

        int neg = 0;
        if ((c == '-' || c == '+') && consumed < max_w) {
            neg = (c == '-');
            consumed++;
            c = scan_getc(src);
        }

        if (base == 0) {
            if (c == '0' && consumed < max_w) {
                int c2 = scan_getc(src);
                if ((c2 == 'x' || c2 == 'X') && consumed + 1 < max_w) {
                    base = 16;
                    consumed += 2;
                    c = scan_getc(src);
                } else {
                    scan_ungetc(src, c2);
                    base = 8;
                }
            } else {
                base = 10;
            }
        } else if (base == 16 && c == '0' && consumed < max_w) {
            int c2 = scan_getc(src);
            if ((c2 == 'x' || c2 == 'X') && consumed + 1 < max_w) {
                consumed += 2;
                c = scan_getc(src);
            } else {
                scan_ungetc(src, c2);
            }
        }

        uint64_t val = 0;
        int digits = 0;
        while (c != EOF && consumed < max_w) {
            int d = scan_digit_val(c, base);
            if (d < 0) break;
            val = val * (uint64_t)base + (uint64_t)d;
            digits++;
            consumed++;
            c = (consumed < max_w) ? scan_getc(src) : EOF;
        }
        if (c != EOF && consumed < max_w) scan_ungetc(src, c);
        if (digits == 0 && base == 8) {
            /* "0" alone with base 0 or 8 is a valid 0 only if zero char was seen cleanly; handled by digits check if '0' remained unconsumed above */
        }
        if (digits == 0) return assigned;

        if (!suppress) {
            if (is_signed) {
                int64_t sval = neg ? -(int64_t)val : (int64_t)val;
                if (len_mod == 2) *va_arg(ap, long long *) = (long long)sval;
                else if (len_mod == 1) *va_arg(ap, long *) = (long)sval;
                else if (len_mod == -1) *va_arg(ap, short *) = (short)sval;
                else *va_arg(ap, int *) = (int)sval;
            } else {
                uint64_t uval = neg ? (uint64_t)(-(int64_t)val) : val;
                if (len_mod == 2) *va_arg(ap, unsigned long long *) = (unsigned long long)uval;
                else if (len_mod == 1) *va_arg(ap, unsigned long *) = (unsigned long)uval;
                else if (len_mod == -1) *va_arg(ap, unsigned short *) = (unsigned short)uval;
                else *va_arg(ap, unsigned int *) = (unsigned int)uval;
            }
            assigned++;
        }
        fmt++;
    }

    return assigned;
}

int sscanf(const char *str, const char *format, ...) {
    if (!str || !format) return EOF;
    scan_src_t src = { .str = str, .pos = 0, .file = NULL, .has_unget = 0, .unget_char = 0 };
    va_list ap;
    va_start(ap, format);
    int res = vscan_common(&src, format, ap);
    va_end(ap);
    return res;
}

int fscanf(FILE *stream, const char *format, ...) {
    if (!stream || !format) return EOF;
    scan_src_t src = { .str = NULL, .pos = 0, .file = stream, .has_unget = 0, .unget_char = 0 };
    va_list ap;
    va_start(ap, format);
    int res = vscan_common(&src, format, ap);
    va_end(ap);
    scan_restore_file_unget(&src);
    return res;
}
