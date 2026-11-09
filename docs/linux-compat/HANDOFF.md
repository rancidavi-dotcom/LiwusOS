# Linux Compatibility Handoff

## Purpose
This document allows a future agent with no context from prior conversations to resume Linux x86-64 binary compatibility work for LiwusOS safely and accurately.

## Why Linux compatibility was started
LiwusOS started with a native ABI and userspace. The project added Linux x86-64 binary compatibility as a strategic long-term feature to allow running unmodified Linux ELF binaries (initially freestanding/static) via an isolated compatibility layer. The design preserves LiwusOS's independence while providing a translation layer at the syscall boundary.

## Final objective (long-term)
- Run Linux x86-64 ELF binaries (starting with static, progressing to dynamic with PT_INTERP/glibc) on LiwusOS
- Translate Linux syscall numbers/arguments/structures/errno at the boundary
- Reuse LiwusOS core (scheduler, VM/VMA, VFS, process/thread model) without duplicating them
- Maintain strict separation between native Liwus ABI and Linux personality

## Why work was temporarily paused (current state)
As recorded in the task directive (and reflected by project direction), development of Linux compatibility was paused to prioritize evolution of the native Liwus ABI and its application ecosystem. This is a deliberate planning pause - **not** abandonment, failure, or rewrite. The implementation remains fully in place and must stay functional. Linux compatibility remains a strategic long-term goal.

## Last known functional checkpoint
- Branch: `lean-kernel`
- Commit: `2d8280c` - "feat: add wait queues and private futex support"
- Working tree clean at checkpoint (verified)
- Build with `-Werror`
- Tests: `make test` 49/49 PASS, `make ring3-test` 49/49 PASS, `make smap-test` 49/49 PASS
- QEMU with 4 CPUs initialized, SMAP active
- Linux syscall probe (ET_EXEC freestanding) passes: getpid/fstat/openat/mmap/munmap/FS-TLS/write
- Linux clone probe passes: CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD with optional CLONE_SETTLS, shared memory, independent FS TLS, thread exit; private futex WAIT/WAKE validated

This remains the frozen Linux-personality checkpoint. The repository has since
advanced to `2b96f7d` for native userspace, filesystem and USB work; the active
regression manifests now contain 51 top-level cases and 23 ring-3 markers.

## Implemented subsystems (verified)
- **Entry/dispatch**: Linux x86-64 `SYSCALL/SYSRET` entry configured (STAR/LSTAR/SFMASK). Stub swaps GS, builds registers frame, routes by ABI. Dispatcher in `src/compat/linux/syscall.c`. Linux returns `-errno` in RAX.
- **ABI selection**: Per process/thread via `task_t->process->abi` (`TASK_ABI_LINUX_X86_64`). Selection checked in Linux dispatcher.
- **ELF loading**: Existing ELF loader supports ET_EXEC/ET_DYN for Linux binaries at boundary (translation done). No PT_INTERP support yet.
- **TLS/FS/GS**: Per-thread FS/GS bases stored; `ARCH_SET_FS/GET_FS`, `ARCH_SET_GS/GET_GS`, `CLONE_SETTLS` supported. Restored on context switch.
- **Threads (shared-process)**: `clone` with required flags `CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD` (+optional `CLONE_SETTLS`), stack/TLS pointers validated, child returns 0. `gettid`, `exit` (thread), `exit_group` (process). Process/thread separation in place (`process_t` resources, `thread_t` execution).
- **Wait queues**: Generic wait queues (`wait_queue_t`) in scheduler; used by native waitpid/pipe blocking and Linux private futex.
- **Futex (private)**: `FUTEX_WAIT_PRIVATE`, `FUTEX_WAKE_PRIVATE` with key `(process->mm, aligned virtual address)`, value comparison before blocking, waiter counting. Shared futex, REQUEUE, timeouts return `-ENOSYS`. Alignment/pointer validation as implemented.
- **Syscalls translated (current)**: read(0), write(1), open(2)/openat(257), close(3), stat(4)/lstat(6), fstat(5), newfstatat(262), lseek(8), mmap(9), munmap(11), brk(12), getpid(39), clone(56), gettid(186), futex(202), arch_prctl(158), exit(60), exit_group(231). Unknown -> `-ENOSYS`.
- **Error translation**: Linux uses `-errno` values; native calls translated to Linux errno where needed (e.g. open returns `-ENOENT` on failure in that path).

## Integration with LiwusOS kernel
- **Isolation**: Personality code in `src/compat/linux/` only. Native ABI remains in `src/kernel/core/syscall.c` (`int 0x80`).
- **Shared core**: Uses same scheduler, VM/VMA, page tables, VFS, process/thread objects, usercopy with SMAP, wait queues.
- **Entry**: `SYSCALL` handled by arch stubs; dispatcher checks `process->abi`.
- **No Linux kernel code**: All translation is original.

## Inviolable architectural decisions
1. LiwusOS is independent - not Linux fork/distribution. Linux personality is optional compatibility layer.
2. Do not remove/disable existing Linux personality functionality.
3. Native Liwus ABI is primary; Linux compatibility is additional.
4. Reuse shared kernel mechanisms (scheduler/VM/VFS/process) - do not duplicate them.
5. Translate at boundary only; do not pollute native UAPI with Linux types/macros.
6. Preserve existing tests and behavior; if native changes break Linux tests, fix regression first.
7. `-Werror` must remain. No generated binaries in git.
8. Process/thread separation is architectural (`process_t` = resources, `thread_t` = execution).

## Tests that must keep passing
- `make test` (current 51-case manifest)
- `make ring3-test` (current 51-case manifest) - includes Linux syscall probe and clone+futex probe
- `make smap-test` (current 51-case manifest) - with SMP/SMAP
- Kernel build with `-Werror`

## Known open problems (see KNOWN_ISSUES.md)
- No PT_INTERP/dynamic linker (glibc dynamic won't load)
- No full pthreads/libc support validated beyond freestanding probes
- Futex: only private WAIT/WAKE, no timeouts, REQUEUE, shared futex, robust lists
- Threads: no full clone generality, no validated cross-CPU migration/stress, no signals
- wait4/Linux process termination semantics incomplete; signals/credentials/groups/sessions missing
- Demand paging/COW/file-backed mmap not implemented for Linux VM needs
- AVX/XSAVE not enabled/saved - glibc built for newer ISA fails with #UD
- TLB shootdown remote not validated under concurrent AP execution
- No vDSO/auxv beyond minimal; no broad syscall coverage

## How to resume development safely
1. **Read first**: `docs/linux-compat/HANDOFF.md`, `docs/linux-compat/IMPLEMENTATION_STATUS.md`, `docs/linux-compat/RESUME_PLAN.md`, `docs/linux-compat/KNOWN_ISSUES.md`, `docs/linux-compat/REPRODUCIBILITY.md`, `docs/ABI_LINUX.md`, `docs/ROADMAP.md`
2. **Verify baseline**: `git log -1`, `make -B kernel.bin`, `make test`, `make ring3-test`, `make smap-test` all pass
3. **Stay in frozen scope**: Do not add new syscalls/features to Linux personality unless necessary to fix regressions; follow RESUME_PLAN order
4. **Update docs**: Any Linux personality change must update implementation status matrix and relevant docs
5. **Preserve separation**: Keep Linux code in `src/compat/linux/`, don't leak into native ABI
6. **Test rigorously**: Run all three test targets; ensure no regressions

## Key source files
- `src/compat/linux/syscall.c` - Linux syscall dispatcher/translation
- `src/kernel/core/syscall.c` - Native syscall dispatcher
- `include/uapi/syscall.h`, `include/uapi/syscall_nums.h` - Native ABI
- `src/kernel/arch/x86_64/syscall_stub.s`, related arch code - SYSCALL entry
- `src/kernel/sched/task.c` - Threads, wait queues, futex, clone/exit
- `include/kernel/task.h` - Process/thread/wait queue structures
- `tests/ring3/linux_syscall_probe.S`, `tests/ring3/linux_clone_probe.S` - Linux probes
- `tests/test_runner_ring3.c` - Loads/runs ring3 tests including Linux probes
