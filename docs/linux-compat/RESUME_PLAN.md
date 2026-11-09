# Linux Compatibility - Resume Plan (Ordered by Dependencies)

This plan is ordered by technical dependencies. Re-evaluate as needed - this is not a mandatory sequence if dependencies change.

## Current baseline (must hold)
- Branch `lean-kernel`; current reviewed repository checkpoint `2b96f7d`
- Active manifests: 51 top-level cases and 23 ring-3 markers in `make test`,
  `make ring3-test` and `make smap-test`
- Build clean with `-Werror`

## Pending work (from roadmap/known issues)

### 1) Futex robustness
**Priority:** High (blocks real threading use)
- [ ] Multiple waiters, cancellation, concurrent exit handling, stress tests
- [ ] Timeouts (`FUTEX_WAIT_BITSET` with timeouts) integration with wait queues
- [ ] `FUTEX_REQUEUE`/`FUTEX_CMP_REQUEUE` for same/different keys
- [ ] Shared futexes between processes (keying beyond (mm,addr) or different scheme)
- [ ] Robust lists (`set_robust_list`) basics

**Acceptance:** Stress tests with multiple waiters/timeouts/requeue pass without hangs/crashes; semantics match Linux for tested cases.

### 2) Signals and safe signal frames
**Priority:** High
- [ ] Pending/blocked signals, `sigaction`, `sigprocmask`, `sigreturn`
- [ ] `sigaltstack`
- [ ] Synchronous delivery (SIGSEGV/SIGFPE)
- [ ] Integration with thread/process model

**Acceptance:** Signal delivery works correctly with proper frame setup/teardown; no corruption.

### 3) Process termination and wait semantics
**Priority:** High
- [ ] `wait4` Linux semantics (status encoding WIFEXITED/WIFSIGNALED etc.)
- [ ] `WNOHANG`, SIGCHLD behavior
- [ ] Proper parent/child relationships, zombie reaping

**Acceptance:** Linux programs using wait4 behave correctly.

### 4) Credentials, groups, sessions
**Priority:** Medium-High
- [ ] Consistent uid/euid/gid/egid/umask
- [ ] Groups, sessions, process groups
- [ ] Queries as needed

**Acceptance:** Basic credential semantics correct.

### 5) Real pthreads with libc
**Priority:** High (key validation)
- [ ] Test with 8 real pthreads using static musl
- [ ] Test with static glibc if possible given ISA constraints
- [ ] Mutex/condvar stress

**Acceptance:** musl static with 8 pthreads + mutex/condvar runs successfully.

### 6) CPU features and FPU/SIMD
**Priority:** High (blocks some binaries)
- [ ] CPU capability policy
- [ ] Proper x87/SSE/AVX/XSAVE state save/restore per thread
- [ ] Enable and announce extensions appropriately
- [ ] Re-test glibc static binaries built for modern ISA

**Acceptance:** Binaries requiring AVX/SSE work or are properly rejected with clear behavior; no #UD crashes from unhandled extensions.

### 7) VM for Linux needs (demand paging, COW)
**Priority:** High
- [ ] Demand paging (page fault consults VMA, alloc zero page, grow stack with guard)
- [ ] COW on fork with refcounting
- [ ] File-backed mmap (private/shared) with page cache
- [ ] `mprotect`, `mremap`, `madvise`, `MAP_FIXED` proper semantics
- [ ] Integrate `brk` with VMAs

**Acceptance:** Linux binaries with complex memory patterns work; fork efficient with COW.

### 8) Loader enhancements and auxv
**Priority:** High
- [ ] PT_INTERP support
- [ ] PT_GNU_STACK, PT_TLS
- [ ] ET_DYN/PIE support completeness
- [ ] Auxv: AT_PHDR, AT_PHENT, AT_PHNUM, AT_ENTRY, AT_BASE, AT_PAGESZ, AT_RANDOM, AT_UID/GID, AT_HWCAP, AT_CLKTCK
- [ ] Initial stack setup per Linux ABI (16-byte alignment, random bytes)

**Acceptance:** Loader sets up correct auxv/stack for dynamic/static binaries.

### 9) Dynamic linking
**Priority:** High
- [ ] Load PT_INTERP (dynamic linker)
- [ ] ld.so maps libc and dependencies
- [ ] Symbol resolution, relocations as needed

**Acceptance:** glibc dynamic "hello world" runs.

### 10) Syscall expansion driven by real programs
**Priority:** Ongoing
- [ ] Expand coverage based on actual binaries (openat variants, fcntl, getdents64, readlinkat, etc.)
- [ ] Add only what's needed, translate correctly

**Acceptance:** Real programs don't hit unexpected ENOSYS.

### 11) SMP robustness for threads
**Priority:** Medium-High
- [ ] Validate thread migration between CPUs
- [ ] TLB shootdown for remote mappings
- [ ] Stress tests with SMP

**Acceptance:** Multi-threaded programs run correctly on 4+ CPUs.

### 12) Robustness, security, long-term stress
**Priority:** Ongoing
- [ ] Fuzzing syscall entry/usercopy
- [ ] Stress tests
- [ ] Regression matrix

**Acceptance:** Stable under stress.

## Guidelines
- Don't start next phase until dependencies ready
- Every change must not break native tests or existing Linux probes
- Update IMPLEMENTATION_STATUS.md and docs when adding features
- Document limitations if partial implementation
- Prefer fixing issues that block real binaries
