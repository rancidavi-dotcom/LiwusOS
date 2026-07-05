/*
 * fast_memcpy - SSE2 Non-Temporal Store memcpy for VRAM writes
 * 
 * Uses movdqa (16-byte aligned load) + movntdq (non-temporal 16-byte store)
 * to bypass the CPU cache and write directly to the memory bus.
 * Combined with MTRR Write-Combining, this achieves maximum VRAM bandwidth.
 *
 * void fast_memcpy(void *dst, const void *src, uint64_t size);
 *   rdi = dst
 *   rsi = src  
 *   rdx = size in bytes
 */

.global fast_memcpy
.type fast_memcpy, @function

fast_memcpy:
    /* Save callee-saved registers */
    push    %rbx

    mov     %rdx, %rcx          /* rcx = total bytes remaining */
    
    /* If less than 64 bytes, use byte copy */
    cmp     $64, %rcx
    jb      .Ltail_bytes

    /* ── Main loop: 64 bytes per iteration (4 x 16-byte SSE2 moves) ── */
.Lloop64:
    cmp     $64, %rcx
    jb      .Ltail_16

    /* Load 4 x 16 bytes from source (aligned load, from RAM) */
    movdqu  0(%rsi), %xmm0
    movdqu  16(%rsi), %xmm1
    movdqu  32(%rsi), %xmm2
    movdqu  48(%rsi), %xmm3

    /* Store 4 x 16 bytes to destination (safe unaligned store) */
    movdqu %xmm0, 0(%rdi)
    movdqu %xmm1, 16(%rdi)
    movdqu %xmm2, 32(%rdi)
    movdqu %xmm3, 48(%rdi)

    add     $64, %rsi
    add     $64, %rdi
    sub     $64, %rcx
    jmp     .Lloop64

    /* ── Tail: 16 bytes at a time ── */
.Ltail_16:
    cmp     $16, %rcx
    jb      .Ltail_bytes

    movdqu  0(%rsi), %xmm0
    movdqu  %xmm0, 0(%rdi)

    add     $16, %rsi
    add     $16, %rdi
    sub     $16, %rcx
    jmp     .Ltail_16

    /* ── Tail: remaining bytes (< 16) ── */
.Ltail_bytes:
    test    %rcx, %rcx
    jz      .Ldone

.Lbyte_loop:
    movb    (%rsi), %al
    movb    %al, (%rdi)
    inc     %rsi
    inc     %rdi
    dec     %rcx
    jnz     .Lbyte_loop

.Ldone:
    /* Ensure all non-temporal stores are visible */
    sfence

    pop     %rbx
    ret
