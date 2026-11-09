# Native Liwus ABI - Getting Started

## Status
This documents what is currently functional. Some pieces may need to be created.

## Prerequisites
Same as building kernel: gcc, binutils, make, qemu.

## Current way to build userspace programs
See `programs/` directory for examples. Programs are built as part of the build system and included in initrd/image. Look at `Makefile` for how ring3 programs are compiled.

## Syscall calling
Native ABI uses `int $0x80`. Register convention: RAX = number, RDI,RSI,RDX,RCX,R8,R9 = args. See `include/uapi/syscall_nums.h`.

## Goal (first milestone)
Make it easy to call native syscalls from C without writing assembly manually, with wrappers, public headers, and a working example that can print, get pid, exit properly.

## Next
Follow DEVELOPMENT_PLAN.md. Create minimal infrastructure if missing.
