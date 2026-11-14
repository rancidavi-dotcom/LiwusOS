#include "liwus/stdlib.h"
#include "liwus/syscall.h"
#include "liwus/mman.h"
#include "liwus/string.h"

#define PAGE_SIZE 4096
#define CHUNK_ALIGN 16
#define MMAP_THRESHOLD (128 * 1024)

typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
    struct block_header *prev;
} block_header_t;

typedef struct __attribute__((aligned(CHUNK_ALIGN))) mmap_header {
    size_t size;
    size_t total_alloc;
    uint32_t magic;
} mmap_header_t;

typedef char mmap_header_alignment_check[
    (sizeof(mmap_header_t) % CHUNK_ALIGN) == 0 ? 1 : -1];

#define MMAP_MAGIC 0x4C49574D /* "LIWM" */

static block_header_t *heap_start = NULL;

static size_t align_up(size_t val, size_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

static block_header_t *request_heap_space(size_t needed_payload) {
    size_t total_needed = sizeof(block_header_t) + needed_payload;
    size_t mmap_size = align_up(total_needed, PAGE_SIZE);
    if (mmap_size < 64 * 1024) {
        mmap_size = 64 * 1024;
    }

    void *addr = liw_mmap(NULL, mmap_size, LIW_PROT_READ | LIW_PROT_WRITE,
                          LIW_MAP_PRIVATE | LIW_MAP_ANONYMOUS, -1, 0);
    if (addr == LIW_MAP_FAILED || !addr) {
        return NULL;
    }

    block_header_t *new_block = (block_header_t *)addr;
    new_block->size = mmap_size - sizeof(block_header_t);
    new_block->is_free = 1;
    new_block->next = NULL;
    new_block->prev = NULL;

    if (!heap_start) {
        heap_start = new_block;
    } else {
        block_header_t *curr = heap_start;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = new_block;
        new_block->prev = curr;
    }
    return new_block;
}

static void split_block(block_header_t *block, size_t needed) {
    if (block->size >= needed + sizeof(block_header_t) + CHUNK_ALIGN) {
        block_header_t *remainder = (block_header_t *)((char *)block + sizeof(block_header_t) + needed);
        remainder->size = block->size - needed - sizeof(block_header_t);
        remainder->is_free = 1;
        remainder->next = block->next;
        remainder->prev = block;

        if (block->next) {
            block->next->prev = remainder;
        }
        block->next = remainder;
        block->size = needed;
    }
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size = align_up(size, CHUNK_ALIGN);

    if (size >= MMAP_THRESHOLD) {
        size_t total = align_up(sizeof(mmap_header_t) + size, PAGE_SIZE);
        void *p = liw_mmap(NULL, total, LIW_PROT_READ | LIW_PROT_WRITE,
                           LIW_MAP_PRIVATE | LIW_MAP_ANONYMOUS, -1, 0);
        if (p == LIW_MAP_FAILED || !p) return NULL;
        mmap_header_t *h = (mmap_header_t *)p;
        h->size = size;
        h->total_alloc = total;
        h->magic = MMAP_MAGIC;
        return (void *)(h + 1);
    }

    block_header_t *curr = heap_start;
    block_header_t *best = NULL;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            best = curr;
            break;
        }
        curr = curr->next;
    }

    if (!best) {
        best = request_heap_space(size);
        if (!best) return NULL;
    }

    split_block(best, size);
    best->is_free = 0;
    return (void *)(best + 1);
}

void free(void *ptr) {
    if (!ptr) return;

    mmap_header_t *mh = ((mmap_header_t *)ptr) - 1;
    if (mh->magic == MMAP_MAGIC) {
        size_t total = mh->total_alloc;
        mh->magic = 0;
        liw_munmap(mh, total);
        return;
    }

    block_header_t *header = ((block_header_t *)ptr) - 1;
    header->is_free = 1;

    if (header->next && header->next->is_free &&
        (char *)header + sizeof(block_header_t) + header->size == (char *)header->next) {
        header->size += sizeof(block_header_t) + header->next->size;
        header->next = header->next->next;
        if (header->next) {
            header->next->prev = header;
        }
    }

    if (header->prev && header->prev->is_free &&
        (char *)header->prev + sizeof(block_header_t) + header->prev->size == (char *)header) {
        header->prev->size += sizeof(block_header_t) + header->size;
        header->prev->next = header->next;
        if (header->next) {
            header->next->prev = header->prev;
        }
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (nmemb && total / nmemb != size) return NULL; // Overflow
    void *ptr = malloc(total);
    if (ptr) {
        liw_memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    mmap_header_t *mh = ((mmap_header_t *)ptr) - 1;
    if (mh->magic == MMAP_MAGIC) {
        void *new_ptr = malloc(size);
        if (!new_ptr) return NULL;
        size_t copy_len = mh->size < size ? mh->size : size;
        liw_memcpy(new_ptr, ptr, copy_len);
        free(ptr);
        return new_ptr;
    }

    block_header_t *header = ((block_header_t *)ptr) - 1;
    size = align_up(size, CHUNK_ALIGN);

    if (header->size >= size) {
        split_block(header, size);
        return ptr;
    }

    if (header->next && header->next->is_free &&
        (char *)header + sizeof(block_header_t) + header->size == (char *)header->next &&
        header->size + sizeof(block_header_t) + header->next->size >= size) {
        header->size += sizeof(block_header_t) + header->next->size;
        header->next = header->next->next;
        if (header->next) {
            header->next->prev = header;
        }
        split_block(header, size);
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    liw_memcpy(new_ptr, ptr, header->size);
    free(ptr);
    return new_ptr;
}

void exit(int status) {
    liw_exit(status);
}

void abort(void) {
    liw_exit(134);
}

int abs(int j) {
    return j < 0 ? -j : j;
}
