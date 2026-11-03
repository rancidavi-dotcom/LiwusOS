# Test inventory and coverage matrix

Snapshot: 2026-11-03, branch `lean-kernel`, after the SDFS rename transaction and concurrent mutation regressions. The original kernel runner had 47 top-level cases. The current
runner has 51 top-level cases after adding `pmm_contract`, `kheap_contract`,
`sdfs_rename_journal_atomic`, and `sdfs_concurrent_mutations`; 50 are kernel-side functions and one is the `ring3_processes` aggregator. Its 23 ring-3 pass markers are nested subcases, not extra members
of the 47-case denominator. `make test`, `make ring3-test`, and `make smap-test`
repeat this same inventory with different QEMU settings.

Legend in the matrix: `✓` means a named test directly checks that property;
`~` means partial or indirect evidence; `—` means no active test. It is not a
percentage or a claim of completeness. BSP means the bootstrap processor.

## Requirement coverage

| Subsystem | Functional | Error/negative | Boundary | Isolation | Concurrency | Cleanup/stress | Risk and active evidence |
|---|---:|---:|---:|---:|---:|---:|---|
| Boot | ~ | — | — | — | — | — | Medium. Every QEMU suite must boot; no dedicated malformed boot-protocol matrix. `smap-test` confirms AP startup. |
| PMM | ✓ | ✓ | ✓ | — | — | ~ | High. `pmm_high_memory` and `pmm_contract` cover high-frame allocation/access/free, distinct aligned frames, invalid/double-free accounting, contiguous claim/release, and invalid claim arguments. No exhaustion, reserved-map sweep, or allocation churn. |
| VMM/page tables | ~ | ~ | ~ | ~ | — | ~ | High. `kernel_stack_guard` checks mappings/guard and `ring3_processes` checks user protections. No systematic map/unmap/CR3/TLB API matrix or two-mm same-VA test. |
| VMA | ✓ | ✓ | ✓ | ~ | N/A | ✓ | Medium. `vma_intervals` plus host VMA test cover order, overlap, split, clone, heap, gaps, failure rollback and fixed-seed model stress. Host test does not exercise PTEs. |
| Kernel heap | ✓ | ~ | ✓ | — | — | ~ | High. `kheap_contract` covers reuse, shrink/grow `krealloc`, byte preservation and page-aligned allocation. No exhaustion, invalid-free validation, allocator fault injection, or concurrent allocation stress. |
| Exceptions/IST/stacks | ~ | ✓ | ✓ | ~ | — | ~ | High. `kernel_stack_guard` provokes a real guard #PF on IST and checks unmap; ring-3 tests expect child faults. No double-fault fixture or broad exception matrix. |
| Scheduler | ✓ | ~ | ~ | — | ~ | ~ | High. `sched_priority`, `sched_sleep`, `sched_wait_queue`; 8 waiters/4 wake cycles and stable CPU ticks while blocked. Tests interleaving on BSP, not SMP. |
| Processes | ✓ | ~ | ~ | ✓ | ~ | ~ | High. `process_thread_ownership` and ring-3 fork/exec/waitpid/exit tests. No systematic allocation-failure rollback, parent death, orphan lifecycle, or high-count churn. |
| Threads/TLS | ~ | ~ | — | ~ | ~ | ~ | High. Linux clone probe checks a child thread, shared mm, distinct FS TLS and individual exit. No many-thread creation/destruction or repeated FPU/TLS switching stress. |
| Wait queues | ✓ | ~ | ~ | — | ~ | ~ | High. `sched_wait_queue` checks 8 waiters, partial/all/empty wake and bounded repeat; killed blocked pipe reader tests removal and cleanup. No simultaneous AP wake race. |
| Futex | ~ | ✓ | ~ | ~ | ~ | ~ | High. Linux clone probe checks one blocking WAIT/WAKE, mismatch, read-only mismatch, bad/alignment, unsupported shared/timeout. No multiple waiter wake, same VA across mms, repeated futex stress or futex cancellation. |
| Pipes | ✓ | ✓ | ✓ | ~ | ~ | ~ | High. Ring-3 cases cover data, 4 KiB-full writer, empty reader, EOF, EPIPE, fork/exec, invalid output, killed blocked reader. No multi-reader/writer stress or repeated close races. |
| VFS/SDFS | ✓ | ✓ | ~ | ~ | ~ | ~ | Medium-high. SDFS/VFS cases cover create/read/write/directories/rename/delete/remount/permissions/journal/CRC/disk-full and terminal redirection. Native SDK probe covers getdents records, continuation, empty/non-directory/invalid-fd, short/read-only buffers and cleanup. Shell integration checks ls/cat against actual SDFS data. Shared RAM disk and fixed order; `sdfs_concurrent_mutations` runs four kernel workers through create/write/read/delete on disjoint names, serialized by the filesystem sleep mutex. It does not test SMP execution. |
| ELF loader | ✓ | ✓ | ~ | ✓ | — | ~ | High. Ring-3 covers native ELF, ET_DYN, Linux static probes, malformed ELF, invalid argv/envp, repeated exec and segment permission faults. No parser fuzzing or header-overflow corpus. |
| Native ABI | ✓ | ✓ | ~ | ✓ | ~ | ~ | High. Ring-3 suite exercises selected `int 0x80` calls, invalid pointers, process/pipes/mmap; `native-hello.lxe` runs through crt0 and SDK wrappers. Not every published syscall has a regression. |
| Linux personality | ✓ | ✓ | ~ | ✓ | ~ | ~ | High. Two freestanding probes cover selected SYSCALL, stat/openat/mmap, arch_prctl, clone/TLS and private futex behavior. No libc/pthreads or full errno/syscall matrix. |
| SMP | ~ | — | — | — | — | — | High. `smap-test` boots APIC IDs 1–3 with four vCPUs; user tasks remain on BSP. No scheduler migration or remote TLB shootdown proof. |
| Memory protection/usercopy | ✓ | ✓ | ✓ | ✓ | — | ~ | High. Ring-3 tests cover NX, supervisor/read-only pages, cross-permission ranges, syscall output pointers, SMAP and stack guard. Not exhaustive for all syscalls or wraparound lengths. |
| Fault injection/fuzz/soak | — | ~ | — | — | — | ~ | High. Host VMA test injects one allocator failure and runs fixed-seed model operations. No kernel-wide allocator fault injection, ELF/syscall fuzzing, coverage counters or soak. |

## Active kernel cases (51 top-level)

The `name` is the stable test identifier used in serial output and in
`tests/harness/kernel_cases.txt`. The listed expected result is what the case
asserts; shared SDFS state is formatted once and then reused in this order.

| Test identifier | Source | Input / behavior and expected result |
|---|---|---|
| `terminal_files` | `tests/test_terminal.c` | Runs terminal file commands/redirection; verifies create/read, append, and truncate contents through the VFS. |
| `crc32_known` | `tests/test_sdfs_crc32.c` | CRC32 of `123456789` equals `0xCBF43926`. |
| `crc32_consistency` | same | Repeated CRC32 of fixed nonempty data is identical and nonzero. |
| `superblock_crc` | `tests/test_sdfs_v2.c` | Reads block 0 and checks V2 magic/version/block size/layout fields and computed CRC. |
| `superblock_v2_fields` | same | Checks bitmap, journal, root-block ordering and bounds against total blocks. |
| `fsck_clean` | same | Requires the mounted filesystem to remain mounted after fsck. It does not inject corruption. |
| `journal_replay_basic` | `tests/test_sdfs_journal.c` | Stages and commits a WAL block update, then replays it and compares resulting data. |
| `journal_clean_noop` | same | Requires a clean journal to report no dirty state; no write should be replayed. |
| `journal_multiple_entries` | same | Replays at least three entries and checks target bytes; corrupts a committed payload, requires replay and mount to refuse it without clearing the dirty record, restores the journal, and verifies recovery. |
| `sdfs_create_empty` | `tests/test_sdfs_create.c` | Creates an empty file; path metadata says file and size zero. |
| `sdfs_create_duplicate` | same | First create succeeds; duplicate create returns `-1`. |
| `sdfs_create_root_blocked` | same | Creating a file at `/` returns `-1`. |
| `sdfs_create_nested` | same | Creates a directory and nested empty file and checks type/size. |
| `sdfs_write_read_1block` | `tests/test_sdfs_rw.c` | Writes and reads a short 13-byte payload and compares bytes/size. |
| `sdfs_write_read_exact_block` | same | Writes/reads 4092 bytes and compares exact contents. |
| `sdfs_write_read_multiblock` | same | Writes/reads a patterned 20,000-byte payload across blocks. |
| `sdfs_overwrite` | same | Replaces a 13-byte file and checks new bytes; injects failures at WAL commit and target-directory writes, remounts after each, and checks old-or-new complete contents. |
| `sdfs_empty_file` | same | Reads an empty file and checks a non-null result with size zero. |
| `sdfs_dir_create` | `tests/test_sdfs_dir.c` | Creates a directory and verifies its type. |
| `sdfs_dir_list` | same | Creates a listing fixture and requires exactly three entries. |
| `sdfs_dir_not_empty` | same | Rejects deletion of a directory containing a child. |
| `sdfs_nested_dirs` | same | Resolves a deep nested path to a file. |
| `sdfs_concurrent_mutations` | same | Four scheduled kernel workers each create, write, read back, and delete eight distinct files; all bytes must match and all workers must finish. Exercises sleepable lock contention on the BSP. |
| `sdfs_rename_file` | `tests/test_sdfs_rename.c` | Renames a file, preserves bytes/size, and makes the old name absent. |
| `sdfs_rename_dir` | same | Renames a directory and verifies the directory and nested file paths. |
| `sdfs_rename_not_found` | same | Renaming a nonexistent path returns `-1`. |
| `sdfs_rename_overwrite` | same | Renaming onto an existing destination returns `-1`. |
| `sdfs_rename_journal_atomic` | same | Injects a pre-commit second-snapshot failure and a post-commit target write failure; remount must show a complete namespace and preserve file bytes. |
| `sdfs_delete_file` | `tests/test_sdfs_delete.c` | Creates and deletes a file; the path is absent afterward. |
| `sdfs_delete_nonexistent` | same | Deleting a missing file returns `-1`. |
| `sdfs_delete_root` | same | Deleting `/` is rejected. |
| `sdfs_delete_file_with_data` | same | Deletes a populated file and confirms it cannot be read. |
| `sdfs_delete_dir_empty` | same | Deletes an empty directory and confirms it is absent. |
| `sdfs_persist_remount` | `tests/test_sdfs_persist.c` | Writes `survive`, remounts, and checks exact bytes/size. |
| `sdfs_persist_dir_structure` | same | Remounts a nested directory fixture and checks the directory and two files. |
| `perms_set_get` | `tests/test_sdfs_perms.c` | Sets/reads file modes 0644/0755 and directory mode 0700; checks root owner-read bit. |
| `perms_default` | same | New file's default permission value is nonzero; does not assert an exact mode. |
| `timestamp_created` | same | Creates a 5-byte file and checks path info; timestamp range/clock accuracy is not asserted. |
| `timestamp_remount` | same | Checks data and size after remount for the timestamp fixture. |
| `v1_compat_read` | `tests/test_sdfs_v2.c` | Reads the `compat` fixture through current V2 filesystem behavior; does not construct a legacy V1 disk image. |
| `sdfs_diskfull` | `tests/test_sdfs_diskfull.c` | Creates/writes files until storage reports failure; requires at least one but fewer than 10,000 files. It runs after ring-3 integration because it leaves the shared RAM disk nearly full. |
| `vma_intervals` | `tests/test_vma.c` | Tests ordered insert, overlap/invalid rejection, half-open lookup, split remove, top-down gap, heap resize, clone and destroy on metadata-only directories. |
| `kernel_stack_guard` | `tests/test_kstack.c` | Verifies stack/IRQ separation and guard PTEs; deliberately faults on the guard, checks IST/#PF and confirms freed mappings disappear. |
| `pmm_high_memory` | `tests/test_pmm_high.c` | Requires RAM above 1 GiB; allocates one high frame, checks direct-map read/write and restored free-memory count. |
| `pmm_contract` | `tests/test_pmm_contract.c` | Allocates three distinct aligned high frames, checks direct-map access and cleanup; verifies double/invalid frees do not change free count; claims/releases a contiguous range and rejects invalid claim arguments. It does not test PMM exhaustion, reserved firmware-map construction, or concurrent allocation. |
| `kheap_contract` | `tests/test_kheap.c` | Checks free-list reuse, `krealloc` shrink/grow contents, `kmalloc_a` page alignment, null/zero `krealloc` behavior and cleanup. It does not test exhaustion, invalid frees, allocation failure or concurrency. |
| `process_thread_ownership` | `tests/test_process_model.c` | Attaches/detaches a thread and checks process references and ownership balance. |
| `sched_sleep` | `tests/test_sched.c` | Sleeps approximately 200 ms and checks elapsed timer ticks within a tolerance. |
| `sched_priority` | same | Runs three priority workers and compares scheduler ticks/loop progress; timing is relative and QEMU-dependent. |
| `sched_wait_queue` | same | Eight waiters block through four rounds; checks exact partial/all/empty wake counts, bounded completion, and unchanged waiter ticks while a monitor runs. |
| `ring3_processes` | `tests/test_runner_ring3.c` | Runs the ring-3 suite, native/Linux ELF probes, and a native DAC probe; checks VFS/file/pipe counters and that PMM free bytes did not decrease. See the 23 nested cases below and the additional native permission result. |

## Ring-3 nested cases (23; under `ring3_processes`)

Markers are defined in `tests/harness/ring3_cases.txt` and emitted by
`tests/ring3/suite.c`.

| Marker suffix | Verified behavior |
|---|---|
| `int 0x80 syscalls and serial write` | Native syscall dispatch and user write reach serial output. |
| `brk allocates writable user memory` | Heap growth returns writable user memory. |
| `mmap, user write, and munmap` | Anonymous mapping is writable and removable. |
| `NX blocks execution from writable anonymous memory` | Executing a data mapping faults and only the child is reaped with expected termination status. |
| `fork deep copy, child exit, and waitpid` | Child memory is isolated; raw child exit code is collected. |
| `read-only waitpid destination is rejected without modification` | Invalid output permissions are rejected and the read-only bytes stay unchanged. |
| `stat, fstat, gettimeofday, and getcwd enforce writable outputs` | Output buffers are checked before copyout. |
| `mixed-permission range is atomic and read-only mmap protection holds` | Multi-page permission validation rejects a mixed invalid range; read-only mapping stays protected. |
| `pipe and file reads reject protected output without consuming data` | Invalid destination does not consume pipe/file input. |
| `repeated open/close and shared file offset across fork` | Repeated descriptor lifetime and shared description offset are checked. |
| `pipe survives fork/exec and returns EOF after final writer closes` | Inherited endpoints, exec, and final-writer EOF. |
| `pipe write without readers returns -EPIPE; invalid descriptors fail` | Error results for closed readers and invalid fd values. |
| `pipe writer blocks at capacity and resumes after reader progress` | 8192-byte stream exceeds 4096-byte capacity, resumes and preserves byte order. |
| `pipe reader blocks empty and wakes after writer progress` | Empty read blocks, then returns a byte after writer wakeup. |
| `process exit cancels blocked pipe reader and releases pipe refs` | Kills a child synchronized into blocked read; parent wait and final I/O counters verify cleanup. |
| `fork followed by ELF64 execve, argc, argv, envp, and waitpid` | Executes a valid ELF and checks arguments/environment and child result. |
| `independent freestanding C ELF runs through execve and waitpid` | Runs a separately linked C ELF through the loader and checks its result. |
| `SDK-built .lxe ELF execve, crt0 argv/envp and syscall wrapper` | Executes the SDK artifact by `/bin/native-hello.lxe`, then checks its CRT argument/environment parsing, native `getpid`/write wrappers, SDFS file lifecycle, and getdents record/cursor/error behavior through child exit status. `.lxe` remains an ELF64 file. |
| `invalid ELF returns error and original image keeps running` | Malformed ELF is rejected without replacing the parent image. |
| `invalid filename, argv, and envp pointers are rejected safely` | Bad user pointers fail without corrupting the caller. |
| `12 repeated execve image replacements complete` | Repeated image replacement completes in a child and is reaped. |
| `write to read-only ELF segment kills only child` | Write protection on a read-only segment terminates the child. |
| `supervisor page access kills only child` | Ring-3 supervisor-only access faults and leaves the parent alive. |

The separate native permission probe (`tests/ring3/permission_probe.c`) checks
SDFS DAC decisions, `stat` metadata, access-mode enforcement on descriptors,
directory search/enumeration, create/unlink/rename authorization, reading an
unlinked open file until close, truncate behavior, invalid operation pointers,
and execute-bit enforcement. It is run inside `ring3_processes` and does not
change the 23 nested-suite marker count.

The shell integration exercises the six native utilities (`mkdir`, `rm`,
`mv`, `cp`, `touch`, `echo`) plus `ls`/`cat` against a temporary SDFS disk.
It covers `mkdir -p` creation and repeat use, copying a file into a directory,
empty and multi-block copy, no-overwrite behavior, preservation of an existing
file when touched, `pwd`/`cd` inheritance, `stat`, ten-line `head`, and empty
directory `rmdir`. The persistent install integration exercises
`mkdir`, `cp`, `mv`, `touch`, `echo`, `ls`, and `cat` across later boots. A
separate reduced QEMU smoke sequence passed all six utilities, including `rm`.

The same integration group also executes an ET_DYN PIE probe, a freestanding
Linux SYSCALL probe, a Linux clone/private-futex probe, and native
`native-hello.lxe` through crt0 and SDK wrappers. They have separate
`[RING3 RESULT]` markers but do not add to the 23 `suite.c` cases or the 49
top-level count. It also runs an external SDK-built ELF64 app and checks its
stdout and raw status 23; that result is separately validated by the serial
harness. The independent `make shell-test` drives the booted ring-3 shell over
QEMU COM1 and is not part of the 50/23 baseline manifests. `make test-unit`
also runs the bounded shell parser cases under ASan/UBSan.

## Known coverage holes

- SDFS tests use a single formatted RAM disk and fixed execution order; a case
  can depend on previous state. The `crc32_empty` function exists but is not
  active. `test_runner_user.c` and `test_user_*.c` are not built by the
  Makefile; fork/pipe entries there are commented out as well.
- `vma_intervals` in QEMU is not a page-table test. The host stress test links
  actual VMA code but uses a host allocator and does not test kernel heap,
  direct map, CR3 changes, TLB invalidation, or hardware faults.
- PMM has only a single-frame high-memory lifecycle case. No PMM exhaustion,
  reserved-region enumeration, double-free, invalid-free, contiguous allocation
  stress, or range-overflow suite is active.
- No kernel-wide or process/ELF allocation fault injection is present. The only
  injection is a deterministic failed host allocation during a VMA split.
- No ELF parser fuzz target, syscall/usercopy fuzz target, code coverage
  instrumentation, extended soak, or per-test timeout exists. QEMU has a
  90-second whole-run limit.
- Linux ABI coverage is deliberately a small regression surface; it does not
  show Linux libc/pthread compatibility. Four APs boot under `smap-test`, but
  user tasks run on the BSP; there is no validated SMP scheduler or remote TLB
  shootdown.
