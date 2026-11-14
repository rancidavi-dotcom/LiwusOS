# PROJECT_HANDOFF.md - Central Entry Point

## Identity and objectives
LiwusOS is an independent, bare-metal x86_64 OS with its own kernel, architecture, memory management, scheduler, and native ABI. Linux x86-64 binary compatibility is an optional compatibility layer (paused for expansion). Native ABI is the primary interface.

## Current state
- **Branch:** `lean-kernel`
- **Last reviewed commit:** `2b96f7d` - "feat(hardware): suporte completo a teclado USB EHCI em hardware real e aceleracao grafica"
- **Build:** Clean with `-Werror`
- **Tests:** active manifests contain 51 top-level cases and 23 ring-3 markers; the hardware checkpoint preserved the existing automated USB/QEMU regressions
- **Environment:** QEMU with 4 CPUs, SMAP active
- **Physical evidence:** an Intel H61/PCH machine enumerates its integrated
  `8087:0024` Rate Matching Hub through EHCI and accepts USB-keyboard input in
  the ring-3 shell. See `docs/HARDWARE_PC_H61.md`.

## Directory structure (key)
```
src/boot/        Boot, long mode
src/kernel/      Core kernel (arch, core/syscall, mm, sched, terminal)
src/compat/linux/ Linux personality (syscall translation)
src/fs/           VFS, SDFS
src/drivers/      Drivers
include/uapi/     Native UAPI (syscall.h, syscall_nums.h)
include/kernel/   Kernel internals
programs/         Userspace programs
tests/            Tests
docs/             Documentation
```

## Architecture
- **Memory:** Higher-half kernel, direct map, VMAs per address space, NXE, SMEP/SMAP
- **Processes/threads:** `process_t` = resources, `thread_t` = execution. Scheduler uses threads. Wait queues, pipes with blocking.
- **Native ABI:** `int 0x80`, numbers in `include/uapi/syscall_nums.h`, documented in `docs/ABI.md`
- **Linux personality:** Isolated in `src/compat/linux/`, uses `SYSCALL` entry, ABI selected per task, returns `-errno`

## Tests and reproduction
- `make -B kernel.bin`
- `make test`, `make ring3-test`, `make smap-test`
- See `docs/linux-compat/REPRODUCIBILITY.md`

## Decisions that cannot be violated
1. Independent OS - not Linux fork/distribution
2. Linux personality is optional compatibility layer only
3. Native Liwus ABI is primary
4. Don't remove/disable existing Linux functionality
5. Reuse shared core, don't duplicate
6. Translate at boundary only
7. Preserve tests, keep `-Werror`
8. Process/thread separation is architectural
9. No generated binaries in git

## Incomplete functionality
- Linux: PT_INTERP/dynamic linking, broad syscalls, signals, full wait4, futex extensions, robust lists, general clone/pthreads, demand paging/COW/file-backed mmap, vDSO/auxv completeness, SMP thread migration/TLB shootdown validation, AVX-512/AMX and complete CPU-feature policy. Current external evidence covers static musl caller/default stacks, four pthreads with mutex/condvar/name, and a static glibc x86-64-v3 greeting on QEMU `-cpu max`; this remains partial compatibility.
- Native: Shell command composition is still minimal: no quotes, pipelines,
  redirection, scripts, background jobs or job control. USB mass storage and a
  generic block-device layer are also absent.

## Known bugs/risks
- Native ABI has mixed error return conventions (-1 vs -errno)
- a static glibc `puts` smoke now passes with x87/SSE/AVX XSAVE state; dynamic glibc, broader libc coverage, AVX-512/AMX remain unverified
- Some Linux features intentionally unimplemented

## Next priority (native)
Implement Native Shell v0.2 command composition: quoting/escaping, bounded
pipelines and `<`, `>`, `>>`, `2>` redirection. Reuse the existing native
`pipe`, `dup2`, `fork`, `execve`, `close` and `waitpid` mechanisms. Keep
background jobs and full job control deferred until signals and process-group
semantics exist. See `docs/NATIVE_SHELL.md` and
`docs/native-abi/DEVELOPMENT_PLAN.md`.

## How to resume Linux compatibility
**Read first:** `docs/linux-compat/HANDOFF.md` → `docs/linux-compat/IMPLEMENTATION_STATUS.md` → `docs/linux-compat/RESUME_PLAN.md` → `docs/linux-compat/KNOWN_ISSUES.md` → `docs/linux-compat/REPRODUCIBILITY.md` → `docs/ABI_LINUX.md` → `docs/ROADMAP.md`

**Verify:** `git log -1 --oneline`, build kernel, run all three tests - all must pass.

**Rules:** Stay in frozen scope (no new Linux features), update docs on changes, preserve separation, no regressions.

## INSTRUÇÕES PARA O PRÓXIMO AGENTE
- **Concluído:** Linux compatibility paused but fully preserved/documented; native ABI audited/documented; project handoff created.
- **Pausado:** Linux expansion (new syscalls/features). All existing Linux code/tests remain active.
- **Próximo milestone recomendado:** Native Shell v0.2 sobre o SDK e a ABI nativa já utilizáveis.
- **Não modificar sem investigação:** `src/compat/linux/*`, Linux tests/probes, core shared mechanisms if changing for Linux reasons. Native ABI numbers/contracts - changes need justification.
- **Ler primeiro:** This file, then native-abi docs and linux-compat docs as needed.
- **Próximo objetivo concreto:** Add quotes/escapes, bounded pipelines and file
  redirection to `liwsh`, plus stdin support in stream-oriented utilities such
  as `cat`/`head`. Prove descriptor closure, failure recovery and repeated
  pipelines in host parser tests and the QEMU shell integration.
- **Verificar ambiente:** Build with `-Werror`, run `make test`, `make ring3-test`, `make smap-test`. All must stay green.

## Verification project update (2026-11-02)

This update supersedes the test counts and broad Linux compatibility claims
above. At the start of the verification work, `lean-kernel` was at `6afc34f`.
The starting inventory was 47 top-level cases. At this historical checkpoint,
the inventory was 49 (48 kernel functions plus the `ring3_processes`
aggregator), with 23 nested
ring-3 markers and separate PIE/Linux/native integration result markers.
Repeating the image under three QEMU configurations does not increase those
counts.

The original Make targets could mask QEMU failures and accepted a generic
`ALL PASS` string without checking which cases ran. The verification changes
add an exact ordered manifest, serial-log validator with mutation self-checks,
test-only QEMU debug exit, and a host ASan/UBSan test that links production VMA
code and compares 5,000 deterministic operations against a reference model.
An opt-in `make test-stress` runs 50,000 operations. See `docs/TESTING.md` and
`docs/TEST_COVERAGE.md` for reproducible commands, case details, and gaps.

`pmm_contract` now checks allocation, invalid/double frees, direct-map access,
and contiguous claims; `kheap_contract` checks reuse, `krealloc` content and
alignment behavior using the actual kernel heap in QEMU.

Local checkpoint commits: `b7c8a52`, `a93cfca`, and `bcbe0a9`
(`test: cover kernel heap allocation contracts`). None was pushed.
Final checks passed: `make -B kernel.bin`, `make test-all` (host self-check +
standard/ring3/SMAP QEMU runs, each validating 49 top-level and 23 ring-3
cases), `make test-stress` (50,000 VMA model operations), and `git diff --check`.
Evidence remains limited: there is no
direct PMM, page-table API, or kernel heap unit suite; no kernel-wide allocation
fault injection, parser fuzzing, long soak, per-case timeout, SMP task
migration, or remote TLB shootdown validation. Do not treat these tests as a
security proof or as evidence of broad Linux compatibility. The next testing
priority is targeted PMM/VMM/heap negative and rollback coverage, followed by
process/thread and pipe/futex concurrency stress, before deciding whether the
verification base is sufficient for SDK or `.lxe` work.

## Milestone 1 update (native execution)
- **Native ELF executed:** `programs/native-hello/native-hello.elf` runs in ring 3 via normal ELF loader, uses `crt0` (`lib/liwus/crt0.S`) calling `main(argc,argv,envp)` from `lib/liwus/crt.c`, uses public wrappers in `include/liwus/syscall.h`/`lib/liwus/syscall.c` with native `int $0x80`. Prints "Hello from LIWUS OS!" via `liw_write(1,...)` and exits with `liw_exit`/`liw_crt_start` calling `liw_exit(main(...))`. Tested in `make test`/`make ring3-test` as `native_hello` with exit status 0.
- **Build flow:** `make -C lib/liwus` and `make -C programs/native-hello` produce native ELF; Makefile integrates `native_hello` into test initrd and `tests/test_runner_ring3.c` validates execution/exit.
- **Linux personality:** Still paused but fully functional; all Linux probes pass.

## Native SDK v0.1 and `.lxe` checkpoint (2026-11-02)

The current SDK package is built with `make sdk` into ignored
`build/liwus-sdk/`: public `include/liwus` headers, `libliwus.a`, `crt0.o`, the
ELF64 linker script, and `bin/native-hello.lxe`. `.lxe` is only the official
suffix for static LIWUS ELF64 `ET_EXEC`; the kernel still identifies and loads
the file as ELF and has no new binary format or extension-specific loader.
`make all` installs the sample at `/bin/native-hello.lxe` in the initrd.

The QEMU ring-3 regression executes `/bin/native-hello.lxe` by native
`execve`, verifies CRT `argc`/`argv`/`envp`, and exercises SDK syscall wrappers
for `getpid`, `gettimeofday`, `getcwd`, `stat`/`fstat`, file open/read/write/
lseek/close, mkdir/chdir/rename/unlink/rmdir, anonymous mmap/munmap, pipes,
dup/dup2, and `write_all`. It also checks SDK fork/execve/waitpid with a second
`.lxe` child, and verifies VFS/file-description/pipe cleanup counters. The
kernel integration runner also launches the `.lxe` artifact directly.

The package contains public headers, `libliwus.a`, `crt0.o`, an ELF64 linker
script, and `bin/native-hello.lxe`. `readelf` confirmed the sample is static
x86-64 ELF64 `ET_EXEC`, with no `PT_INTERP`; `.lxe` remains a suffix and adds no
loader or binary-format behavior. A separate consumer was compiled using only
the installed package paths. The verification inventory is 49 top-level
kernel cases and 23 ring-3 markers.

Checkpoint commit: `f4d8b14` (`feat: package native SDK v0.1 and lxe ELF apps`)
on `lean-kernel`; local only, not pushed. `make test-all` passed the standard,
ring-3, and SMAP QEMU modes, each validating all 49 top-level cases and 23
ring-3 markers. Existing VMM, fault-injection, and concurrency gaps remain
documented for later milestones. This SDK checkpoint supersedes the earlier
verification section's proposal to finish those audits before SDK work.

## External SDK and Native Shell checkpoint (complete)

`make sdk-install SDK_PREFIX=...` installs the public headers, CRT, archive,
and linker script into a caller-selected prefix. The external SDK validation
script generates and compiles its C application in a temporary directory
outside the checkout, checks the ELF and undefined symbols, and the test image
executes it in ring 3 with the expected output and exit status 23.

`programs/liwsh/` is a small SDK-linked ring-3 shell. It reads stdin, parses
bounded whitespace-separated argv, handles `help`, `version`, `exit`, and
executes children with fork/execve/waitpid. `make shell-test` injects multiple
commands via QEMU COM1, including successful and failed applications, status
recovery, and Ctrl-D/exit restart. The parser has ASan/UBSan host cases. The
kernel's existing COM1/keyboard polling read now sleeps for one 10 ms timer
interval between polls. No TTY or serial interrupt interface was added.

Verification passed: `make -B kernel.bin`, `make test-all` (standard, ring-3,
and SMAP; each 49/49 and 23/23), `make test-stress` (50,000 VMA model
operations), `make shell-test` via QEMU COM1, and `git diff --check`. The idle
measurement was 0.050 QEMU CPU seconds during one second at the prompt. The
shell transcript is `test-logs/shell-integration.serial`; final QEMU logs were
`test-logs/test-opwOlO.serial`, `test-logs/ring3-dezzD6.serial`, and
`test-logs/smap-6PqpKO.serial`.

Checkpoint commits: `3f3d38e` (`feat: validate external SDK and add native
shell v0.1`) and `0af11d1` (`test: verify shell idle CPU usage`) on
`lean-kernel`; local only, not pushed. The original 49 kernel cases and 23
ring-3 markers remain the baseline; the external app is an additional
validated integration result, not an artificial manifest-count change.

## Native application installation v0.1 (validated)

Current implementation adds `tools/liwus_sdfs_install.py` and
`make app-install APP=... DISK=... [REPLACE=1]`. It is a host-side offline
installer for existing SDFS V2 raw images. It checks static x86-64 ELF64
`ET_EXEC`, segment bounds, entrypoint, no `PT_INTERP`/`PT_DYNAMIC`, and W^X;
validates SDFS metadata, bitmap ownership, and file chains; stages the complete
image in a neighboring temporary file; then publishes with atomic filesystem
rename. It rejects images found open by other processes and takes a nonblocking
file lock. It requires `/bin` to exist and refuses an existing target unless
replacement is explicit. This is not an in-guest package manager.

The first integration run exposed a kernel persistence defect: the first-boot
path flushed the bitmap before writing `/.system_installed`. The marker's data
block could then be referenced by a directory entry while its bitmap bit was
still free. `src/kernel/core/kernel.c` now creates/writes the marker before
flushing the bitmap and disk cache; the ramdisk path also flushes after marker
creation. The installer intentionally fails closed on such inconsistent
images; it does not repair them.

`make app-install-test` runs host image/ELF tests plus a QEMU test that uses one
temporary image across four boots, verifies the program is absent before
installation, checks refusal while QEMU holds the disk open, installs and runs
version A, replaces it with a larger version B, and runs B after two more
boots. The flow passed. `make -B kernel.bin`, `make test-all` (49 kernel cases and
23 ring-3 markers in standard, ring3, and SMAP runs), `make test-stress`
(50,000 VMA operations), `make test-unit`, `make shell-test`,
`make app-install-test` (five host installer cases plus four-boot QEMU flow),
`python3 -m py_compile ...`, and `git diff --check` all passed. Serial evidence is
in ignored `test-logs/persistent-boot-{1,2,3,4}.serial` and
`test-logs/persistent-install.summary`. Checkpoint commits: `ad5b98e` (`feat: install native apps into persistent SDFS images`) and `2f79d53` (`fix: use native SDFS application permissions`), local only, not pushed. Preserve the preexisting local changes in `docs/ABI_LINUX.md`,
`docs/ROADMAP.md`, and the local documentation directories when checkpointing.

## SDFS transaction and fault-injection milestone (complete)

The SDFS V2 journal was previously not a true WAL: staging writes marked each
entry committed immediately, metadata writes ignored journal errors, replay
could skip a bad payload and clear the log, and the QEMU AHCI path had no
FLUSH CACHE command. The current work changes the journal to stage entry data
and headers first, publish a checksummed dirty header as the commit record,
apply target blocks, flush them, then clean the journal. Replay validates all
entries and target ranges and refuses a corrupt committed transaction. Boot
no longer formats a recognized damaged SDFS; it falls back to RAM storage.

`sdfs_write_file()` now builds and flushes a new chain, persists allocation
bits, journals one directory block to switch the entry, and frees old blocks
only after commit. Returned I/O failures can leak blocks when commit state is
uncertain, by design to avoid freeing data a recovered entry might reference.
`delete` now unlinks before freeing. `make test` passed 49 kernel cases and 23
ring-3 markers after the transaction tests were added inside existing
`sdfs_overwrite` and `journal_multiple_entries` cases. Tests inject failure at
pre-commit, target write, and corrupt journal payload; they remount and verify
old/new complete contents or fail-closed recovery. `make app-install-test` has
also exercised firstboot/install/update/reboot on a real temporary AHCI image
with FLUSH CACHE EXT.

Verification passed on the final transaction implementation: `make -B
kernel.bin`, `make test-all` (standard, ring-3, and SMAP each validating 49
kernel cases and 23 ring-3 markers), `make test-stress` (50,000 VMA model
operations), `make test-unit`, `make shell-test`, `make app-install-test` (host
installer cases plus four-boot persistent AHCI flow), and `git diff --check`.
The SDFS fault cases run within the existing journal and overwrite test cases;
the manifest count remains 49. Logs are ignored under `test-logs/`.

Checkpoint commit: `aaad05f` (`fix: make SDFS replacement journal recoverable`)
on `lean-kernel`, local only, not pushed. Keep the preexisting local changes in
`docs/ABI_LINUX.md`, `docs/ROADMAP.md`, and the local documentation directories
separate. This personal handoff file remains untracked.

## Native filesystem utilities milestone (complete)

Starting state confirmed at `aaad05f` on `lean-kernel`; the personal
documentation changes above were preserved. Existing syscall 30 (`getdents`)
and VFS `readdir` callbacks made a new syscall unnecessary. The implementation
adds a public `liw_dirent` record and `liw_getdents` wrapper, strengthens
descriptor/user-buffer/EOF/short-buffer behavior, builds independent `ls.lxe`
and `cat.lxe`, and adds generic exact-first `.lxe` candidate lookup to `liwsh`.
The initrd seeds `/readme.txt` and skips existing names; `sdfs-put` explicitly
updates one regular file in an offline SDFS image.

Final verification passed: `make -B kernel.bin`, `make test-all` (49 kernel
cases and 23 ring-3 markers in standard, ring-3, and SMAP), `make test-stress`
(50,000 VMA model steps), `make test-unit`, `make shell-test`,
`make app-install-test` (six host installer cases plus four-boot persistent
application install/update), and `git diff --check`. The console test covers
all requested command forms, actual `/bin` enumeration, `/readme.txt`, empty
and 768-byte binary files, generic `demo`, exact-file precedence, invalid and
long names, and shell recovery. The persistence flow verified first-boot
seeding and explicit offline app updates. Test manifests remain 49/23.

This checkpoint originally treated SDFS permission bits as metadata only. The
later Native System Utilities & Security milestone superseded that limitation:
native path traversal, open, mutation, directory enumeration and `execve` now
enforce the stored owner/group/other bits for the current single-user model.

Checkpoint commits: `8594303` (`feat: expose native VFS directory
enumeration`), `1543401` (`feat: add native ls cat and lxe command lookup`),
and `7c3c638` (`docs: record native file permission limits`) on `lean-kernel`;
local only, not pushed. The initial media seed does not
replace existing paths. `sdfs-put` explicitly updates regular data files in
an offline image. This personal handoff and the preexisting Linux/native docs
remain untracked or locally modified and were not included in either commit.
