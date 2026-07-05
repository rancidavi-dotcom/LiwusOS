/* boot.s — x86_64 higher half kernel entry */
/* Multiboot2 + long mode + 4-level paging */

.set MB2_MAGIC,  0xE85250D6
.set MB2_ARCH,   0                 /* i386 protected mode */

.section .multiboot
.align 8
.long MB2_MAGIC
.long MB2_ARCH
.long mb2_end - mb2_start
.long -(MB2_MAGIC + MB2_ARCH + (mb2_end - mb2_start))

mb2_start:
  /* Framebuffer tag (Type 5) */
  .align 8
  .word 5      /* type */
  .word 0      /* flags: 0 (required) */
  .long 20     /* size */
  .long 1024   /* width */
  .long 768    /* height */
  .long 32     /* depth */

  .align 8
  .word 0, 0          /* end tag type=0, flags=0 */
  .long 8             /* end tag size=8 */
mb2_end:

.section .boot.text
.code32

.global _start
_start:
  /* Save multiboot info pointer from GRUB (32-bit physical address) */
  mov %ebx, (mb2_info_phys_low)
  mov $0, (mb2_info_phys_low + 4)    /* Zero upper 32 bits */

  /* Set up stack */
  lea (boot_stack_top), %esp

  /* ---- Step 1: Zero out page table area ---- */
  lea (pml4_table), %edi
  lea (page_tables_end), %ecx
  sub %edi, %ecx
  shr $2, %ecx
  xor %eax, %eax
  cld
  rep stosl

  /* ---- Step 2: Build PML4 ---- */
  /* PML4[0] -> PDP (identity map) */
  lea (pdp), %eax
  or $0x3, %eax
  mov %eax, (pml4_table)

  /* ---- Step 3: Build PDP ---- */
  /* PDP[0] -> PD */
  lea (pd), %eax
  or $0x3, %eax
  mov %eax, (pdp)

  /* ---- Step 4: Build PD (2MB pages covering first 256MB) ---- */
  mov $0x83, %eax
  lea (pd), %edi
  mov $0, %ecx
1:
  mov %eax, (%edi, %ecx, 8)
  add $0x200000, %eax
  inc %ecx
  cmp $128, %ecx
  jl 1b

  /* ---- Step 5: Enable PAE ---- */
  mov %cr4, %eax
  or $(1 << 5), %eax
  mov %eax, %cr4

  /* ---- Step 6: Set CR3 to PML4 ---- */
  lea (pml4_table), %eax
  mov %eax, %cr3

  /* ---- Step 7: Enable Long Mode in EFER ---- */
  mov $0xC0000080, %ecx
  rdmsr
  or $(1 << 8), %eax          /* LME = 1 */
  wrmsr

  /* ---- Step 8: Enable paging ---- */
  mov %cr0, %eax
  or $0x80000000, %eax        /* PG = 1 */
  mov %eax, %cr0

  /* ---- Step 9: Load GDT with 64-bit code segment ---- */
  lgdt (gdt_ptr_64)

  /* Far jump to 64-bit mode */
  ljmp $0x08, $entry_64

.code64
entry_64:
  /* Now running in 64-bit long mode (identity mapped) */
  mov $boot_stack_top, %rsp

  xor %ax, %ax
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %fs
  mov %ax, %gs
  mov $0x10, %ax
  mov %ax, %ss

  /* Pass multiboot2 info in rsi (saved in ebx before it was clobbered) */
  mov %rbx, %rsi
  xor %edi, %edi       /* magic = 0 (not used anyway) */
  mov $kernel_main, %rax
  call *%rax

  /* Should never return */
  cli
3:
  hlt
  jmp 3b

/* ---- Runtime helper functions (called from 64-bit C code) ---- */

.global load_page_directory
load_page_directory:
  mov %rdi, %rax
  mov %rax, %cr3
  ret

.global enable_paging
enable_paging:
  /* Paging should already be enabled. Just return. */
  ret

.global gdt_flush
gdt_flush:
  /* gdt_flush(gdt_ptr_t *) — loads GDT pointer */
  mov %rdi, %rax
  lgdt (%rax)
  /* No segment reload needed in 64-bit mode (segmentation mostly ignored) */
  ret

.global tss_flush
tss_flush:
  /* tss_flush() — load TSS selector */
  xor %eax, %eax
  mov $0x38, %ax      /* TSS at entry 7 (7*8 = 56 = 0x38) | RPL 0 */
  ltr %ax
  ret

.global idt_flush
idt_flush:
  /* idt_flush(idt_ptr_t *) */
  mov %rdi, %rax
  lidt (%rax)
  ret

/* ---- 64-bit GDT (temporary, replaced by init_gdt later) ---- */
.section .boot.data
.align 16
gdt_64:
  .quad 0x0000000000000000           /* 0x00: Null */
  .quad 0x00AF9A000000FFFF           /* 0x08: Kernel code 64-bit (L=1, DPL=0) */
  .quad 0x00AF92000000FFFF           /* 0x10: Kernel data 64-bit */
  .quad 0x00CFFA000000FFFF           /* 0x18: User code 32-bit compat (D=1) */
  .quad 0x00CFF2000000FFFF           /* 0x20: User data 32-bit */
  .quad 0x00AFFA000000FFFF           /* 0x28: User code 64-bit (L=1, DPL=3) */
  .quad 0x00AFF2000000FFFF           /* 0x30: User data 64-bit */
gdt_ptr_64:
  .word gdt_ptr_64 - gdt_64 - 1     /* Limit */
  .quad gdt_64                       /* Base (64-bit pointer) */

mb2_info_phys_low:
  .quad 0

/* ---- Boot stack ---- */
.section .boot.bss
.align 16
boot_stack_bottom:
  .skip 16384
boot_stack_top:

/* ---- Page tables (must be 4KB aligned) ---- */
.align 4096
pml4_table:
  .skip 4096
pdp:
  .skip 4096
pd:
  .skip 4096
page_tables_end:
