#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int __liw_sys_save_file(const char *name, void *buffer, uint32_t size);

#define LIW_FILE_MODE_READ 1
#define LIW_FILE_MODE_WRITE 2
#define LIW_FILE_MODE_STD 3

static FILE std_in = {LIW_FILE_MODE_STD, 0, 0, 0, NULL, NULL, 0, 0, 0, 0};
static FILE std_out = {LIW_FILE_MODE_STD, 0, 0, 1, NULL, NULL, 0, 0, 0, 0};
static FILE std_err = {LIW_FILE_MODE_STD, 0, 0, 2, NULL, NULL, 0, 0, 0, 0};

FILE *stdin = &std_in;
FILE *stdout = &std_out;
FILE *stderr = &std_err;

static int ensure_capacity(FILE *stream, size_t needed) {
  unsigned char *new_buffer;
  size_t new_capacity;

  if (needed <= stream->capacity) {
    return 0;
  }

  new_capacity = stream->capacity ? stream->capacity : 256;
  while (new_capacity < needed) {
    new_capacity *= 2;
  }

  new_buffer = realloc(stream->buffer, new_capacity);
  if (!new_buffer) {
    stream->error = 1;
    errno = ENOMEM;
    return -1;
  }

  stream->buffer = new_buffer;
  stream->capacity = new_capacity;
  return 0;
}

static char *dup_string(const char *s) {
  size_t len = strlen(s) + 1;
  char *copy = malloc(len);
  if (!copy) {
    errno = ENOMEM;
    return NULL;
  }
  memcpy(copy, s, len);
  return copy;
}

static int write_char_to_buffer(char **out, size_t *remaining, char c) {
  if (*remaining > 1) {
    **out = c;
    (*out)++;
    (*remaining)--;
  }
  return 1;
}

static int write_string_to_buffer(char **out, size_t *remaining, const char *s) {
  int count = 0;
  while (*s) {
    count += write_char_to_buffer(out, remaining, *s++);
  }
  return count;
}

static int parse_decimal(const char **fmt) {
  int value = 0;

  while (**fmt >= '0' && **fmt <= '9') {
    value = value * 10 + (**fmt - '0');
    ++(*fmt);
  }

  return value;
}

static int format_unsigned(char **out, size_t *remaining, unsigned long value,
                           unsigned int base, int uppercase, int min_digits) {
  char buffer[32];
  const char *digits =
      uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
  int count = 0;
  int index = 0;

  if (value == 0) {
    buffer[index++] = '0';
  } else {
    while (value > 0 && index < (int)sizeof(buffer)) {
      buffer[index++] = digits[value % base];
      value /= base;
    }
  }

  while (index < min_digits && index < (int)sizeof(buffer)) {
    buffer[index++] = '0';
  }

  while (index-- > 0) {
    count += write_char_to_buffer(out, remaining, buffer[index]);
  }

  return count;
}

static int format_signed(char **out, size_t *remaining, long value,
                         int min_digits) {
  unsigned long magnitude;
  int count = 0;

  if (value < 0) {
    count += write_char_to_buffer(out, remaining, '-');
    magnitude = (unsigned long)(-(value + 1)) + 1UL;
  } else {
    magnitude = (unsigned long)value;
  }

  count += format_unsigned(out, remaining, magnitude, 10, 0, min_digits);
  return count;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  char *out = str;
  size_t remaining = size;
  int written = 0;

  if (size > 0) {
    *out = '\0';
  }

  while (*format) {
    if (*format != '%') {
      written += write_char_to_buffer(&out, &remaining, *format++);
      continue;
    }

    ++format;
    int zero_pad = 0;
    int width = 0;
    int precision = -1;
    int long_modifier = 0;
    int min_digits;

    if (*format == '0') {
      zero_pad = 1;
      ++format;
    }

    if (*format >= '0' && *format <= '9') {
      width = parse_decimal(&format);
    }

    if (*format == '.') {
      ++format;
      precision = parse_decimal(&format);
    }

    while (*format == 'l') {
      long_modifier = 1;
      ++format;
    }

    min_digits = precision >= 0 ? precision : (zero_pad ? width : 0);

    switch (*format) {
    case '%':
      written += write_char_to_buffer(&out, &remaining, '%');
      break;
    case 'c':
      written += write_char_to_buffer(&out, &remaining, (char)va_arg(ap, int));
      break;
    case 's': {
      const char *s = va_arg(ap, const char *);
      written += write_string_to_buffer(&out, &remaining, s ? s : "(null)");
      break;
    }
    case 'd':
    case 'i':
      if (long_modifier) {
        written += format_signed(&out, &remaining, va_arg(ap, long), min_digits);
      } else {
        written +=
            format_signed(&out, &remaining, (long)va_arg(ap, int), min_digits);
      }
      break;
    case 'u':
      if (long_modifier) {
        written += format_unsigned(&out, &remaining, va_arg(ap, unsigned long),
                                   10, 0, min_digits);
      } else {
        written += format_unsigned(
            &out, &remaining, (unsigned long)va_arg(ap, unsigned int), 10, 0,
            min_digits);
      }
      break;
    case 'x':
    case 'X':
      if (long_modifier) {
        written += format_unsigned(&out, &remaining, va_arg(ap, unsigned long),
                                   16, *format == 'X', min_digits);
      } else {
        written += format_unsigned(
            &out, &remaining, (unsigned long)va_arg(ap, unsigned int), 16,
            *format == 'X', min_digits);
      }
      break;
    default:
      written += write_char_to_buffer(&out, &remaining, '%');
      if (zero_pad) {
        written += write_char_to_buffer(&out, &remaining, '0');
      }
      if (width > 0) {
        char widthbuf[12];
        int widthlen = 0;
        int tmp = width;

        if (tmp == 0) {
          widthbuf[widthlen++] = '0';
        } else {
          char rev[12];
          int revlen = 0;
          while (tmp > 0) {
            rev[revlen++] = (char)('0' + (tmp % 10));
            tmp /= 10;
          }
          while (revlen-- > 0) {
            widthbuf[widthlen++] = rev[revlen];
          }
        }

        for (int i = 0; i < widthlen; ++i) {
          written += write_char_to_buffer(&out, &remaining, widthbuf[i]);
        }
      }
      if (precision >= 0) {
        written += write_char_to_buffer(&out, &remaining, '.');
        written += format_unsigned(&out, &remaining, (unsigned int)precision, 10, 0, 0);
      }
      written += write_char_to_buffer(&out, &remaining, *format);
      break;
    }
    ++format;
  }

  if (size > 0) {
    *out = '\0';
  }

  return written;
}

int snprintf(char *str, size_t size, const char *format, ...) {
  va_list ap;
  int written;

  va_start(ap, format);
  written = vsnprintf(str, size, format, ap);
  va_end(ap);
  return written;
}

static int stream_write(FILE *stream, const char *buffer, size_t len) {
  if (stream == stdout || stream == stderr) {
    return write(stream->fd, buffer, len);
  }

  if (ensure_capacity(stream, stream->pos + len + 1) != 0) {
    return -1;
  }

  memcpy(stream->buffer + stream->pos, buffer, len);
  stream->pos += len;
  if (stream->pos > stream->size) {
    stream->size = stream->pos;
  }
  stream->buffer[stream->size] = '\0';
  return (int)len;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
  char buffer[1024];
  int len = vsnprintf(buffer, sizeof(buffer), format, ap);
  if (len < 0) {
    stream->error = 1;
    return -1;
  }
  if (stream_write(stream, buffer, (size_t)len) < 0) {
    stream->error = 1;
    return -1;
  }
  return len;
}

int fprintf(FILE *stream, const char *format, ...) {
  va_list ap;
  int written;

  va_start(ap, format);
  written = vfprintf(stream, format, ap);
  va_end(ap);
  return written;
}

int vprintf(const char *format, va_list ap) { return vfprintf(stdout, format, ap); }

int printf(const char *format, ...) {
  va_list ap;
  int written;

  va_start(ap, format);
  written = vfprintf(stdout, format, ap);
  va_end(ap);
  return written;
}

int sprintf(char *str, const char *format, ...) {
  va_list ap;
  int written;

  va_start(ap, format);
  written = vsnprintf(str, (size_t)-1, format, ap);
  va_end(ap);
  return written;
}

int putchar(int c) {
  char ch = (char)c;
  return write(1, &ch, 1) == 1 ? (unsigned char)ch : -1;
}

int puts(const char *s) {
  int count = fprintf(stdout, "%s\n", s);
  return count;
}

FILE *fopen(const char *path, const char *mode) {
  FILE *stream = calloc(1, sizeof(FILE));

  if (!stream) {
    return NULL;
  }

  stream->fd = -1;
  stream->path = dup_string(path);
  if (!stream->path) {
    free(stream);
    return NULL;
  }

  if (mode[0] == 'r') {
    long end_pos;

    stream->mode = LIW_FILE_MODE_READ;
    stream->fd = open(path, 0);
    if (stream->fd < 0) {
      free(stream->path);
      free(stream);
      return NULL;
    }

    end_pos = lseek(stream->fd, 0, SEEK_END);
    if (end_pos < 0 || lseek(stream->fd, 0, SEEK_SET) < 0) {
      close(stream->fd);
      free(stream->path);
      free(stream);
      return NULL;
    }

    stream->size = (size_t)end_pos;
    stream->pos = 0;
  } else if (mode[0] == 'w') {
    stream->mode = LIW_FILE_MODE_WRITE;
  } else {
    free(stream->path);
    free(stream);
    errno = EINVAL;
    return NULL;
  }

  return stream;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
  FILE *replacement;

  if (!stream) {
    errno = EINVAL;
    return NULL;
  }

  fclose(stream);
  replacement = fopen(path, mode);
  return replacement;
}

int fclose(FILE *stream) {
  int rc = 0;

  if (!stream || stream == stdin || stream == stdout || stream == stderr) {
    return 0;
  }

  if (stream->mode == LIW_FILE_MODE_READ && stream->fd >= 0) {
    close(stream->fd);
  }

  if (stream->mode == LIW_FILE_MODE_WRITE) {
    if (__liw_sys_save_file(stream->path, stream->buffer, (uint32_t)stream->size) <
        0) {
      rc = EOF;
    }
  }

  free(stream->path);
  free(stream->buffer);
  free(stream);
  return rc;
}

FILE *tmpfile(void) {
  errno = ENOSYS;
  return NULL;
}

char *tmpnam(char *s) {
  static char name[L_tmpnam];
  static int count = 0;
  sprintf(name, "/tmp/lua_%d", count++);
  if (s) {
    strcpy(s, name);
    return s;
  }
  return name;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t total = size * nmemb;
  ssize_t got;

  if (!stream || size == 0 || nmemb == 0) {
    return 0;
  }

  if (stream->mode == LIW_FILE_MODE_READ && stream->fd >= 0) {
    got = read(stream->fd, ptr, total);
    if (got <= 0) {
      if (got == 0) {
        stream->eof = 1;
      } else {
        stream->error = 1;
      }
      return 0;
    }

    stream->pos += (size_t)got;
    if ((size_t)got < total || stream->pos >= stream->size) {
      stream->eof = 1;
    }
    return (size_t)got / size;
  }

  if (stream->pos >= stream->size) {
    stream->eof = 1;
    return 0;
  }

  size_t available = stream->size - stream->pos;
  if (total > available) {
    total = available;
    stream->eof = 1;
  }

  memcpy(ptr, stream->buffer + stream->pos, total);
  stream->pos += total;
  return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t total = size * nmemb;

  if (!stream || size == 0 || nmemb == 0) {
    return 0;
  }

  if (stream_write(stream, (const char *)ptr, total) < 0) {
    return 0;
  }

  return nmemb;
}

int fseek(FILE *stream, long offset, int whence) {
  size_t new_pos;

  if (!stream) {
    return -1;
  }

  if (stream->mode == LIW_FILE_MODE_READ && stream->fd >= 0) {
    long pos = lseek(stream->fd, offset, whence);
    if (pos < 0) {
      stream->error = 1;
      return -1;
    }
    stream->pos = (size_t)pos;
    stream->eof = 0;
    return 0;
  }

  switch (whence) {
  case SEEK_SET:
    new_pos = (size_t)offset;
    break;
  case SEEK_CUR:
    new_pos = stream->pos + offset;
    break;
  case SEEK_END:
    new_pos = stream->size + offset;
    break;
  default:
    return -1;
  }

  if (new_pos > stream->size && stream->mode == LIW_FILE_MODE_READ) {
    stream->eof = 1;
    return -1;
  }

  if (stream->mode == LIW_FILE_MODE_WRITE && ensure_capacity(stream, new_pos + 1) != 0) {
    return -1;
  }

  stream->pos = new_pos;
  if (stream->pos > stream->size) {
    stream->size = stream->pos;
  }
  stream->eof = 0;
  return 0;
}

long ftell(FILE *stream) { return stream ? (long)stream->pos : -1; }

int feof(FILE *stream) { return stream ? stream->eof : 0; }

int ferror(FILE *stream) { return stream ? stream->error : 1; }

int fflush(FILE *stream) { (void)stream; return 0; }

void clearerr(FILE *stream) {
  if (stream) {
    stream->error = 0;
    stream->eof = 0;
  }
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
  (void)stream;
  (void)buf;
  (void)mode;
  (void)size;
  return 0;
}

void rewind(FILE *stream) {
  if (stream) {
    stream->pos = 0;
    stream->eof = 0;
  }
}

int fgetc(FILE *stream) {
  unsigned char c;
  if (fread(&c, 1, 1, stream) != 1) {
    return EOF;
  }
  return c;
}

int fputc(int c, FILE *stream) {
  unsigned char ch = (unsigned char)c;
  return fwrite(&ch, 1, 1, stream) == 1 ? ch : EOF;
}

int ungetc(int c, FILE *stream) {
  if (!stream || stream->pos == 0) {
    return EOF;
  }
  if (stream->mode == LIW_FILE_MODE_READ && stream->fd >= 0) {
    if (lseek(stream->fd, -1, SEEK_CUR) < 0) {
      return EOF;
    }
  }
  stream->pos--;
  stream->eof = 0;
  return c;
}

char *fgets(char *s, int size, FILE *stream) {
  int i = 0;
  int c;

  if (!s || size <= 0 || !stream) {
    return NULL;
  }

  while (i < size - 1) {
    c = fgetc(stream);
    if (c == EOF) {
      break;
    }
    s[i++] = (char)c;
    if (c == '\n') {
      break;
    }
  }

  if (i == 0) {
    return NULL;
  }

  s[i] = '\0';
  return s;
}

int fputs(const char *s, FILE *stream) { return (int)fwrite(s, 1, strlen(s), stream); }

int fileno(FILE *stream) { return stream ? stream->fd : -1; }

static int parse_int(const char **p, int base, int *out) {
  int value = 0;
  int digits = 0;

  while (isspace((unsigned char)**p)) {
    ++(*p);
  }

  if (base == 16 && (*p)[0] == '0' && (((*p)[1] == 'x') || ((*p)[1] == 'X'))) {
    *p += 2;
  } else if (base == 8 && (*p)[0] == '0') {
    *p += 1;
  }

  while (**p) {
    int c = **p;
    int digit;
    if (isdigit(c)) {
      digit = c - '0';
    } else if (base == 16 && c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (base == 16 && c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      break;
    }

    if (digit >= base) {
      break;
    }

    value = value * base + digit;
    ++(*p);
    ++digits;
  }

  if (!digits) {
    return 0;
  }

  *out = value;
  return 1;
}

int vsprintf(char *str, const char *format, va_list ap) {
  return vsnprintf(str, (size_t)-1, format, ap);
}

int vsscanf(const char *str, const char *format, va_list ap) {
  int matched = 0;

  while (*format) {
    if (isspace((unsigned char)*format)) {
      while (isspace((unsigned char)*str)) {
        ++str;
      }
      ++format;
      continue;
    }

    if (*format != '%') {
      if (*format != *str) {
        break;
      }
      ++format;
      ++str;
      continue;
    }

    ++format;
    if (*format == 'd') {
      int *out = va_arg(ap, int *);
      int sign = 1;
      int value;
      while (isspace((unsigned char)*str)) {
        ++str;
      }
      if (*str == '-') {
        sign = -1;
        ++str;
      }
      if (!parse_int(&str, 10, &value)) {
        break;
      }
      *out = value * sign;
      ++matched;
    } else if (*format == 'x' || *format == 'X') {
      int *out = va_arg(ap, int *);
      if (!parse_int(&str, 16, out)) {
        break;
      }
      ++matched;
    } else if (*format == 'o') {
      int *out = va_arg(ap, int *);
      if (!parse_int(&str, 8, out)) {
        break;
      }
      ++matched;
    } else {
      break;
    }
    ++format;
  }

  return matched;
}

int sscanf(const char *str, const char *format, ...) {
  va_list ap;
  int matched;

  va_start(ap, format);
  matched = vsscanf(str, format, ap);
  va_end(ap);
  return matched;
}

int remove(const char *path) { (void)path; return 0; }

int rename(const char *oldpath, const char *newpath) {
  FILE *src;
  FILE *dst;
  unsigned char buffer[512];
  size_t got;

  src = fopen(oldpath, "rb");
  if (!src) {
    return -1;
  }

  dst = fopen(newpath, "wb");
  if (!dst) {
    fclose(src);
    return -1;
  }

  while ((got = fread(buffer, 1, sizeof(buffer), src)) > 0) {
    fwrite(buffer, 1, got, dst);
  }

  fclose(src);
  fclose(dst);
  return 0;
}
