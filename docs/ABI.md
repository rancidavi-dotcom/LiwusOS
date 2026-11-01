# LiwusOS native ABI (v1)

This document freezes the syscall interface of the lean kernel as of Gate 0.
The Linux ABI is a separate compatibility layer described in `ABI_LINUX.md` and
must not change anything described here. Any change to this document requires a
matching kernel change and a test in `tests/`.

Gate 0's syscall numbers and native UAPI contracts below remain frozen.
Memory and protection notes are updated to describe the current kernel; they do
not imply that the Linux personality implements the full Linux ABI.

## 1. Syscall entry

- Instruction: `int $0x80` from ring 3 (IDT vector 128, DPL 3).
- Number: `rax`.
- Arguments: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` (C calling convention order).
- Result: `rax`.
- At most six arguments. The native ABI uses `int 0x80`; the separate Linux
  personality has an initial x86-64 `SYSCALL` entry (roadmap step 35).
- 32-bit user tasks use the same register convention. This is **not** the i386
  Linux convention (`ebx`, `ecx`, `edx`, `esi`, `edi`, `ebp`).

The handler validates user pointers with `copy_from_user`/`copy_to_user` before
dereferencing them. `do_execve` rejects calls whose `cs` is not a ring-3
selector.

## 2. Syscall table

| # | Name | Arguments | Success result |
|---|---|---|---|
| 1 | `exit` | `int status` | does not return |
| 2 | `brk` | `uint64_t addr` | new program break |
| 3 | `read` | `int fd, void *buf, uint32_t count` | bytes read |
| 4 | `write` | `int fd, const void *buf, uint32_t count` | bytes written |
| 5 | `open` | `const char *path, int flags` | fd |
| 6 | `close` | `int fd` | `0` |
| 7 | `waitpid` | `int pid, int *status, int options` | child pid |
| 8 | `timer` | — | ticks (100 Hz) |
| 10 | `key_event` | `struct { uint8_t scancode; int pressed; } *` | `1` if an event was consumed, `0` otherwise |
| 11 | `key_press` | `uint8_t scancode` | `0`/`1` |
| 14 | `fork` | — | child pid in the parent, `0` in the child |
| 15 | `execve` | `const char *path, char *const argv[], char *const envp[]` | replaces the image |
| 16 | `tcgetattr` | `int fd, struct termios *` | `0` |
| 17 | `tcsetattr` | `int fd, int action, const struct termios *` | `0` |
| 18 | `ioctl` | `int fd, unsigned long request, void *arg` | `0` |
| 19 | `lseek` | `int fd, int64_t offset, int whence` | new offset |
| 20 | `getpid` | — | pid |
| 21 | `gettimeofday` | `struct { int64_t tv_sec, tv_usec; } *, void *tz` | `0` |
| 22 | `stat` | `const char *path, struct kernel_stat *` | `0` |
| 23 | `fstat` | `int fd, struct kernel_stat *` | `0` |
| 24 | `unlink` | `const char *path` | `0` |
| 25 | `mkdir` | `const char *path, uint32_t mode` | `0` |
| 26 | `chdir` | `const char *path` | `0` |
| 27 | `getcwd` | `char *buf, uint32_t size` | `0` |
| 28 | `kill` | `int pid, int sig` | `0` (the signal number is ignored; the target is killed) |
| 29 | `rmdir` | `const char *path` | `0` |
| 30 | `getdents` | `uint32_t fd, void *buf, uint32_t count` | bytes written |
| 31 | `pipe` | `int fds[2]` | `0` |
| 32 | `dup` | `int fd` | new fd |
| 33 | `dup2` | `int oldfd, int newfd` | `newfd` |
| 34 | `task_snapshot` | `task_info_t *out, int max` | number of tasks (not declared in `syscall_nums.h`) |
| 35 | `mmap` | `void *addr, uint64_t len, int prot, int flags, int fd, uint64_t off` | mapped address |
| 36 | `munmap` | `void *addr, uint64_t len` | `0` |
| 37 | `mouse_get` | `mouse state *` | `0` |
| 38 | `rename` | `const char *old, const char *new` | `0` |
| 39 | `set_foreground` | `int pid` | `0` |
| 40 | `job_create` | — | job id |
| 41 | `job_foreground` | `int job` | `0` |

Numbers 9, 12 and 13 are unused. Numbers and semantics are LiwusOS-specific:
they are not Linux syscall numbers and must not be interpreted as such.

## 3. Return values and errno (current state)

The native ABI is **not uniform** today. It mixes:

- `-1` for generic failure (`open`, `close`, `stat`, `ioctl`, `kill`, ...);
- `-errno` for some paths (`execve` returns `-ENOENT`, `-ENOEXEC`, `-ENOMEM`;
  usercopy helpers return `-EFAULT`, `-E2BIG`);
- `MAP_FAILED` (`(uint64_t)-1`) for `mmap` failures.

Until the Linux ABI boundary is implemented, user programs must not depend on a
specific negative value: treat `rax < 0` (or `rax == (uint64_t)-1` for `mmap`)
as failure. Standardizing internal helper error returns is tracked as part of
roadmap step 36 and must not silently change the native ABI without updating
this document.

## 4. Current address space and kernel layout

| Range | Use |
|---|---|
| `0x0000000000000000` | null guard page, never mapped |
| `0x0000000000000000`–`0x0000000000000fff` | permanent null guard |
| `0x0000000000100000`–`0x0000800000000000` | canonical userspace; concrete mappings are tracked by per-mm VMAs |
| `0x0000000001000000` | boot stub `.boot`, VMA=LMA, entered by GRUB before the higher-half jump |
| `0x0000000002000000` | physical load address of the kernel image |
| `0xffffffff80000000` | kernel image VMA; text RX, rodata R/NX, data+bss RW/NX |
| `0xffff800000000000` + physical | direct-map alias, configured up to 512 GiB |
| `0xffffc00000000000` onward | guarded kernel stack slots |
| `0x0000555555554000` | deterministic development base for PIE ET_DYN |
| near `0x00007ffffffff000` | user stack region; exact initial top is loader-defined |

- Physical memory comes from the Multiboot2 map. The PMM allocates usable frames
  through the direct map; the QEMU 4 GiB test allocated, accessed and freed a
  frame above 1 GiB. It does not test every address up to the 512 GiB limit.
- The kernel image VMA is `0xffffffff80000000` and its LMA is 32 MiB.
  `virt_to_phys()` handles image/direct-map pointers; `phys_to_virt()` makes a
  direct-map alias. CR3 and PTE entries always contain physical addresses.
- User intervals are bounded by `USER_ADDR_MAX=0x0000800000000000`, validated
  for canonicality, overflow, VMA overlap and per-page U/S permissions. The
  former low identity alias has been removed after boot.
- Page tables are eager. A user page fault terminates that task; demand paging
  is roadmap step 27, outside the current memory gate.
- ELF loading, process VM changes and kernel/MMIO mappings target an explicit
  page directory. `_in` APIs are used for active paths; wrappers without `_in`
  remain for boot convenience.

## 5. Processes and threads

- A process and its schedulable thread are separate kernel objects. For now
  each process has exactly one thread; there is no `clone` or signal delivery.
  The native `fork` and `waitpid` contract remains unchanged. FS-base storage
  belongs to the thread object for reuse by the Linux personality.
- `fork` copies the entire address space (`vmm_copy_directory`), with no COW.
- Kernel task stacks use 16–32 KiB virtual slots with unmapped guards. IRQ
  stacks are per CPU; page-fault and double-fault paths use per-CPU IST stacks.
- Each process has 32 file descriptors. 0–2 are standard streams; the first
  free descriptor is searched from 3. `dup`/`fork` share a reference-counted
  `file_description_t`, so offsets are shared as in POSIX.
- Pipes hold 4096 bytes and block by yielding the CPU (`switch_task`), not by a
  wait queue.
- `kill` terminates the target immediately; signal numbers are ignored.
- `waitpid` blocks by yielding and returns the raw exit code, not a wait status.

## 6. ELF and initial stack

- Supported: ELF32 and ELF64, `ET_EXEC`/`ET_DYN` headers, `PT_LOAD` segments.
- Not supported: `PT_INTERP`, `PT_TLS`, `PT_GNU_STACK`, dynamic linking,
  relocations, shebang scripts.
- A segment is rejected if it is outside the user interval, overflows, overlaps
  an existing VMA, has `p_memsz < p_filesz`, or requests a W+X page.
- The initial stack is eagerly mapped as 64 KiB ending at
  `USER_STACK_TOP=0x00007ffffffff000`.
- 64-bit layout, ascending: `argc`, `argv[0..argc-1]`, `NULL`, `envp...`,
  `NULL`, `AT_NULL` (type `0`), value `0`. The stack pointer is 16-byte aligned.
- 32-bit layout: same shape with 32-bit words and no `AT_NULL` pair.
- There is no `AT_PHDR`, `AT_ENTRY`, `AT_RANDOM` or `AT_SYSINFO_EHDR`. This is
  sufficient for the current freestanding programs and insufficient for any
  glibc/musl binary; fixing it is roadmap step 32.

## 7. UAPI structures

### `open` flags

| Flag | Native value |
|---|---|
| `O_CREAT` | `0x0200` |
| `O_TRUNC` | `0x0400` |
| `O_APPEND` | `0x0008` |

Access mode bits are not interpreted; these values differ from Linux and are
translated only at the Linux boundary.

### `mmap` flags and protections

`PROT_READ=0x1`, `PROT_WRITE=0x2`, `PROT_EXEC=0x4`, `MAP_PRIVATE=0x02`,
`MAP_FIXED=0x10`, `MAP_ANONYMOUS=0x20`. Only anonymous mappings are supported;
`fd` and `offset` are ignored.

### `struct kernel_stat`

64-bit fields in this order: `st_dev`, `st_ino`, `st_mode` (32-bit),
`st_nlink`, `st_uid`/`st_gid` (32-bit), `st_rdev`, `st_size`, then
`st_atime`/`st_atimensec`, `st_mtime`/`st_mtimensec`, `st_ctime`/`st_ctimensec`,
`st_blksize`, `st_blocks`, `st_spare4[2]`. This follows the newlib layout, not
the Linux x86-64 layout.

### `struct dirent` (getdents)

`uint64_t d_ino`, `int64_t d_off`, `uint16_t d_reclen`, `uint8_t d_type`
(`K_DT_DIR=4`, `K_DT_REG=8`), `char d_name[256]`. Not the Linux `dirent64`
layout.

### `termios` and ioctls

`struct termios` uses `uint32_t` flags and `c_cc[32]` (see
`include/uapi/termios.h`). Supported ioctls: `TCGETS`, `TCSETS`, `TCSETSW`,
`TCSETSF`, `TIOCGWINSZ` (reports 80×25). Anything else returns `-1`.

## 8. User/kernel boundary (current implementation)

- A user address is valid only if every covered page is present and has
  `PTE_U`. The check walks the four page-table levels for each page.
- User writes additionally require `PTE_W`.
- A CPU exception taken in ring 3 terminates only the current user task.
- `execve` requires `cs & 3 == 3`; kernel threads cannot call it.
- EFER.NXE is enabled when available; ELF and mmap permissions enforce W^X.
- SMEP and SMAP are enabled when available. Usercopy brackets intentional
  accesses with STAC/CLAC; QEMU validation passed with SMAP active.
- Ring-3 #PF runs on IST1; its frame is moved to the task kernel stack before
  the task is terminated and the scheduler runs.

## 9. Known gaps (tracked in `ROADMAP.md`)

1. Inconsistent `-1` vs `-errno` returns (step 36).
2. Linux `SYSCALL` entry has an initial tested path; validated SYSRET return,
   the broad Linux syscall surface and Linux-specific auxv remain incomplete
   (steps 32–36).
3. No auxv beyond `AT_NULL`, no vDSO (steps 32–34).
4. No threads, TLS, signals or futex (steps 18–26).
5. No demand paging, COW or file-backed `mmap` (steps 27–30).
6. Linux threads, clone, and cross-CPU task migration (steps 18–20).
7. 32 file descriptors and no `O_CLOEXEC` (step 37).
