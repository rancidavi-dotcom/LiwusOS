/* boot.s */

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO_MODE, 1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO_MODE
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0, 0, 0, 0, 0
.long 0          /* mode_type */
.long 1024       /* width */
.long 768        /* height */
.long 32         /* depth */

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
_start:
	mov $stack_top, %esp
    push %ebx
    push %eax
	call kernel_main

    cli
1:	hlt
	jmp 1b

.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $.flush
.flush:
    ret

.global load_page_directory
load_page_directory:
    mov 4(%esp), %eax
    mov %eax, %cr3
    ret

.global enable_paging
enable_paging:
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    ret