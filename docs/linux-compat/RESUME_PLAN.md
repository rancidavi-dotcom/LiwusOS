# Linux Compatibility - Resume Plan (Ordered by Dependencies)

This plan is ordered by technical dependencies. Re-evaluate as needed - this is not a mandatory sequence if dependencies change.

## Current baseline (must hold)
- Branch `lean-kernel`; current HEAD at start of this session `8ec9b84`; working tree has unrelated native SDK, filesystem, GUI, networking and USB edits that must be preserved
- Active manifests: 51 top-level cases and 23 ring-3 markers in `make test`,
  `make ring3-test` and `make smap-test`
- Build clean with `-Werror`

## Pending work (from roadmap/known issues)

### 1) Futex robustness
**Priority:** High (blocks real threading use)
- [ ] Multiple waiters, cancellation, concurrent exit handling, stress tests
- [x] Relative timeout for private `FUTEX_WAIT` at 10 ms tick resolution; 10 ms expiration, invalid `tv_nsec`, and invalid timespec pointer pass in QEMU ring-3
- [x] `set_tid_address(218)` uses the generic per-thread clear-child-TID field; thread exit clears it and wakes one matching private waiter; QEMU clone probe passes
- [x] Private `FUTEX_WAIT_BITSET`/`FUTEX_WAKE_BITSET` with absolute MONOTONIC/REALTIME deadlines and mask filtering; basic expired-deadline and matching-mask handshake tests pass
- [ ] Add incompatible-mask selection tests, clock-step cases and broader timeout/wake race stress
- [x] Basic private `FUTEX_REQUEUE`/`FUTEX_CMP_REQUEUE`; validated empty queue, cmp mismatch, invalid destination, and musl condvar broadcast/join
- [ ] Expand same/different-key, incompatible bitset masks, competing wake/requeue, timeout, and waiter-count race coverage
- [ ] Shared futexes between processes (keying beyond (mm,addr) or different scheme)
- [ ] Robust lists (`set_robust_list`) basics

**Acceptance:** Updated timed wait probe passes; then stress tests with multiple waiters/timeouts/requeue pass without hangs/crashes; semantics match Linux for tested cases. Current requeue coverage is basic and does not meet this stress acceptance yet.

### 2) Signals and safe signal frames
**Priority:** High
- [ ] Pending/blocked signals, `sigaction`, `sigprocmask`, `sigreturn`
- [ ] `sigaltstack`
- [ ] Synchronous delivery (SIGSEGV/SIGFPE)
- [ ] Integration with thread/process model

**Acceptance:** Signal delivery works correctly with proper frame setup/teardown; no corruption.

### 3) Process termination and wait semantics
**Priority:** High
- [x] `fork(57)` routes through the generic LIWUS fork path and preserves the Linux process personality.
- [x] Basic `wait4(61)`: positive PID or `-1`, blocking/WNOHANG, normal exit-status encoding, EFAULT-before-reap, ECHILD after reap; validated in QEMU.
- [ ] Signal termination, WIFSIGNALED/WUNTRACED/WCONTINUED, SIGCHLD and process-group selection.
- [ ] Real `rusage` accounting (currently zero-filled).

**Acceptance:** The supported subset has a passing QEMU probe; full Linux wait semantics remain unvalidated until signals, groups and accounting are implemented.

### 4) Credentials, groups, sessions
**Priority:** Medium-High
- [x] Linux read-only UID/GID getters (`getuid/geteuid/getgid/getegid`) translated to generic LIWUS process credentials and probed in QEMU
- [ ] Validate credentials across fork/clone and VFS permission boundaries
- [ ] Groups, sessions, process groups
- [ ] Credential-changing calls only after privilege and permission model is defined

**Acceptance:** Basic credential semantics correct.

### 5) Real pthreads with libc
**Priority:** High (key validation)
- [x] One static musl pthread create/join using caller-provided stack passes with SMAP and four QEMU CPUs; required musl clone flags and parent/child TID handling are supported
- [x] Diagnose/fix default musl stack TLS fault: missing `mprotect(10)` left musl's PROT_NONE stack inaccessible; default-stack and four-thread mutex/join static musl probes pass in QEMU
- [ ] Test with 8 real pthreads using static musl
- [x] Static glibc `puts` smoke ELF passes `make smap-test LINUX_STATIC_ELF=/tmp/liwus-static-glibc-v3-hello` on `-cpu max`; this is not dynamic-linker or broad glibc coverage
- [ ] Mutex/condvar stress

**Acceptance:** Default musl stack works, then static musl with 8 pthreads + mutex/condvar runs successfully; current four-thread condvar evidence is an intermediate result.

### 6) CPU features and FPU/SIMD
**Priority:** High (blocks some binaries)
- [ ] Complete CPU feature policy, including heterogeneous CPUs and truthful per-process feature exposure
- [x] x87/SSE/AVX/XSAVE context save/restore per thread; x86-64 v3 static glibc smoke passes
- [x] Enable CR4.OSXSAVE and XCR0=7 only when XSAVE+AVX are enumerated; retain FXSAVE fallback
- [ ] AVX-512/AMX state and kernel-mode SIMD bracketing remain unsupported/unvalidated
- [x] Re-test static glibc built from this machine's archive; it now prints the greeting and exits 0 under QEMU `-cpu max`

**Acceptance:** x87/SSE/AVX user state survives blocking calls and task switches; broader state components and dynamic glibc remain pending.

### 7) VM for Linux needs (demand paging, COW)
**Priority:** High
- [ ] Demand paging (page fault consults VMA, alloc zero page, grow stack with guard)
- [ ] COW on fork with refcounting
- [ ] File-backed mmap (private/shared) with page cache
- [x] Eager-backed `mprotect` over fully mapped VMA ranges; Linux probe checks PROT_NONE/RW and invalid-range errors. Concurrent changes and TLB shootdown remain unvalidated.
- [ ] `mremap`, `madvise`, `MAP_FIXED` proper semantics
- [ ] Integrate `brk` with VMAs

**Acceptance:** Linux binaries with complex memory patterns work; fork efficient with COW.

### 8) Loader enhancements and auxv
**Priority:** High
- [ ] PT_INTERP support
- [ ] PT_GNU_STACK, PT_TLS
- [ ] ET_DYN/PIE support completeness
- [x] Basic ELF64 Linux auxv: AT_PHDR/PHENT/PHNUM/ENTRY/BASE/PAGESZ/BRK/RANDOM/UID/GID/PLATFORM/HWCAP/HWCAP2/CLKTCK/SECURE/EXECFN; ring-3 probe validates values and stack alignment
- [x] Basic AT_HWCAP (CPUID.1:EDX) and AT_HWCAP2=0, checked by ring-3 probe
- [ ] vDSO/AT_SYSINFO_EHDR; define behavior when program headers are not mapped
- [x] Static musl ET_EXEC launched through Linux `execve` and validated by captured output/status with `LINUX_STATIC_ELF`
- [x] Basic initial stack setup per Linux ABI (16-byte alignment, AT_RANDOM bytes); fallback entropy is not cryptographic
- [x] Linux-specific exact program break: AT_BRK is page-aligned from the highest PT_LOAD end; brk(0), grow, zero-fill, shrink, and invalid requests have ring-3 coverage
- [x] `pread64(17)`/`pwrite64(18)` preserve shared offsets and cover zero-filled gaps, EOF/invalid buffers and no-side-effect failure; storage remains limited to the current 32-bit VFS
- [x] `getrandom(318)` uses only advertised RDRAND, accepts GRND_NONBLOCK/GRND_RANDOM, caps each call at 256 bytes and reports EIO without a hardware RNG; ring-3 covers transfer, zero count, EFAULT and EINVAL
- [x] `close_range(436)` closes or marks CLOEXEC across the 32-entry LIWUS fd table; tests cover range close, CLOEXEC, invalid flags and first > last
- [x] `prctl(157)` implements per-thread `PR_SET_NAME`/`PR_GET_NAME`, Linux 15-byte truncation, EFAULT and unsupported-option behavior

**Acceptance:** Still pending for dynamic/static libc binaries; only the static freestanding probe validates the supported subset.

### 9) Dynamic linking
**Priority:** High
- [ ] Load PT_INTERP (dynamic linker)
- [ ] ld.so maps libc and dependencies
- [ ] Symbol resolution, relocations as needed

**Acceptance:** glibc dynamic "hello world" runs.

### 10) Syscall expansion driven by real programs
**Priority:** Ongoing
- [x] Implement `getdents64` with Linux record layout, VFS translation, EOF/short-buffer/error probes. `open(2)` and `openat` cover Linux access modes, `O_CREAT|O_EXCL`, mode/umask, `O_CLOEXEC`, `O_DIRECTORY`, x86-64 `O_LARGEFILE`, `O_NONBLOCK` stored/reported for regular files without nonblocking I/O, `O_LARGEFILE` accepted/ignored in musl `F_SETFL`, `O_NOFOLLOW` no-op while the VFS has no symlinks, dirfd-relative paths and explicit rejection of other unsupported flags. `faccessat`/`faccessat2(flags=0)`, `mkdir(83)`/`mkdirat`, `rmdir(84)`, `unlink(87/at)`, `newfstatat(flags=0)`, `statx(332, STATX_BASIC_STATS, AT_SYMLINK_NOFOLLOW)` for the 256-byte x86-64 layout, common path/dirfd queries and explicit unsupported flags, `rename(82)`/`renameat(264)` and `renameat2(flags=0)` use generic LIWUS path/VFS services; probes cover relative dirfd paths, legacy path operations, same-name rename, missing source, existing destination, unsupported flags, and invalid/non-directory descriptors. Static musl and glibc ELFs exercise allocation, clocks, random, task names, stdio file create/write/seek/read/rename/unlink, stat/access, directory iteration, mkdir/rmdir, and `pipe2(O_CLOEXEC|O_NONBLOCK)` with `F_GETFL`, `EAGAIN`, round-trip and close. `pipe2(O_CLOEXEC|O_NONBLOCK)`, `F_SETFL(O_NONBLOCK)` pipe EAGAIN/dup/atomicity probes, `dup3(O_CLOEXEC)`, `F_DUPFD_CLOEXEC`, `F_GETFD`/`F_SETFD`, and CLOEXEC close on successful `execve` have ring-3 probes; failed exec preserves descriptors in the tested case. `chdir`/`fchdir` and `getcpu` also have personality probes. Directory descriptors still resolve through a stored path; directory renames, stable vnode semantics and concurrent directory mutation are unavailable.
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
