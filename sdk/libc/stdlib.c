#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct heap_block {
  size_t size;
  int free;
  struct heap_block *next;
} heap_block_t;

static heap_block_t *heap_head = NULL;

static size_t align_up(size_t value) { return (value + 7U) & ~7U; }

static heap_block_t *find_free_block(size_t size) {
  heap_block_t *block = heap_head;
  while (block) {
    if (block->free && block->size >= size) {
      return block;
    }
    block = block->next;
  }
  return NULL;
}

static heap_block_t *request_block(size_t size) {
  heap_block_t *block = (heap_block_t *)sbrk((ptrdiff_t)(sizeof(heap_block_t) + size));
  if (block == (void *)-1) {
    return NULL;
  }

  block->size = size;
  block->free = 0;
  block->next = NULL;
  return block;
}

char *itoa(int value, char *str, int base) {
  char *ptr = str;
  char *ptr1 = str;
  char tmp_char;
  int tmp_value;

  if (base < 2 || base > 36) {
    *str = '\0';
    return str;
  }

  do {
    tmp_value = value;
    value /= base;
    *ptr++ =
        "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"
            [35 + (tmp_value - value * base)];
  } while (value);

  if (tmp_value < 0) {
    *ptr++ = '-';
  }
  *ptr-- = '\0';
  while (ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr-- = *ptr1;
    *ptr1++ = tmp_char;
  }
  return str;
}

void exit(int status) { _exit(status); }

void abort(void) { _exit(1); }

int atoi(const char *s) {
  int sign = 1;
  int value = 0;

  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' ||
         *s == '\v') {
    ++s;
  }

  if (*s == '-') {
    sign = -1;
    ++s;
  } else if (*s == '+') {
    ++s;
  }

  while (*s >= '0' && *s <= '9') {
    value = (value * 10) + (*s - '0');
    ++s;
  }

  return value * sign;
}

double atof(const char *s) {
  double sign = 1.0;
  double value = 0.0;
  double frac = 0.1;
  int seen_dot = 0;

  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' ||
         *s == '\v') {
    ++s;
  }

  if (*s == '-') {
    sign = -1.0;
    ++s;
  } else if (*s == '+') {
    ++s;
  }

  while (*s) {
    if (*s == '.' && !seen_dot) {
      seen_dot = 1;
    } else if (*s >= '0' && *s <= '9') {
      if (!seen_dot) {
        value = value * 10.0 + (double)(*s - '0');
      } else {
        value += (double)(*s - '0') * frac;
        frac *= 0.1;
      }
    } else {
      break;
    }
    ++s;
  }

  return value * sign;
}

double strtod(const char *nptr, char **endptr) {
  const char *s = nptr;
  double value = atof(nptr);

  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' ||
         *s == '\v') {
    ++s;
  }
  if (*s == '-' || *s == '+') {
    ++s;
  }
  while ((*s >= '0' && *s <= '9') || *s == '.') {
    ++s;
  }

  if (endptr) {
    *endptr = (char *)s;
  }
  return value;
}

int abs(int value) { return value < 0 ? -value : value; }

void *malloc(size_t size) {
  heap_block_t *block;
  heap_block_t *tail;

  if (size == 0) {
    return NULL;
  }

  size = align_up(size);
  block = find_free_block(size);
  if (block) {
    block->free = 0;
    return (void *)(block + 1);
  }

  block = request_block(size);
  if (!block) {
    errno = ENOMEM;
    return NULL;
  }

  if (!heap_head) {
    heap_head = block;
  } else {
    tail = heap_head;
    while (tail->next) {
      tail = tail->next;
    }
    tail->next = block;
  }

  return (void *)(block + 1);
}

void free(void *ptr) {
  heap_block_t *block;

  if (!ptr) {
    return;
  }

  block = (heap_block_t *)ptr - 1;
  block->free = 1;
}

void *calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  unsigned char *ptr = (unsigned char *)malloc(total);
  size_t i;

  if (!ptr) {
    return NULL;
  }

  for (i = 0; i < total; ++i) {
    ptr[i] = 0;
  }

  return ptr;
}

void *realloc(void *ptr, size_t size) {
  heap_block_t *block;
  unsigned char *dst;
  unsigned char *src;
  size_t i;

  if (!ptr) {
    return malloc(size);
  }

  if (size == 0) {
    free(ptr);
    return NULL;
  }

  block = (heap_block_t *)ptr - 1;
  if (block->size >= size) {
    return ptr;
  }

  dst = (unsigned char *)malloc(size);
  if (!dst) {
    return NULL;
  }

  src = (unsigned char *)ptr;
  for (i = 0; i < block->size; ++i) {
    dst[i] = src[i];
  }

  free(ptr);
  return dst;
}

char *getenv(const char *name) {
  (void)name;
  return NULL;
}

char *strdup(const char *s) {
  size_t len = strlen(s) + 1;
  char *copy = malloc(len);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len);
  return copy;
}

int system(const char *command) {
  (void)command;
  return -1;
}
