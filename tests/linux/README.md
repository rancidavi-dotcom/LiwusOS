# External Linux ELF smoke test

This source builds a normal Linux x86-64 ELF with musl. It is not linked to the
LIWUS SDK and contains no LIWUS-specific syscall code. The greeting is
libc-neutral so a static glibc build of the same source can use the same
external-ELF harness check.

With an x86-64 musl toolchain installed:

```sh
musl-gcc -static -O0 -march=x86-64 -mtune=generic \
  -o /tmp/liwus-musl-hello tests/linux/musl_hello.c
readelf -l /tmp/liwus-musl-hello   # confirm there is no INTERP segment
make smap-test LINUX_STATIC_ELF=/tmp/liwus-musl-hello
```

The broader static-libc smoke exercises a 256 KiB allocation/free, CPUID-aware
`getrandom`, `uname`, `time`, monotonic `clock_gettime`/`clock_getres`,
`sched_yield`, `getcwd`, `prctl` name set/get, buffered file
create/write/seek/read/close/rename/unlink through libc, `stat`/`fstat`/`access`,
and directory `opendir`/`readdir`/`closedir`. It also exercises
`pipe2(O_CLOEXEC|O_NONBLOCK)`, `F_GETFL`, nonblocking `EAGAIN`, pipe round-trip
and close. This covers musl's `open(2)` with the x86-64 `O_LARGEFILE` flag,
`O_NOFOLLOW`, regular-file `O_NONBLOCK` status queries/updates (including
musl's immutable `O_LARGEFILE` argument to `F_SETFL`), `fsync(74)`/
`fdatasync(75)`, `unlink(87)`, and libc
`mkdir(83)`/`rmdir(84)`; directory iteration uses `getdents64`. The same smoke
also checks Linux `preadv(295)`/`pwritev(296)` using two iovecs, the
`preadv2(327)`/`pwritev2(328)` zero-flags path, preservation of the shared file
offset, rejection of unsupported RWF flags, and a negative offset on legacy
`preadv` returning `EINVAL`.

```sh
musl-gcc -static -O0 -march=x86-64 -mtune=generic \
  -o /tmp/liwus-musl-syscall-smoke tests/linux/musl_syscall_smoke.c
readelf -l /tmp/liwus-musl-syscall-smoke  # confirm there is no INTERP segment
make smap-test LINUX_STATIC_ELF=/tmp/liwus-musl-syscall-smoke \
  LINUX_STATIC_EXPECT='static Linux libc syscall smoke PASS'
```

The test follows CPUID. If RDRAND is advertised it requires a full 32-byte
`getrandom` result; otherwise it requires musl to report `EIO`, matching the
personality's no-weak-fallback policy. It has passed both the default ring-3
QEMU CPU and the four-vCPU SMAP configuration.

The same source also builds as static glibc. The host glibc archive may use AVX
instructions, so run this variant only in the `smap-test` target (`-cpu max`):

```sh
gcc -static -O0 -march=x86-64 -mtune=generic \
  -o /tmp/liwus-glibc-syscall-smoke tests/linux/musl_syscall_smoke.c
readelf -l /tmp/liwus-glibc-syscall-smoke  # confirm there is no INTERP segment
make smap-test LINUX_STATIC_ELF=/tmp/liwus-glibc-syscall-smoke \
  LINUX_STATIC_EXPECT='static Linux libc syscall smoke PASS'
```

To verify the currently supported one-thread libc path, build a static musl
pthread program with a caller-provided stack:

```sh
musl-gcc -static -O0 -march=x86-64 -mtune=generic -pthread \
  -o /tmp/liwus-musl-pthread tests/linux/musl_pthread_static_stack.c
readelf -l /tmp/liwus-musl-pthread  # confirm there is no INTERP segment
make smap-test LINUX_STATIC_ELF=/tmp/liwus-musl-pthread \
  LINUX_STATIC_EXPECT='static musl pthread create/join with caller stack PASS'
```

This exercises musl's `clone` flags, parent-TID write, child-clear-TID wake,
TLS, thread startup and `pthread_join` using an application-owned stack.

To cover musl's default stack, guard mapping and TLS setup, build the one-thread
probe without supplying a stack:

```sh
musl-gcc -static -O0 -march=x86-64 -mtune=generic -pthread \
  -o /tmp/liwus-musl-pthread-default tests/linux/musl_pthread_default_stack.c
readelf -l /tmp/liwus-musl-pthread-default  # confirm there is no INTERP segment
make smap-test LINUX_STATIC_ELF=/tmp/liwus-musl-pthread-default \
  LINUX_STATIC_EXPECT='static musl default-stack pthread create/join PASS'
```

For a four-thread mutex/join regression on normal default stacks:

```sh
musl-gcc -static -O0 -march=x86-64 -mtune=generic -pthread \
  -o /tmp/liwus-musl-pthread-smp tests/linux/musl_pthread_smp.c
readelf -l /tmp/liwus-musl-pthread-smp  # confirm there is no INTERP segment
make smap-test LINUX_STATIC_ELF=/tmp/liwus-musl-pthread-smp \
  LINUX_STATIC_EXPECT='static musl 4-thread mutex/join PASS'
```

Each thread sets and reads its own pthread name, increments a shared counter
under a pthread mutex, and the parent joins all threads. This checks the
default stack guard/TLS path, `clone`, `prctl`-backed thread naming, private
futex wait/wake and child-clear-TID join behavior. A four-thread pass does not
establish cross-CPU migration or broad POSIX support. Set
`LINUX_STATIC_EXPECT` to the exact success line printed by the supplied ELF;
the default remains the hello text above.

The QEMU ring-3 runner passes the supplied ELF into the Linux probe, which
replaces itself with Linux `execve`. This validates the Linux `execve` stack
and auxv for a real static ELF, then checks its output/exit status. Keep
generated ELF files outside the repository. The hello program covers only
static libc `puts`. The caller-stack probe covers one create/join; the
default-stack probe, four-thread mutex probe and four-thread condition-variable
probe cover only those specific libc paths. Dynamic linking and general Linux
compatibility remain unvalidated.

For the condition-variable broadcast/join probe:

```sh
musl-gcc -static -O0 -march=x86-64 -mtune=generic -pthread \
  -o /tmp/liwus-musl-pthread-condvar tests/linux/musl_pthread_condvar.c
make smap-test LINUX_STATIC_ELF=/tmp/liwus-musl-pthread-condvar \
  LINUX_STATIC_EXPECT='static musl 4-thread condition-variable broadcast/join PASS'
```

The static pthread smoke cases have been observed passing in the QEMU SMAP
configuration with four virtual CPUs; this does not test cross-CPU migration.
The default `ring3-test` CPU does not advertise AVX. Use a baseline ELF there;
the host's static glibc archive emits x86-64-v3 instructions and must be tested
with `make smap-test ...` (`-cpu max`). Running that glibc ELF on the default
virtual CPU correctly traps at its first unsupported AVX instruction.
