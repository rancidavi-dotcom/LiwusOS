/*
 * libtcc API test program (built with ONE_SOURCE like main tcc)
 * This file includes tcc.c directly (like apps/tcc/tcc.c) to get
 * all TCC internals including libtcc API functions.
 */
#ifndef ONE_SOURCE
#define ONE_SOURCE 1
#endif
#ifndef TCC_TARGET_X86_64
#define TCC_TARGET_X86_64
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal syscall for debug (needed by libtcc.c debug prints) */
static long raw_syscall(long nr, long a, long b, long c) {
    long ret;
    /* int $0x80 on x86_64 uses: rax=nr, rbx=arg1, rcx=arg2, rdx=arg3, rsi=arg4, rdi=arg5, rbp=arg6 */
    asm volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a), "c"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}
static void kprintf(const char *s) {
    long n = 0; while (s[n]) n++;
    raw_syscall(4, (long)s, n, 0);
}
static void kprintf_ptr(const char *p, void *v) {
    long n = 0; while (p[n]) n++;
    raw_syscall(4, (long)p, n, 0);
    raw_syscall(4, (long)"0x", 2, 0);
    char buf[32]; char *e = buf+31; *e='\0';
    unsigned long val=(unsigned long)v;
    do { *--e="0123456789abcdef"[val&0xF]; val>>=4; } while(val&&e>buf);
    raw_syscall(4, (long)e, buf+31-e, 0);
    raw_syscall(4, (long)"\n", 1, 0);
}

/* Include TCC headers to get TCCState and function declarations */
#define TCC_API
#include "../../third_party/tcc/tcc.h"

/* Test function that uses libtcc API */
int test_libtcc_main() {
    TCCState *s = tcc_new();
    if (!s) {
        printf("FAIL: tcc_new failed\n");
        return 1;
    }
    
    tcc_set_output_type(s, TCC_OUTPUT_OBJ);
    tcc_add_include_path(s, "/tccsdk/include");
    tcc_add_sysinclude_path(s, "/tccsdk/include");
    
    const char *code = 
        "int hello(void) {\n"
        "    return 42;\n"
        "}\n";
    
    if (tcc_compile_string(s, code) < 0) {
        printf("FAIL: tcc_compile_string failed\n");
        tcc_delete(s);
        return 1;
    }
    
    if (tcc_output_file(s, "/hello_tcc.o") < 0) {
        printf("FAIL: tcc_output_file failed\n");
        tcc_delete(s);
        return 1;
    }
    
    tcc_delete(s);
    printf("OK: libtcc compiled successfully\n");
    return 0;
}

/* Rename tcc's main to avoid conflict */
#define main tcc_original_main

/* Include the entire TCC implementation (ONE_SOURCE style) */
#include "../../third_party/tcc/tcc.c"

#undef main

/* Our own main that calls the test */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    /* Initialize malloc arena AFTER kernel zeroed .bss */
    __libc_init_malloc_arena();
    
    /* Override TCC's reallocator with our simple bump allocator */
    extern void *(*reallocator)(void*, unsigned long);
    reallocator = tcc_simple_realloc;
    
    return test_libtcc_main();
}