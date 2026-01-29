/*
 * LiwusOS - Tiny C Compiler (TCC) user-space port.
 *
 * Este arquivo empacota o TCC 0.9.28rc inteiro em UMA unidade de
 * compilacao (ONE_SOURCE). O #include abaixo puxa tcc.c que, por sua
 * vez, inclui libtcc.c (que quando ONE_SOURCE=1 inclui todos os
 * fontes do compilador: tccpp.c, tccgen.c, tccelf.c, x86_64-gen.c, etc).
 *
 * O Makefile compila este arquivo com:
 *   -DONE_SOURCE=1 -DTCC_TARGET_X86_64 -Ithird_party/tcc
 * e linka com crt0/libgloss/libc/libm (newlib) -> ELF estatico usavel
 * dentro do LiwusOS.
 */
#ifndef ONE_SOURCE
#define ONE_SOURCE 1
#endif
#ifndef TCC_TARGET_X86_64
#define TCC_TARGET_X86_64
#endif

#include <stddef.h>
#include <stdint.h>

/* Minimal syscall for debug */
static long raw_syscall(long nr, long a, long b, long c) {
    long ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(nr), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}
static void kprintf(const char *s) {
    long n = 0; while (s[n]) n++;
    raw_syscall(4, 1, (long)s, n);
}
static void kprintf_ptr(const char *p, void *v) {
    long n = 0; while (p[n]) n++;
    raw_syscall(4, 1, (long)p, n);
    raw_syscall(4, 1, (long)"0x", 2);
    char buf[32]; char *e = buf+31; *e='\0';
    unsigned long val=(unsigned long)v;
    do { *--e="0123456789abcdef"[val&0xF]; val>>=4; } while(val&&e>buf);
    raw_syscall(4, 1, (long)e, buf+31-e);
    raw_syscall(4, 1, (long)"\n", 1);
}

/* Simple sys_brk-based allocator with proper realloc using headers */
static unsigned long tcc_heap_end = 0;

void *tcc_simple_realloc(void *ptr, unsigned long size) {
    if (size == 0) return ptr;
    extern long raw_syscall(long nr, long a, long b, long c);
    unsigned long aligned_size = (size + 15) & ~15UL;
    unsigned long header_size = 8;
    unsigned long total_size = aligned_size + header_size;
    unsigned long new_addr;
    
    if (ptr == 0) {
        /* New allocation: place header right before data */
        if (tcc_heap_end == 0) {
            new_addr = raw_syscall(2, 0, 0, 0);
            tcc_heap_end = new_addr;
        }
        unsigned long request_addr = tcc_heap_end + total_size;
        new_addr = raw_syscall(2, request_addr, 0, 0);
        if (new_addr == (unsigned long)-1 || new_addr < tcc_heap_end) return 0;
        unsigned long *header = (unsigned long*)tcc_heap_end;
        *header = aligned_size;
        void *data = (void*)((char*)header + header_size);
        tcc_heap_end = (unsigned long)data + aligned_size;
        return data;
    }
    
    /* Realloc - get old size from header (right before ptr) */
    unsigned long *old_header = (unsigned long*)((char*)ptr - header_size);
    unsigned long old_size = *old_header;
    unsigned long copy_size = (old_size < size) ? old_size : size;
    
    /* Check if we can expand in place (ptr is the last allocation) */
    unsigned long ptr_addr = (unsigned long)ptr;
    unsigned long old_total = old_size + header_size;
    if (ptr_addr + old_total == tcc_heap_end) {
        /* Can expand in place */
        unsigned long request_addr = tcc_heap_end + (total_size - old_total);
        new_addr = raw_syscall(2, request_addr, 0, 0);
        if (new_addr == (unsigned long)-1 || new_addr < tcc_heap_end) return 0;
        *old_header = aligned_size;
        tcc_heap_end = new_addr;
        return ptr;
    }
    
    /* Must allocate new and copy */
    if (tcc_heap_end == 0) {
        new_addr = raw_syscall(2, 0, 0, 0);
        tcc_heap_end = new_addr;
    }
    unsigned long request_addr = tcc_heap_end + total_size;
    new_addr = raw_syscall(2, request_addr, 0, 0);
    if (new_addr == (unsigned long)-1 || new_addr < tcc_heap_end) return 0;
    unsigned long *new_header = (unsigned long*)tcc_heap_end;
    *new_header = aligned_size;
    void *new_data = (void*)((char*)new_header + header_size);
    tcc_heap_end = (unsigned long)new_data + aligned_size;
    
    /* Copy old data */
    char *dst = (char*)new_data;
    char *src = (char*)ptr;
    for (unsigned long i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }
    return new_data;
}

/* Override standard malloc/free/realloc to use our allocator */
void *malloc(size_t size) {
    return tcc_simple_realloc(0, size);
}
void free(void *ptr) {
    (void)ptr; /* leak for simplicity */
}
void *realloc(void *ptr, size_t size) {
    return tcc_simple_realloc(ptr, size);
}
void *calloc(size_t nmemb, size_t size) {
    void *p = tcc_simple_realloc(0, nmemb * size);
    if (p) {
        /* zero the memory */
        char *p8 = (char*)p;
        for (size_t i = 0; i < nmemb * size; i++) p8[i] = 0;
    }
    return p;
}

/* newlib's internal functions (stdio, etc.) call the reentrant "_r"
   allocators directly, bypassing the malloc() override. Redeclare them
   so everything funnels through our sys_brk allocator instead of
   newlib's (uninitialized here) heap arena. */
struct _reent;
void * _malloc_r(struct _reent *r, size_t size);
void   _free_r(struct _reent *r, void *ptr);
void * _realloc_r(struct _reent *r, void *ptr, size_t size);
void * _calloc_r(struct _reent *r, size_t nmemb, size_t size);
void * _memalign_r(struct _reent *r, size_t align, size_t size);
void * _valloc_r(struct _reent *r, size_t size);
void * _pvalloc_r(struct _reent *r, size_t size);

void *_malloc_r(struct _reent *r, size_t size) { (void)r; return tcc_simple_realloc(0, size); }
void  _free_r(struct _reent *r, void *ptr)   { (void)r; (void)ptr; }
void *_realloc_r(struct _reent *r, void *ptr, size_t size) { (void)r; return tcc_simple_realloc(ptr, size); }
void *_calloc_r(struct _reent *r, size_t n, size_t size)   {
    (void)r;
    void *p = tcc_simple_realloc(0, n * size);
    if (p) { char *p8 = (char*)p; for (size_t i = 0; i < n * size; i++) p8[i] = 0; }
    return p;
}
void *_memalign_r(struct _reent *r, size_t a, size_t s) { (void)r; (void)a; return tcc_simple_realloc(0, s); }
void *_valloc_r(struct _reent *r, size_t s)   { (void)r; return tcc_simple_realloc(0, s); }
void *_pvalloc_r(struct _reent *r, size_t s)  { (void)r; return tcc_simple_realloc(0, s); }

/* Initialize malloc arena (__malloc_av_) after kernel zeroed .bss */
static void __libc_init_malloc_arena(void) {
    kprintf("MALLOC_ARENA: init start");
    extern char __malloc_av_[];
    char *av = __malloc_av_;
    
    /* Zero the entire malloc_state (128 bytes) */
    for (int i = 0; i < 128; i++) av[i] = 0;
    
    /* Set up the wilderness chunk (first chunk after malloc_state) */
    /* Chunk starts at __malloc_av_ + 0x30 */
    char *wilderness = av + 0x30;
    /* Set chunk size (size field at offset 0, with PREV_INUSE bit = 1) */
    *(unsigned long*)wilderness = 0x200001;  /* size = ~2MB | PREV_INUSE */
    
    /* fastbins[10] at offset 0 - already zeroed */
    /* top at offset 0x40 (64) */
    *(char**)(av + 0x40) = wilderness;     /* top = first chunk (wilderness) */
    /* last_remainder at offset 0x48 - zero */
    
    /* bins at offset 0x50 - initialize bins[0] to point to itself (empty) */
    char *bins0 = av + 0x50;
    *(char**)bins0 = bins0;         /* fd = bins[0] */
    *(char**)(bins0 + 8) = bins0;   /* bk = bins[0] */
    
    /* binmap at offset 0x90 - zero */
    /* next at offset 0x98 - zero (no next arena) */
    /* next_free at offset 0xa0 - zero */
    /* system_mem at offset 0xa8 - zero */
    /* max_system_mem at offset 0xb0 - zero */
    
    /* Initialize malloc arena pointers */
    char *bins0_ptr = av + 0x50;
    *(char**)(av + 0x8) = bins0_ptr;      /* __malloc_av_+0x8 = av_ = bins[0] */
    *(char**)(av + 0x10) = bins0_ptr;     /* __malloc_av_+0x10 = av_ */
    *(char**)(av + 0x20) = bins0_ptr;     /* __malloc_av_+0x20 = av_ */
    
    kprintf_ptr("MALLOC_ARENA: init done, bins0=", bins0_ptr);
}

#ifndef main
#define main tcc_original_main
#endif

#include "../../third_party/tcc/tcc.c"

#undef main

int main(int argc, char **argv) {
    kprintf("=== TCC_WRAPPER_MAIN_START ===");
    kprintf_ptr("TCC_WRAPPER: argc=", (void*)(long)argc);
    if (argv[0]) kprintf_ptr("TCC_WRAPPER: argv[0]=", argv[0]);
    if (argv[1]) kprintf_ptr("TCC_WRAPPER: argv[1]=", argv[1]);
    if (argv[2]) kprintf_ptr("TCC_WRAPPER: argv[2]=", argv[2]);
    if (argv[3]) kprintf_ptr("TCC_WRAPPER: argv[3]=", argv[3]);
    if (argv[4]) kprintf_ptr("TCC_WRAPPER: argv[4]=", argv[4]);
    
    /* Initialize malloc arena AFTER kernel zeroed .bss */
    __libc_init_malloc_arena();
    
    /* Override TCC's reallocator with our simple sys_brk-based allocator */
    extern void tcc_set_realloc(void *(*realloc_func)(void*, unsigned long));
    tcc_set_realloc(tcc_simple_realloc);

    /* Force static linking so TCC uses the .a archives in tccsdk/lib
       (crt1.o/crti.o/crtn.o/libc.a/libm.a/libgloss.a) instead of looking
       for dynamic libc.so. Only needed when producing an executable, so
       skip it for -c/-S/-E (object/asm/preprocess only). Inject "-static"
       as the first arg. */
    static char static_flag[] = "-static";
    int need_static = 1;
    for (int i = 1; i < argc; i++)
        if (argv[i] && (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-S") == 0
                        || strcmp(argv[i], "-E") == 0))
            { need_static = 0; break; }

    char *newargv[32];
    int cap = argc < 30 ? argc : 30;
    int nargc = argc + (need_static ? 1 : 0);
    if (nargc > 31) nargc = 31;
    newargv[0] = argv[0];
    if (need_static) {
        newargv[1] = static_flag;
        for (int i = 1; i <= cap; i++) newargv[i + 1] = argv[i];
    } else {
        for (int i = 1; i <= cap; i++) newargv[i] = argv[i];
    }
    newargv[nargc] = NULL;

    /* Initialize TCC callbacks to prevent NULL derefs */
    int r = tcc_original_main(nargc, newargv);
    kprintf("TCC: main() exit");
    return r;
}
