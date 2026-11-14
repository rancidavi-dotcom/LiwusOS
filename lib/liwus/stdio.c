#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/syscall.h"
#include "liwus/fcntl.h"

static FILE _stdin_file = { .fd = 0, .buf_mode = 1 };
static FILE _stdout_file = { .fd = 1, .buf_mode = 1 };
static FILE _stderr_file = { .fd = 2, .buf_mode = 0 };

FILE *stdin = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;

    int flags = 0;
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        flags = LIW_O_RDONLY;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        flags = LIW_O_WRONLY | LIW_O_CREAT | LIW_O_TRUNC;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        flags = LIW_O_WRONLY | LIW_O_CREAT | LIW_O_APPEND;
    } else if (strcmp(mode, "r+") == 0 || strcmp(mode, "r+b") == 0) {
        flags = LIW_O_RDWR;
    } else if (strcmp(mode, "w+") == 0 || strcmp(mode, "w+b") == 0) {
        flags = LIW_O_RDWR | LIW_O_CREAT | LIW_O_TRUNC;
    } else {
        return NULL;
    }

    int fd = liw_open(path, flags);
    if (fd < 0) return NULL;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        liw_close(fd);
        return NULL;
    }
    liw_memset(f, 0, sizeof(FILE));
    f->fd = fd;
    f->buf_mode = 2; // fully buffered
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    if (fd < 0 || !mode) return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) return NULL;
    liw_memset(f, 0, sizeof(FILE));
    f->fd = fd;
    f->buf_mode = (fd == 1 || fd == 2) ? 1 : 2;
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    if (!stream) return NULL;
    fflush(stream);
    if (stream->fd >= 0) {
        liw_close(stream->fd);
        stream->fd = -1;
    }
    if (!path || !mode) return NULL;

    int flags = 0;
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        flags = LIW_O_RDONLY;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        flags = LIW_O_WRONLY | LIW_O_CREAT | LIW_O_TRUNC;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        flags = LIW_O_WRONLY | LIW_O_CREAT | LIW_O_APPEND;
    } else if (strcmp(mode, "r+") == 0 || strcmp(mode, "r+b") == 0) {
        flags = LIW_O_RDWR;
    } else if (strcmp(mode, "w+") == 0 || strcmp(mode, "w+b") == 0) {
        flags = LIW_O_RDWR | LIW_O_CREAT | LIW_O_TRUNC;
    } else {
        return NULL;
    }

    int fd = liw_open(path, flags);
    if (fd < 0) return NULL;

    stream->fd = fd;
    stream->buf_pos = 0;
    stream->buf_len = 0;
    stream->eof = 0;
    stream->error = 0;
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    fflush(stream);
    int res = liw_close(stream->fd);
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    return res < 0 ? EOF : 0;
}

int fflush(FILE *stream) {
    if (!stream) {
        fflush(stdout);
        fflush(stderr);
        return 0;
    }
    if (stream->buf_pos > 0) {
        long written = liw_write_all(stream->fd, stream->buf, stream->buf_pos);
        stream->buf_pos = 0;
        if (written < 0) {
            stream->error = 1;
            return EOF;
        }
    }
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    const unsigned char *p = (const unsigned char *)ptr;

    if (stream->buf_mode == 0) {
        long w = liw_write_all(stream->fd, p, total_bytes);
        if (w < 0) {
            stream->error = 1;
            return 0;
        }
        return nmemb;
    }

    for (size_t i = 0; i < total_bytes; i++) {
        stream->buf[stream->buf_pos++] = p[i];
        if (stream->buf_pos >= BUFSIZ || (stream->buf_mode == 1 && p[i] == '\n')) {
            if (fflush(stream) == EOF) {
                return i / size;
            }
        }
    }
    return nmemb;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    unsigned char *p = (unsigned char *)ptr;
    size_t read_bytes = 0;

    if (stream->buf_pos < stream->buf_len) {
        size_t avail = (size_t)(stream->buf_len - stream->buf_pos);
        size_t take = total_bytes < avail ? total_bytes : avail;
        memcpy(p, stream->buf + stream->buf_pos, take);
        stream->buf_pos += (int)take;
        read_bytes += take;
    }

    while (read_bytes < total_bytes) {
        size_t rem = total_bytes - read_bytes;
        if (rem >= BUFSIZ) {
            long r = liw_read(stream->fd, p + read_bytes, rem);
            if (r <= 0) {
                if (r == 0) stream->eof = 1;
                else stream->error = 1;
                break;
            }
            read_bytes += (size_t)r;
        } else {
            long r = liw_read(stream->fd, stream->buf, BUFSIZ);
            if (r <= 0) {
                if (r == 0) stream->eof = 1;
                else stream->error = 1;
                break;
            }
            stream->buf_pos = 0;
            stream->buf_len = (int)r;
            size_t take = rem < (size_t)r ? rem : (size_t)r;
            memcpy(p + read_bytes, stream->buf, take);
            stream->buf_pos = (int)take;
            read_bytes += take;
        }
    }
    return read_bytes / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) return -1;
    fflush(stream);
    stream->buf_pos = 0;
    stream->buf_len = 0;
    long res = liw_lseek(stream->fd, (int32_t)offset, whence);
    if (res < 0) {
        stream->error = 1;
        return -1;
    }
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    long curr = liw_lseek(stream->fd, 0, SEEK_CUR);
    if (curr < 0) return -1;
    return curr - (stream->buf_len - stream->buf_pos);
}

void rewind(FILE *stream) {
    fseek(stream, 0, SEEK_SET);
}

int fgetc(FILE *stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1) return (int)c;
    return EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 1 || !stream) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) == 1) return c;
    return EOF;
}

int fputs(const char *s, FILE *stream) {
    if (!s || !stream) return EOF;
    size_t len = liw_strlen(s);
    return fwrite(s, 1, len, stream) == len ? 0 : EOF;
}

int putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout);
}

int getchar(void) {
    return fgetc(stdin);
}

int feof(FILE *stream) {
    return stream ? stream->eof : 1;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 1;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}
