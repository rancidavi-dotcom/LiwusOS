/* crti.S — C runtime init prologue (crti.o)
 * Fornece os símbolos _init/_fini e os seções .init/.fini para o TCC
 * linkar corretamente um executável (esperado em CONFIG_TCC_CRTPREFIX). */
.text
.section .init
.global _init
.type _init, @function
_init:
    push %rbp
    mov %rsp, %rbp

.section .fini
.global _fini
.type _fini, @function
_fini:
    push %rbp
    mov %rsp, %rbp

.section .note.GNU-stack,"",@progbits
