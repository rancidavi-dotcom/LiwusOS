# LIWUS Native SDK v0.1 and `.lxe`

## What `.lxe` means

`.lxe` is the official filename suffix for a statically linked native LIWUS
64-bit executable. The file itself is an ordinary little-endian x86-64 ELF64
`ET_EXEC` image with `PT_LOAD` segments. It is not a container, archive, or new
binary format. The kernel selects the ELF loader from the file contents and
does not assign special semantics to the suffix.

The SDK linker script creates separate RX text, R/NX read-only data, and RW/NX
data segments. The current loader accepts static `PT_LOAD` images. Dynamic
linking, `PT_INTERP`, relocations, shared libraries, and a full libc are outside
v0.1.

## Build the SDK package

From the repository root:

```sh
make sdk
```

This creates an ignored, reproducible package tree:

```text
build/liwus-sdk/
  bin/native-hello.lxe
  include/liwus/       # public SDK headers
  lib/crt0.o           # ELF entry and C runtime handoff
  lib/libliwus.a       # syscall wrappers, CRT, small string helpers
  share/liwus/elf64.ld # static x86-64 ELF linker script
```

`make all` also puts the sample at `/bin/native-hello.lxe` in the initrd. The
shell's existing `/bin` lookup lets it be run as `native-hello.lxe`.

## Build another program

Programs provide `int main(int argc, char **argv, char **envp)`. Compile without
host startup files, PIE, stack protector, or red-zone use, then link with the
SDK CRT and linker script:

```sh
SDK=build/liwus-sdk
gcc -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding \
  -fno-builtin -fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
  -I"$SDK/include" -c hello.c -o hello.o
ld -m elf_x86_64 -nostdlib -static --build-id=none \
  -T "$SDK/share/liwus/elf64.ld" -o hello.lxe \
  "$SDK/lib/crt0.o" hello.o "$SDK/lib/libliwus.a"
```

Include `liwus/liwus.h` for the SDK umbrella API. The v0.1 surface includes
`liw_syscall0` through `liw_syscall6`, wrappers for common process, file,
memory, directory, and pipe calls, `liw_write_all`, basic string helpers, and
the native `liw_stat`/`liw_timeval` payloads. Public names are prefixed with
`liw_`; the SDK does not claim to provide POSIX or Linux headers.

Syscall wrappers return the native syscall result without translating it into
a uniform `errno` convention. The native ABI currently mixes `-1` and
negative error numbers; callers should treat negative results as failure.
Native open and mmap flags use the `LIW_` constants in the SDK headers and must
not be assumed to match Linux values. `liw_lseek` exposes the implemented
32-bit signed offset currently accepted by the kernel.

## Regression test

The QEMU test image includes the same `.lxe` at its initrd root and at
`/bin/native-hello.lxe`. Ring 3 forks and calls the existing native `execve`
syscall on the `/bin` path. The child checks the `crt0` `argc`/`argv`/`envp`
handoff; `getpid`, `gettimeofday`, `getcwd`, `stat`/`fstat`,
open/read/write/lseek/close, mkdir/chdir/rename/unlink/rmdir, anonymous
`mmap`/`munmap`, pipe, dup/dup2, and `write_all`. It then uses
fork/execve/waitpid to start a second `.lxe` child and checks its exit status.
A separate integration check launches the artifact through the kernel's
initrd program path. Resource counters verify that the probes leave no VFS
lookup nodes, open file descriptions, or pipe objects behind. No loader
behavior is added for `.lxe`; these tests verify the SDK output through the
existing ELF path.

Run the SDK build and regression with:

```sh
make sdk
make test
```

The automated regression checks runtime behavior and ELF loading. Inspect the
generated headers, archive, linker script, and `readelf -h -l
build/liwus-sdk/bin/native-hello.lxe` when validating a toolchain change.
