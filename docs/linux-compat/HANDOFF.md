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

## Current work state
The maintainer resumed Linux-personality work for an overnight engineering session on 2026-11-11. LIWUS remains an independent OS; changes belong in the compatibility personality or reusable generic kernel interfaces. Do not replace native ABI code or describe the system as Linux.

## Last known functional checkpoint
- Branch: `lean-kernel`
- Commit at session start: `8ec9b84` on `lean-kernel`
- Working tree contains extensive unrelated native changes; preserve them and do not commit them as part of Linux ABI work
- Build with `-Werror`
- Tests at older checkpoint: `make test` 49/49 PASS, `make ring3-test` 49/49 PASS, `make smap-test` 49/49 PASS. Current tree has 51 top-level cases and 23 ring-3 markers; the latest `make ring3-test` passed 51/51 + 23/23.
- QEMU with 4 CPUs initialized, SMAP active
- Linux syscall probe (ET_EXEC freestanding) passes in QEMU: UID/GID getters, getpid/getppid/gettid, uname/sysinfo, gettimeofday/clock_gettime/clock_getres, nanosleep/sched_yield, fork/wait4, fstat/openat/getdents64/mmap/mprotect/munmap/FS-TLS/write
- Linux clone probe passes in QEMU: CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD with optional CLONE_SETTLS, shared memory, independent FS TLS, thread exit; private futex WAIT/WAKE/WAIT_BITSET, relative/absolute timeout subsets, and matching-mask wake validated

The active tree now contains 51 top-level cases and 23 ring-3 markers. The most
recent `make ring3-test` passed all 51/51 kernel cases and 23 ring-3 markers in
QEMU after adding Linux `pipe`, `readv`/`writev`, `getdents64`, directory-cookie
`lseek`, `fork`, the initial `wait4` subset, and `set_tid_address` clear-and-wake
handling. The Linux probes check directory records,
rewind/EOF/errors, fork child return, WNOHANG, wait status encoding, EFAULT
without reaping, zero-filled rusage, ECHILD after reap, and child-tid zero/wake
on thread exit, plus vectored pipe I/O and error/limit paths. `make ring3-test`
passed after these changes. Static musl pthread create/join now passes both
with an application-provided stack and with musl's default guarded stack. The
four-thread mutex/counter/join probe also passes under `make smap-test` with
SMAP enabled and a four-vCPU QEMU configuration. The default-stack failure was
caused by missing Linux `mprotect(10)`: musl reserves PROT_NONE, then enables
its usable stack before TLS initialization. The new VMA protection path is
covered by host fault-injection/split tests and the ring-3 syscall probe.
Cross-CPU migration, concurrent mapping/TLB shootdown, broad requeue races, larger thread
counts and full Linux compatibility remain unvalidated. Current `make test-unit`,
`make test-build`, and `make smap-test LINUX_STATIC_ELF=/tmp/liwus-musl-pthread-smp`
pass. The source tree remains uncommitted because existing unrelated edits
overlap several build files.

## Implemented subsystems (verified)
- **Entry/dispatch**: Linux x86-64 `SYSCALL/SYSRET` entry configured (STAR/LSTAR/SFMASK). Stub swaps GS, builds registers frame, routes by ABI. Dispatcher in `src/compat/linux/syscall.c`. Linux returns `-errno` in RAX.
- **ABI selection**: Per process/thread via `task_t->process->abi` (`TASK_ABI_LINUX_X86_64`). Selection checked in Linux dispatcher.
- **ELF loading**: Existing ELF loader supports ET_EXEC/ET_DYN for Linux binaries at boundary (translation done). No PT_INTERP support yet.
- **TLS/FS/GS**: Per-thread FS/GS bases stored; `ARCH_SET_FS/GET_FS`, `ARCH_SET_GS/GET_GS`, `CLONE_SETTLS` supported. Restored on context switch.
- **Threads (shared-process)**: `clone` with required flags `CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD` and tested `CLONE_SETTLS`, `CLONE_SYSVSEM`, `CLONE_DETACHED`, `CLONE_PARENT_SETTID`, `CLONE_CHILD_SETTID`, `CLONE_CHILD_CLEARTID`; stack/TLS/TID pointers validated, child returns 0. `gettid`, `exit` (thread), `exit_group` (process). Process/thread separation in place (`process_t` resources, `thread_t` execution). Static musl create/join passes with caller and default stacks; a four-thread mutex/join and condition-variable broadcast/join tests pass, while broader pthread/requeue race coverage remains incomplete.
- **Fork/wait**: Linux `fork(57)` reuses generic `fork_process`; child preserves its Linux personality. `wait4(61)` supports positive PID or -1, WNOHANG, normal exit status encoding and zero-filled 144-byte rusage; process groups, signal states, SIGCHLD and resource accounting are not implemented.
- **Thread termination**: `set_tid_address(218)` registers `thread_t.clear_child_tid`. When a thread exits in its current address space, the generic scheduler writes zero and wakes one waiter keyed to the same private futex address. `CLONE_CHILD_SETTID`, `CLONE_CHILD_CLEARTID` and `CLONE_PARENT_SETTID` are implemented for the tested thread path; the child-TID store happens before the child is made runnable. The updated freestanding clone probe, including invalid TID pointer checks, passes in QEMU.
- **Vectored I/O**: `pipe(22)` translates to generic LIWUS pipes; `readv/writev` copy Linux x86-64 `iovec[]` and transfer via the native read/write paths. The probe validates 2-vector data flow, invalid arrays, IOV_MAX rejection and pipe cleanup.
- **Wait queues**: Generic wait queues (`wait_queue_t`) in scheduler; used by native waitpid/pipe blocking and Linux private futex.
- **Futex (private)**: `FUTEX_WAIT_PRIVATE`, `FUTEX_WAKE_PRIVATE`, `FUTEX_WAIT_BITSET_PRIVATE`, and `FUTEX_WAKE_BITSET_PRIVATE` with key `(process->mm, aligned virtual address)`, value comparison before blocking, waiter counting, and relative `FUTEX_WAIT` timeout at 10 ms tick resolution; exercised by QEMU ring-3 probes. Private `FUTEX_REQUEUE`/`FUTEX_CMP_REQUEUE` pass basic ring-3 cases and a static musl condvar broadcast/join test. Shared futex, robust lists, incompatible-mask race behavior, and broad requeue race coverage remain unsupported/unvalidated.
- **Syscalls translated (current)**: read(0), write(1), pipe(22), pipe2(293) with O_CLOEXEC, dup(32), dup2(33), dup3(292) with flags=0/O_CLOEXEC, fcntl(72) subset including F_DUPFD_CLOEXEC/F_GETFD/F_SETFD, readv(19), writev(20), pread64(17), pwrite64(18), close_range(436) for close/CLOEXEC, prctl(157) for PR_SET_NAME/PR_GET_NAME, sched_yield(24), nanosleep(35), clock_nanosleep(230) (partial), access(21), faccessat(269), faccessat2(439, flags=0 subset), open(2)/openat(257) including x86-64 O_LARGEFILE, mkdirat(258), unlink(87)/unlinkat(263), newfstatat(262, flags=0) with AT_FDCWD or relative directory-fd paths, close(3), stat(4)/lstat(6), fstat(5), lseek(8), getcwd(79), chdir(80), fchdir(81) while stored path remains resolvable in VFS, mmap(9), mprotect(10), munmap(11), brk(12), getrandom(318) via advertised RDRAND only (partial), getpid(39), gettimeofday(96), time(201), sysinfo(99), getuid(102), getgid(104), geteuid(107), getegid(108), getresuid(118), getresgid(120), getgroups(115, empty set), umask(95), getrlimit(97), prlimit64(302) query subset, getppid(110), fork(57), execve(59) for static ELF64 through the generic loader (successful exec closes CLOEXEC descriptors), clone(56), wait4(61), getdents64(217), gettid(186), set_tid_address(218), futex(202), clock_gettime(228), getcpu(309) (single NUMA node), arch_prctl(158), exit(60), exit_group(231), uname(63). Unknown -> `-ENOSYS`.
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
- `make ring3-test` (current 51-case manifest) - includes Linux syscall, getdents64, and clone+futex probes
- `make smap-test` (current 51-case manifest) - with SMP/SMAP
- Kernel build with `-Werror`

## Known open problems (see KNOWN_ISSUES.md)
- No PT_INTERP/dynamic linker (glibc dynamic won't load)
- Pthread evidence remains a small static musl matrix; it is not general libc support
- Futex: private WAIT/WAKE/WAIT_BITSET/REQUEUE and relative/absolute timeout subsets validated at basic coverage; no shared futex or robust lists; basic WAIT_BITSET is implemented, while broader REQUEUE race coverage remains pending
- Threads: no full clone generality, no validated cross-CPU migration/stress, no signals
- wait4 supports normal exit only with PID positive/-1 and WNOHANG; signal status, process groups, SIGCHLD and real rusage are missing
- Demand paging/COW/file-backed mmap not implemented for Linux VM needs
- AVX-512/AMX xstate and full CPU feature-policy validation remain unsupported; x87/SSE/AVX with XSAVE now works, including the tested static glibc smoke
- TLB shootdown remote not validated under concurrent AP execution
- Auxv has a tested basic set including `AT_HWCAP`/`AT_HWCAP2`, but no vDSO/`AT_SYSINFO_EHDR` or complete libc loader contract; no broad syscall coverage

## How to resume development safely
1. **Read first**: `docs/linux-compat/HANDOFF.md`, `docs/linux-compat/IMPLEMENTATION_STATUS.md`, `docs/linux-compat/RESUME_PLAN.md`, `docs/linux-compat/KNOWN_ISSUES.md`, `docs/linux-compat/REPRODUCIBILITY.md`, `docs/ABI_LINUX.md`, `docs/ROADMAP.md`
2. **Verify baseline**: `git log -1`, `make -B kernel.bin`, `make test`, `make ring3-test`, `make smap-test` all pass
3. **Continue incrementally**: Follow `RESUME_PLAN`; do not claim broad Linux compatibility from freestanding probes
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
