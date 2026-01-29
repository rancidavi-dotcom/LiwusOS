.global trampoline_start
.global trampoline_end

.extern ap_pml4_val
.extern ap_stack_val
.extern ap_entry_val
.extern ap_status
.extern gdt_ptr

.text

.code16
trampoline_start:
    cli
    cld
    
    # Reset segment registers to 0
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    # Load temporary 32-bit GDT descriptor
    # The physical address is 0x8000 + (temp_gdt_ptr - trampoline_start)
    lgdt (temp_gdt_ptr - trampoline_start + 0x8000)

    # Enable Protected Mode (CR0.PE = 1)
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0

    # Far jump to Protected Mode (32-bit) using code segment selector 0x08
    ljmpl $0x08, $(entry32 - trampoline_start + 0x8000)

.align 16
temp_gdt:
    # 0x00: Null descriptor
    .quad 0x0000000000000000
    # 0x08: 32-bit Code Descriptor (kernel space)
    .quad 0x00cf9a000000ffff
    # 0x10: 32-bit Data Descriptor (kernel space)
    .quad 0x00cf92000000ffff
temp_gdt_end:

temp_gdt_ptr:
    .word (temp_gdt_end - temp_gdt - 1)
    .long (temp_gdt - trampoline_start + 0x8000)

.code32
entry32:
    # Load 32-bit data segment selector (0x10)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    # Load PML4 address into CR3
    mov $ap_pml4_val, %eax
    mov (%eax), %eax
    mov %eax, %cr3

    # Enable Physical Address Extension (PAE) in CR4
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    # Enable Long Mode in EFER MSR (0xC0000080)
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    # Enable Paging (CR0.PG = 1)
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    # Load the main 64-bit kernel GDT descriptor
    lgdt (gdt_ptr)

    # Far jump to 64-bit Long Mode code segment selector 0x08
    ljmp $0x08, $entry64

.code64
entry64:
    # Set up 64-bit data segment registers (0x10 selector)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    # Load the custom per-CPU kernel stack allocated by the BSP
    mov $ap_stack_val, %rax
    mov (%rax), %rsp

    # Signal the BSP that this core has initialized successfully
    mov $ap_status, %rax
    movq $1, (%rax)

    # Jump to the C entrypoint function: `ap_kernel_main()`
    mov $ap_entry_val, %rax
    mov (%rax), %rax
    jmp *%rax

trampoline_end:
