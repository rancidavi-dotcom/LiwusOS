/*
 * sdk/liwus.ld
 *
 * Userspace linker script for LiwusOS Ring 3 executables.
 *
 * Layout (matching the ELF loader in src/kernel/core/elf.c):
 *   - .text is loaded at a low virtual address (4 MB), far from the kernel
 *     at 16 MB, avoiding any overlap with the boot identity mapping.
 *   - .rodata, .data, .bss follow immediately, page-aligned.
 *   - No stack section: the kernel lays out a 64 KB user stack at
 *     0xC0000000 and passes its top in rsp at exec time.
 *
 * Feedback:
 * The kernel maps every PT_LOAD segment at p_vaddr with flags 0x7
 * (R|W|X), zero-fills .bss, and starts execution at e_entry.
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
  text    PT_LOAD FLAGS(5);   /* R X */
  rodata  PT_LOAD FLAGS(4);   /* R   */
  data    PT_LOAD FLAGS(6);   /* R W */
}

SECTIONS
{
  /* Base at 4 MB — below the kernel (16 MB), above the zero page. */
  . = 0x400000;

  .text : {
    *(.text .text.*)
    *(.init .fini)
  } :text

  . = ALIGN(4096);
  .rodata : {
    *(.rodata .rodata.*)
    *(.eh_frame*)
  } :rodata

  . = ALIGN(4096);
  .data : {
    *(.data .data.*)
    *(.sdata .sdata.*)
  } :data

  . = ALIGN(4096);
  .bss : {
    *(.bss .bss.*)
    *(COMMON)
    *(.sbss .sbss.*)
  } :data

  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.debug*)
  }
}
