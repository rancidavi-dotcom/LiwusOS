OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
  . = 16M;

  .text ALIGN(4K) : {
    *(.multiboot)
    *(.boot.text)
    *(.boot.data)
    *(.text)
    *(.text.*)
  }

  .rodata ALIGN(4K) : {
    *(.rodata)
    *(.rodata.*)
    *(.eh_frame)
    *(.eh_frame_hdr)
  }

  .data ALIGN(4K) : {
    *(.data)
    *(.data.*)
  }

  .bss ALIGN(4K) : {
    *(COMMON)
    *(.bss)
    *(.bss.*)
    *(.boot.bss)
  }

  end = .;

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.interp)
  }
}
