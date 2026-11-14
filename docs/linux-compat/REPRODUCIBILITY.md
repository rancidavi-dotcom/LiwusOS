# Linux Compatibility - Reproducibility

## Build dependencies
- gcc (x86_64 cross or native capable of freestanding x86_64)
- binutils
- make
- grub-mkrescue, xorriso, mtools
- qemu-system-x86_64

## Toolchain
Uses default host toolchain as configured in Makefile. Build is freestanding for kernel, ring3 tests compiled separately.

## Build commands
```sh
make            # kernel.bin + liwusos.iso
make -B kernel.bin
```

## Test commands
```sh
make test        # 51 top-level cases + ring3 tests
make ring3-test  # ring3 integration suite including Linux probes
make smap-test   # SMP + SMAP test (4 CPUs, 4GB)
```

All must pass with `-Werror`.

## QEMU parameters (from Makefile)
- `make run`: standard run
- `make test`: `-machine q35 -m 2048 -display none -monitor none -serial file:test_serial.log -no-reboot -drive file=liwus_disk.img,format=raw,if=none,id=disk -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -cdrom liwusos-test.iso`
- `make smap-test`: similar with `-cpu max -smp 4 -m 4096`

## Test environment
- Disk image: `liwus_disk.img` (AHCI)
- Initrd/test ISO contains ring3 programs including `linux_syscall_probe.elf`, `linux_clone_probe.elf`
- Tests run in QEMU, results checked via serial output

## Linux probes
- `linux_syscall_probe.elf`: ET_EXEC freestanding, uses Linux x86-64 syscall convention (syscall instruction). Tests `getrandom(318)` with CPUID-conditional RDRAND availability, output, zero count, invalid flags and pointer; `close_range(436)` closing and CLOEXEC marking, empty ranges and invalid arguments; `prctl(157)` thread-name set/get, 15-byte truncation, EFAULT and unsupported options; conditional AVX state preservation across blocking `nanosleep` and fork/wait4; UID/GID getters, getresuid(118)/getresgid(120) (nullable outputs and EFAULT), getgroups(115) empty-group query, umask(95) set/restore, getrlimit(97)/prlimit64(302) RLIMIT_NOFILE query and errors, getpid(39), getppid(110), getcpu(309) CPU/node output, null pointers and EFAULT, fork/execve(59)/wait4 static-ELF round trip and argv, close-on-exec success and failed-exec preservation for `pipe2`, `dup3`, `F_DUPFD_CLOEXEC`, plus `F_GETFD/F_SETFD`; `pipe2(O_CLOEXEC|O_NONBLOCK)` and `F_SETFL` pipe state cover EAGAIN, shared status through dup, round-trip and short-write atomicity; uname(63), getcwd/chdir/fchdir, `faccessat`/`faccessat2(flags=0)`/`mkdirat`/`unlinkat`/`newfstatat` relative dirfd paths, gettimeofday/time/sysinfo/clocks/sleeps/yield, legacy `open(2)` including x86-64 `O_LARGEFILE`, `unlink(87)`, `mkdir(83)`, `rmdir(84)`, `rename(82)`, dirfd-relative `renameat(264)`, and `renameat2(316, flags=0)` plus rejection of unsupported flags, `openat` dirfd-relative and absolute-path behavior, `EBADF`/`ENOTDIR`, mode/umask/O_EXCL/access-mode/CLOEXEC/O_DIRECTORY (`ENOTDIR`/`EISDIR`)/unsupported-flags, `O_APPEND` at open and `F_SETFL` with seek/write/readback plus `F_SETFL(O_NONBLOCK)` store/get/clear on regular files and pipes, including `O_LARGEFILE` ignored as musl sends it; open-time `O_NONBLOCK`/`O_NOFOLLOW` cases, `fstat` stream and pipe types, `pread64`/`pwrite64` with position preservation and zero-filled gaps, pipe/dup/dup2, fcntl(F_DUPFD/F_GETFL), readv/writev, access/faccessat, stat/getdents64, `statx(332)` layout/mask/path/dirfd/error cases, invalid syscall -ENOSYS, arch_prctl FS get/set, FS:0 access, mmap/mprotect/munmap, private WAIT_BITSET/WAKE_BITSET and FUTEX_REQUEUE/CMP_REQUEUE cases, ELF auxv including page-aligned AT_BRK, and Linux brk(12) query/grow/zero-fill/shrink/error behavior. See `tests/ring3/linux_syscall_probe.S`; `make test-all` passes 51 kernel cases and 23 ring-3 markers in test, ring3 and SMAP QEMU modes.
- `linux_clone_probe.elf`: Tests clone with CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD (+CLONE_SETTLS, CLONE_PARENT_SETTID, CLONE_CHILD_SETTID, CLONE_CHILD_CLEARTID), shared memory, independent FS TLS, per-thread `prctl` comm inheritance/isolation, thread exit/exit_group, private futex WAIT/WAKE/WAIT_BITSET, relative/absolute timeout and bitset cases.

## External Linux binaries (optional)
If you have a Linux ELF binary to test:
```sh
make ring3-test LINUX_STATIC_ELF=/path/to/binary
```
It will be copied temporarily as `/linux_glibc_hello` into the test image. The ring-3 Linux probe then calls Linux `execve` for that path and the harness checks its output marker and exit status. `make smap-test LINUX_STATIC_ELF=/path/to/binary` also works. Override the default hello marker with `LINUX_STATIC_EXPECT='exact application success line'` when testing another ELF, such as one of the pthread probes. The static musl and static glibc smoke artifacts built from `tests/linux/musl_hello.c` print `Hello from an unmodified static GNU/Linux ELF`. The glibc archive advertises x86-64-v3/v4 instructions; it passes this greeting smoke after per-thread x87/SSE/AVX XSAVE support was added, under QEMU `-cpu max`. This does not validate AVX-512 code paths or dynamic linking.

`tests/linux/musl_syscall_smoke.c` is a wider static musl smoke covering 256 KiB allocation/free, CPUID-conditioned `getrandom`, clock/time APIs, task name, CWD, `sched_getaffinity(204)` for the BSP-only user scheduler, `posix_fadvise` regular-file and pipe/error cases, `preadv(295)`/`pwritev(296)` and `preadv2(327)`/`pwritev2(328)` over two vectors, positional-offset preservation, negative legacy offsets, and unsupported flags, buffered stdio file create/write/seek/read/close/rename/unlink, `stat`/`fstat`/`access`, `O_NOFOLLOW` open and `O_NONBLOCK` F_GETFL/F_SETFL on a regular file, `fsync(74)`/`fdatasync(75)`/`syncfs(306)` on a regular file, `EINVAL` for sync on a pipe and `EBADF` for a closed fd, `pipe2(O_CLOEXEC|O_NONBLOCK)` with CLOEXEC/EAGAIN/round-trip, mkdir/rmdir, and root directory iteration. It exercises the legacy x86-64 `open(2)`/`unlink(87)` syscalls used by musl, including `O_LARGEFILE` on open, plus musl's `getdents64` path. The current static musl smoke passed both ring3 and four-vCPU SMAP integration after the vectored syscall changes. A matching static glibc build also passed four-vCPU SMAP with this coverage; the glibc run confirms `opendir`/`readdir` with its `O_NONBLOCK` directory-open flag.

## Logs and expected results
- Serial logs written to `test_serial.log` (for test targets)
- Expected: `RESULT: 51/51 PASS`, `RESULT: ALL PASS`, `LIWUS_BOOT_READY`, and probe messages like `[RING3 RESULT] Linux x86-64 SYSCALL getrandom/ids/resuid-resgid-groups/umask/rlimit/prlimit/time/sysinfo/sleep/yield/clocks/clock_nanosleep/access/faccessat/.../pread64/pwrite64/getcwd/dup/dup2/dup3/fcntl/mprotect/brk-futex-bitset/requeue/... PASS` and `[RING3 RESULT] Linux clone, private futex WAIT/WAKE/WAIT_BITSET/timeout ... PASS`
- smap-test also shows AP initialization messages

## Distinguishing kernel vs environment failures
- Kernel panics/crashes show in serial; timeout means QEMU killed or hung
- Test framework reports PASS/FAIL counts
- Check `test_serial.log` for details
- Ensure QEMU has enough memory (-m 2048/4096)
- If tests hang, check for deadlocks in futex/wait queues

## Notes
- No permanent paths to external binaries are stored
- All tests must run with `-Werror` clean build
