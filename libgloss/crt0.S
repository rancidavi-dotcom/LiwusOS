.section .text
.global _start
_start:
    xor %rbp, %rbp
    mov (%rsp), %rdi        /* argc */
    lea 8(%rsp), %rsi       /* argv */
    xor %rdx, %rdx          /* envp = NULL */
    andq $-16, %rsp         /* align stack to 16 bytes */

    /* Save argc/argv to callee-saved registers before clobbering */
    mov %rdi, %rbx          /* rbx = argc */
    mov %rsi, %r12          /* r12 = argv */

    /* Initialize _impure_ptr to point to _impure_data (static reent struct) */
    lea _impure_data@GOTPCREL(%rip), %rax
    mov %rax, _impure_ptr@GOTPCREL(%rip)

    /* Zero the _reent structure (_impure_data) */
    mov %rax, %rdi
    mov $256, %rcx
    xor %rax, %rax
    rep stosb

/* NOTE: malloc arena initialization moved to __libc_init_malloc_arena() 
   called from main() AFTER kernel zeroed .bss section */

    /* Initialize _impure_ptr to point to _impure_data (static reent struct) */
    lea _impure_data@GOTPCREL(%rip), %rax
    mov %rax, _impure_ptr@GOTPCREL(%rip)

    /* Zero the _reent structure (_impure_data) */
    mov %rax, %rdi
    mov $256, %rcx
    xor %rax, %rax
    rep stosb

    /* Now call __sinit with the reent pointer */
    mov _impure_ptr@GOTPCREL(%rip), %rdi
    /* NOTE: with -fPIC, the GOT entry already holds the struct address, no deref needed */
    call __sinit

    /* Initialize stdin/stdout/stderr FILE structures explicitly */
    /* __sinit should have set them up, but ensure they're valid */
    mov _impure_ptr@GOTPCREL(%rip), %rdi
    mov (%rdi), %rdi         /* dereference to get _reent* */
    
    /* _stdin at offset 0x0, _stdout at 0x8, _stderr at 0x10 in _reent */
    /* These should be set by __sinit, but verify */

    /* Restore argc/argv and call main */
    mov %rbx, %rdi          /* argc */
    mov %r12, %rsi          /* argv */
    xor %rdx, %rdx          /* envp = NULL */
    call main

    mov %rax, %rdi
    call _exit
    hlt
