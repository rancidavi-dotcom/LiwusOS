#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

void *liw_malloc(size_t size);
void *liw_realloc(void *ptr, size_t size);
void liw_free(void *ptr);

void *liw_mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)prot;
    (void)flags;
    (void)fd;
    (void)off;
    void *p = mmap(addr, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? (void *)(intptr_t)-1 : p;
}

int liw_munmap(void *addr, size_t len) {
    return munmap(addr, len);
}

void *liw_memset(void *dst, int c, size_t n) {
    return memset(dst, c, n);
}

void *liw_memcpy(void *dst, const void *src, size_t n) {
    return memcpy(dst, src, n);
}

int main(void) {
    void *small = liw_malloc(64);
    void *large = liw_malloc(128U * 1024U);
    if (!small || !large || ((uintptr_t)small & 15U) ||
        ((uintptr_t)large & 15U)) {
        fprintf(stderr, "LIWUS malloc alignment failed (small=%p large=%p)\n",
                small, large);
        return 1;
    }

    memset(large, 0x5a, 128U * 1024U);
    void *grown = liw_realloc(large, 256U * 1024U);
    if (!grown || ((uintptr_t)grown & 15U)) {
        liw_free(small);
        liw_free(large);
        fprintf(stderr, "LIWUS large realloc alignment failed\n");
        return 2;
    }
    const uint8_t *bytes = (const uint8_t *)grown;
    for (size_t i = 0; i < 128U * 1024U; ++i) {
        if (bytes[i] != 0x5a) {
            liw_free(small);
            liw_free(grown);
            fprintf(stderr, "LIWUS large realloc content mismatch at %zu\n", i);
            return 3;
        }
    }

    liw_free(grown);
    liw_free(small);
    puts("LIWUS malloc PASS: 16-byte alignment and large realloc contents");
    return 0;
}
