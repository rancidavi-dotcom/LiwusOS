OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

/* Higher-half kernel (step 9a).
 *
 * The boot stub is linked and loaded at 16 MiB so GRUB can enter it with
 * paging off. The rest of the kernel has a virtual address at
 * 0xffffffff80000000 and a physical load address of 32 MiB. The boot stub
 * installs both the identity map and the higher-half window before jumping
 * into kernel_main.
 */
KERNEL_VIRT_BASE = 0xffffffff80000000;
KERNEL_PHYS_BASE = 0x02000000;
KERNEL_VIRT_OFFSET = KERNEL_VIRT_BASE - KERNEL_PHYS_BASE;

SECTIONS
{
  . = 16M;

  /* The boot stack and page tables live in the same PT_LOAD as the boot
   * code: this keeps the multiboot header at the start of the file and the
   * stub reachable before paging. The cost is ~40 KiB of zero-filled file
   * content; moving these tables to the direct map is part of step 9b. */
  .boot ALIGN(4K) : {
    *(.multiboot)
    *(.boot.text)
    *(.boot.data)
    *(.boot.bss)
  }

  . = KERNEL_VIRT_BASE;

  .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRT_OFFSET) {
    __kernel_text_start = .;
    *(.text)
    *(.text.*)
    __kernel_text_end = .;
  }

  .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRT_OFFSET) {
    __kernel_rodata_start = .;
    *(.rodata)
    *(.rodata.*)
    *(.eh_frame)
    *(.eh_frame_hdr)
    __kernel_rodata_end = .;
  }

  .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRT_OFFSET) {
    __kernel_data_start = .;
    *(.data)
    *(.data.*)
    *(.ldata)
    *(.ldata.*)
  }

  .bss ALIGN(4K) (NOLOAD) : AT(ADDR(.bss) - KERNEL_VIRT_OFFSET) {
    *(COMMON)
    *(.bss)
    *(.bss.*)
    *(.lbss)
    *(.lbss.*)
  }

  . = ALIGN(4K);
  end = .;
  kernel_end = .;

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.interp)
  }
}
