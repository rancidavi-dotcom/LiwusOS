# Native Liwus ABI - Architecture

## Overview
LiwusOS has its own native ABI based on `int 0x80` (documented in `docs/ABI.md`). The native ABI is the primary interface; Linux compatibility is an optional translation layer.

## Relationship to kernel
- Entry: `int $0x80` from ring 3 → IDT vector 128, handled by native syscall dispatcher in `src/kernel/core/syscall.c`
- ABI selection per task via `task_t->process->abi` (`TASK_ABI_LIWUS` by default)
- Native syscalls use register convention: number in RAX, args in RDI,RSI,RDX,RCX,R8,R9 (C calling convention order)
- Returns via RAX; current behavior mixes `-1` and `-errno` for errors (see ABI.md) - must be treated as `rax < 0` or MAP_FAILED for mmap
- Reuses same kernel core: scheduler, VM/VMA, VFS, process/thread model, usercopy with SMAP

## Principles
1. Simplicity and predictable contracts
2. Compatibility between versions (preserve binary compatibility when possible)
3. Memory safety via usercopy validation
4. Documented interfaces
5. Low coupling, independence from Linux
6. Easy to use from C
7. Testable
8. Incremental evolution

## Evolution rules
- Do not change syscall numbers/regs/layouts without strong justification and compatibility plan
- New generic kernel capabilities may be exposed when there is real need
- Don't expose APIs just because internal mechanism exists
- Prefer good existing conventions over artificial differences
- Document contracts in UAPI headers and docs
