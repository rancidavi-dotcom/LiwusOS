# LiwusOS verification suite

This document describes the tests that are built from this repository and how
to reproduce them. The suite is evidence about named behaviors; a green result
does not prove that the kernel is free of bugs.

## Layers and commands

| Command | What it runs | Typical use |
|---|---|---|
| `make test-harness` | Synthetic serial-log checks for missing/duplicate cases, missing PASS, failure/panic markers, and wrong QEMU status | Fast runner changes |
| `make test-unit` | Harness self-checks plus the actual `src/kernel/mm/vma.c` compiled on the host with ASan/UBSan; 5,000 fixed-seed model operations | Fast development gate |
| `make test-stress` | Same VMA source and reference model for 50,000 operations with seed `0x4c495755` | Explicit host stress |
| `make test` | Host checks, then the 47 top-level kernel cases and the ring-3 integration group in QEMU with 2 GiB RAM | Main regression gate |
| `make ring3-test` | Repeats that same 47-case test image and ring-3 group in QEMU | Ring-3 focused reproduction |
| `make smap-test` | Repeats the same suite with `-cpu max -smp 4 -m 4096`, checking SMAP and AP startup | Protection/SMP bring-up regression |
| `make test-all` | Runs host, standard QEMU, ring-3, and SMAP modes serially | Pre-checkpoint validation |

`make test`, `make ring3-test`, and `make smap-test` use the same test ISO and
runner. They are three environment runs, not three independent counts of the
same test cases. The runner reports 47 top-level cases: 46 kernel-side cases
and one `ring3_processes` integration case. That integration case emits 22
separate `[RING3 PASS]` markers and runs PIE, Linux ABI, Linux clone/futex, and
native C ELF probes. Those subcases are not added to the 47 count.

The non-`KERNEL_TEST` `tests/test_runner_user.c` and `tests/test_user_*.c` are
not part of the active Makefile test image. `test_crc32_empty` is declared but
not called by the active kernel runner. Neither is counted as tested.

## QEMU exit and serial validation

The test kernel emits a QEMU `isa-debug-exit` code only from
`tests/test_runner_kernel.c`, which is compiled into the `KERNEL_TEST` image.
Production `kernel.bin` has no test exit behavior. QEMU exit status `1` means
the runner reached its success exit; test failure, panic, launch failure, or
the 90-second timeout produces a different status and fails the Make target.

`tests/harness/validate_serial.py` checks all of the following before accepting
a run:

- Exactly one runner start/end and one `RESULT: ALL PASS`.
- The ordered kernel case names match `tests/harness/kernel_cases.txt` exactly;
  missing, extra, or duplicate cases fail.
- Every top-level case emits exactly one `PASS` result.
- The 22 ring-3 markers and required PIE/Linux/native integration results each
  appear exactly once.
- Panic, fatal setup, assertion, and failure markers are absent.
- QEMU exited through the expected code; SMAP mode also confirms SMAP and APIC
  ID 3 startup.

The harness self-test builds synthetic logs and intentionally removes a case,
removes a case's PASS, duplicates a case, inserts failure/panic markers, omits a
ring-3 result, and changes the QEMU status. It requires each mutation to be
rejected. Ring-3 marker counting allows unrelated kernel serial output to share
the first line of a marker; it still requires one exact marker occurrence.

Each QEMU run is retained under ignored `test-logs/` with a unique name. The
latest standard run is copied to `test_serial.log`; ring-3 and SMAP logs are
also copied to `/tmp/liwusos-ring3-integration.serial` and
`/tmp/liwusos-smap-integration.serial`. Reproduce a result with the printed
Make target and inspect that run's `.serial` file.

## Host VMA test

`tests/host/test_vma_host.c` links the production `src/kernel/mm/vma.c`
directly. Only the allocator is replaced with a host `malloc/free` shim. The
test checks half-open boundaries, alignment and user limits, overlap rejection,
adjacent coalescing, file-offset preservation across split, heap collision and
adjacent growth, top-down gap selection, clone/destroy, and a deterministic
allocation failure during split. It then compares 5,000 or 50,000 random
insert/remove operations against an independent 256-page occupancy model,
checking list ordering, non-overlap, lookup behavior, and balanced VMA
allocations after each operation. The PRNG seed is fixed and printed on
failure.

This tests the production VMA algorithm, not hardware page tables or the kernel
heap. The allocator fault injection and sanitizers apply only to this host
test; no production allocator behavior is changed.

## Adding or changing a test

1. Give the case a stable unique name and a bounded success/failure condition.
2. Add kernel-side cases to `tests/test_runner_kernel.c` and the same ordered
   name to `tests/harness/kernel_cases.txt`.
3. Add ring-3 pass labels to `tests/harness/ring3_cases.txt` when adding a
   subcase to `tests/ring3/suite.c`.
4. Update `docs/TEST_COVERAGE.md` with the input/property checked, expected
   result, subsystem, and remaining limitations.
5. Run `make test-unit`, the relevant QEMU target, and `git diff --check`.
6. Before a checkpoint, run `make -B kernel.bin` and `make test-all`.

The SDFS tests share one RAM disk formatted at runner startup and execute in a
fixed order. Most use distinct paths; remount, journal, permissions, and
disk-full cases deliberately change shared filesystem state. They are not
isolated per test. The QEMU timeout is suite-wide, not an individual per-case
deadline.

## Current limits

The host property test covers VMA metadata only. There is no direct host unit
suite for PMM, page-table mutation, kernel heap, ELF parsing, or syscall
dispatch. There is no kernel-wide fault injection, coverage instrumentation,
parser fuzzing, long soak, multi-reader/writer pipe stress, many-thread stress,
or actual user-task scheduling on APs. The SMAP run proves AP startup and the
listed tests under SMAP; it does not prove scheduler migration, remote TLB
shootdown, or parallel execution.
