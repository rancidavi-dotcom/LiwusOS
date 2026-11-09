# Linux Compatibility - Implementation Status

This is a concrete audit of what is implemented vs tested vs missing. Status: **IMPLEMENTED**, **PARCIAL**, **NÃO IMPLEMENTADO**, **NÃO VALIDADO**.

## Entry, ABI selection, dispatch
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| Linux x86-64 SYSCALL entry (STAR/LSTAR/SFMASK) | IMPLEMENTADO | `src/kernel/arch/x86_64/*`, syscall stubs | Entry configured per CPU; GS swap, kernel stack switch, frame build | SYSRET return path not fully optimized/validated in all cases; uses IRETQ in practice | smap-test / arch bringup | Arch setup |
| ABI selection per task/process | IMPLEMENTADO | `include/kernel/task.h` (`task_abi_t`), `src/compat/linux/syscall.c` | `process->abi == TASK_ABI_LINUX_X86_64` gates Linux dispatcher | Set via test/harness paths currently | ring3 tests set abi for Linux tasks | Task model |
| Linux dispatcher isolation | IMPLEMENTADO | `src/compat/linux/syscall.c` | Routes by syscall number, returns `-errno` in RAX | Isolated in compat/linux/ | ring3 probes | - |
| int 0x80 (native) unchanged | IMPLEMENTADO | `src/kernel/core/syscall.c` | Native ABI untouched | Separate from Linux | Native tests | - |

## Process/Thread model
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| Process/thread separation | IMPLEMENTADO | `include/kernel/task.h`, `src/kernel/sched/task.c` | `process_t` resources, `thread_t` execution; `task_t` alias | Fully established | General tests | - |
| clone() basic shared-thread | PARCIAL | `src/compat/linux/syscall.c:clone`, `src/kernel/sched/task.c` | Flags: `CLONE_VM\|CLONE_FS\|CLONE_FILES\|CLONE_SIGHAND\|CLONE_THREAD` (+ `CLONE_SETTLS`). Stack/TLS validated. | Only this combination; no general resource cloning, no TID pointers fully validated, no rollback/stress | linux_clone_probe | Threading |
| gettid(186) | IMPLEMENTADO | `src/compat/linux/syscall.c` | Returns thread id | - | linux_clone_probe | - |
| exit(60) thread exit | IMPLEMENTADO | `src/compat/linux/syscall.c`, `src/kernel/sched/task.c` | Terminates calling thread | - | linux_clone_probe | - |
| exit_group(231) process exit | IMPLEMENTADO | `src/compat/linux/syscall.c`, `src/kernel/sched/task.c` | Terminates all threads in process | - | linux_clone_probe | - |
| fork() Linux path | NÃO VALIDADO | Uses native fork path behavior | May go through native fork semantics | Linux semantics of fork not fully validated separately from native | - | - |

## TLS/arch_prctl
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| ARCH_SET_FS/GET_FS (0x1002/0x1003) | IMPLEMENTADO | `src/compat/linux/syscall.c:arch_prctl` | Per-thread FS base, written to MSR, stored in thread | Basic validation only | linux_syscall_probe | TLS per-thread |
| ARCH_SET_GS/GET_GS (0x1001/0x1004) | IMPLEMENTADO | `src/compat/linux/syscall.c:arch_prctl` | Per-thread GS base | Basic validation only | Basic via clone/TLS paths | - |
| CLONE_SETTLS | IMPLEMENTADO | `src/compat/linux/syscall.c`, `src/kernel/sched/task.c` | Sets TLS for child thread | Only with supported clone flags | linux_clone_probe | clone |
| FS:0 access works | IMPLEMENTADO | Tested via probe | Confirms TLS base applied | - | linux_syscall_probe | - |

## File I/O / stat / open
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| read(0) | IMPLEMENTADO | `src/compat/linux/syscall.c` -> native `sys_read` | Translated | Count limited to UINT32_MAX | linux_syscall_probe | VFS |
| write(1) | IMPLEMENTADO | `src/compat/linux/syscall.c` -> native `sys_write` | Translated | Count limited to UINT32_MAX | linux_syscall_probe | VFS |
| open(2) not directly exposed in probe; openat used | PARCIAL | Translation paths exist via native | - | openat is primary tested path | - | - |
| openat(257) | PARCIAL | `src/compat/linux/syscall.c` | Only AT_FDCWD (-100), translates O_CREAT/O_TRUNC/O_APPEND to native flags | No other dirfd, no O_CLOEXEC, limited flag translation | linux_syscall_probe (-ENOENT case) | - |
| close(3) | IMPLEMENTADO | `src/compat/linux/syscall.c` | Returns Linux errno on failure | - | Indirect | - |
| stat(4), lstat(6) | PARCIAL | `src/compat/linux/syscall.c` | Converted to Linux x86-64 `struct stat` (144 bytes) layout | Symlinks not implemented in VFS; lstat treated same as stat | linux_syscall_probe | - |
| fstat(5) | IMPLEMENTADO | `src/compat/linux/syscall.c` | Same conversion | - | linux_syscall_probe | - |
| newfstatat(262) | PARCIAL | `src/compat/linux/syscall.c` | Only AT_FDCWD, flags must be 0 | Restricted | - | - |
| lseek(8) | IMPLEMENTADO | `src/compat/linux/syscall.c` | Translates with bounds checking | - | Implicit | - |

## Memory
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| mmap(9) | IMPLEMENTADO | `src/compat/linux/syscall.c` -> native `sys_mmap` | 4th arg in R10 (x86-64 Linux ABI) | Basic anonymous mappings work as tested; full Linux mmap semantics partial | linux_syscall_probe | VM/VMA |
| munmap(11) | IMPLEMENTADO | `src/compat/linux/syscall.c` -> native `sys_munmap` | - | - | linux_syscall_probe | - |
| brk(12) | IMPLEMENTADO | `src/compat/linux/syscall.c` -> native `sys_brk` | - | - | Basic | - |
| mprotect/mremap/madvise/msync | NÃO IMPLEMENTADO | - | - | Not in current dispatcher | - | VM extensions |
| Demand paging/COW/file-backed mmap | NÃO IMPLEMENTADO | - | - | Planned later | - | VM work |

## Synchronization
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| futex(202) FUTEX_WAIT_PRIVATE (0) | IMPLEMENTADO | `src/compat/linux/syscall.c`, `src/kernel/sched/task.c` | Keyed by (mm, aligned addr), value check before block, wait queue blocking | No timeout (regs->r10 != 0 -> ENOSYS), private only | linux_clone_probe | wait queues |
| futex(202) FUTEX_WAKE_PRIVATE (1) | IMPLEMENTADO | `src/compat/linux/syscall.c`, `src/kernel/sched/task.c` | Wake up to val waiters, returns count woken | - | linux_clone_probe | wait queues |
| Futex shared (no PRIVATE flag) | NÃO IMPLEMENTADO | Returns ENOSYS | - | Explicitly rejected | linux_clone_probe tests rejection | - |
| FUTEX_REQUEUE | NÃO IMPLEMENTADO | Returns ENOSYS | - | Planned | - | - |
| Timeout support | NÃO IMPLEMENTADO | Returns ENOSYS if timeout passed | - | Planned | - | - |
| Robust lists | NÃO IMPLEMENTADO | - | - | Planned | - | - |

## Process control / signals / credentials
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| getpid(39) | IMPLEMENTADO | `src/compat/linux/syscall.c` -> native | - | - | linux_syscall_probe | - |
| Signals (sigaction/procmask/sigreturn/sigaltstack) | NÃO IMPLEMENTADO | - | - | - | - | Signal subsystem |
| kill/tgkill | NÃO IMPLEMENTADO | Native kill exists differently; Linux paths not implemented | - | - | - | - |
| wait4 / waitpid Linux semantics | PARCIAL | Native waitpid exists but Linux-specific semantics incomplete | - | WNOHANG/status codes/children model need Linux semantics | - | wait queues/process model |
| Credentials/groups/sessions | PARCIAL/ NÃO IMPLEMENTADO | Fields exist in process_t (uid/euid/gid/egid/umask) but not fully wired for Linux | Mostly zero/default | Not fully implemented per Linux semantics | - | - |
| set_tid_address/set_robust_list | NÃO IMPLEMENTADO | - | - | - | - | - |
| prctl | NÃO IMPLEMENTADO | - | - | - | - | - |

## Other
| Funcionalidade | Estado | Localização | Semântica suportada | Limitações conhecidas | Teste | Dependências |
|---|---|---|---|---|---|---|
| vDSO | NÃO IMPLEMENTADO | - | - | - | - | - |
| Auxv (minimal) | NÃO VALIDADO/INCOMPLETO | Loader paths incomplete for full Linux auxv | Native loader has minimal auxv | Linux needs AT_PHDR/PHENT/PHNUM/ENTRY/BASE/PAGESZ/RANDOM etc. | - | Loader |
| PT_INTERP/dynamic linker | NÃO IMPLEMENTADO | - | - | glibc dynamic won't load | - | Loader/VM |
| /proc, /dev, epoll, eventfd, etc. | NÃO IMPLEMENTADO | - | - | Future phases | - | Various |
| i386/int 0x80 Linux | NÃO IMPLEMENTADO | - | Not planned for initial gates | - | - | - |

## Summary
- **Working well**: SYSCALL entry/dispatch, ABI selection, basic file ops translation, mmap/munmap/brk, getpid, arch_prctl TLS, clone+threads (limited), private futex WAIT/WAKE, exit/exit_group. Freestanding ET_EXEC Linux probes pass.
- **Major gaps**: PT_INTERP/dynamic linking, broad syscall coverage, signals, wait4 Linux semantics, futex extensions (shared/REQUEUE/timeouts/robust), full clone semantics, pthreads/libc full support, demand paging/COW/file-backed mmap, vDSO/auxv completeness, cross-CPU thread migration validation.
