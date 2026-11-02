# Native utilities and security milestone handoff

This is a versioned handoff for the Native System Utilities & Security v0.1
milestone. The pre-existing untracked `docs/PROJECT_HANDOFF.md` was preserved.

## Starting point

- Branch `lean-kernel`, initial HEAD `7c3c638`.
- Working tree already contained local edits in `docs/ABI_LINUX.md` and
  `docs/ROADMAP.md`, plus untracked handoff/compatibility documents. They were
  not overwritten or included in the implementation changes.
- Baseline declared in the task: 49 kernel cases and 23 nested ring-3 markers.

## Scheduler investigation

The first standard run passed; a bounded sequence of nine QEMU standard runs
had one failure in the Linux clone/TLS probe. The relevant failure was in the
probe's futex race assumptions: it required `FUTEX_WAKE` to return exactly one
and the waiter to return zero even when the parent published the futex value
before the child queued. A legal execution is wake count zero followed by
`-EAGAIN`. The wait-queue test also woke its round gate before proving all
workers had queued. The probes now accept the valid futex outcomes and wait
until all workers are queued before gate wakeup. Twenty bounded post-fix QEMU
runs passed. No scheduler source change was needed; the reproduced defect was
in test protocol assumptions, not evidence of kernel memory corruption.

## Implemented

- Standardized new SDFS permission metadata on POSIX 9-bit mode layout, with
  compatibility reads for the two prior private encodings.
- Assigned uid/gid 1000 to ring-3 LIWUS tasks; uid 0 remains kernel-only.
- Enforced directory search, file open access, directory enumeration,
  parent-directory mutation, and executable access. `stat` reports stored
  metadata. Descriptor access rights are recorded and checked by read/write.
- Added generic VFS callbacks for create-as, mkdir, unlink, rmdir, and rename.
- Deferred freeing an unlinked SDFS file/directory while an open description
  retains its inode; final close releases orphaned blocks.
- Added SDK applications `mkdir`, `rm`, `mv`, `cp`, `touch`, and `echo`, all
  linked as ordinary ELF64 `.lxe` files.
- Added ring-3 DAC integration fixtures and shell/persistence utility tests.
- Updated native ABI, SDK, shell, test coverage, and SDFS limitation docs.

## Confirmed so far

- `make -B kernel.bin`: passed with `-Werror` and no warnings.
- `make test-all`: passed. Its `test`, `ring3`, and `smap` QEMU runs each
  reported 49/49 kernel cases and 23 nested ring-3 markers.
  Latest serial logs: `test-logs/test-PorAbn.serial`,
  `test-logs/ring3-DwGGdP.serial`, and `test-logs/smap-5A3EGa.serial`.
- `make test-stress`: passed 50,000 VMA model operations (seed `0x4c495755`);
  allocation/free counts matched.
- `make test-unit`: passed the harness self-test, 5,000 VMA model operations,
  and Native Shell parser cases.
- `make app-install-test`: passed installer host tests and four boots using the
  same temporary SDFS image. The shell-launched app A was updated to a larger
  app B; utility-created files and directory uid/modes persisted. The
  installer refused the image while QEMU held it open.
- A reduced fresh-QEMU shell smoke sequence passed `mkdir`, `touch`, `echo`,
  `cp`, `mv`, `rm`, `ls`, and `cat` as individual `.lxe` programs against an
  AHCI-backed temporary SDFS disk. This was a one-off smoke run, not a
  replacement for `make shell-test`.
- The same test exposed a mode-bit mapping bug (`VFS_ACCESS_READ`/`WRITE` were
  compared directly with POSIX mode bits); the conversion was fixed and the
  full ring-3 target passed afterward.
- A probe also showed that the SDFS disk-full stress case leaves only a small
  number of blocks free. It now runs after ring-3 integration in the shared
  test runner so later process tests have usable storage.
- Scheduler investigation: one of nine initial bounded standard QEMU runs
  exposed a probe/wait-queue ordering assumption; after the probe and gate
  synchronization fix, 20 bounded runs passed. No kernel scheduler change was
  made, because the observed failure was legal futex behavior plus a test
  gate released before all waiters queued.
- `make shell-test` has **not passed**. After successfully running `mkdir
  /toolbox-test`, the full serial integration waits for the next command's
  child exit marker; the guest transcript shows the prompt but no echoed input
  from `touch /toolbox-test/empty`. Repeatedly pacing input and switching the
  serial transport from QEMU stdio to a loopback TCP socket did not fix the
  full sequence. Reduced fresh-QEMU runs and the four-boot persistence test
  did run `mkdir` followed by `touch` successfully. The cause of the
  long-sequence failure is unresolved and it must not be treated as passed.

## Remaining validation and limitations

The required implementation targets passed except for the unresolved full
shell integration timeout. Run final checks after any further fixes:

```sh
make -B kernel.bin
make test-all
make test-stress
make test-unit
make shell-test
make app-install-test
git diff --check
```

Do not claim the milestone complete until `make shell-test` also passes and
the reproducible logs are recorded. The persistence suite exercises `mkdir`,
`cp`, `mv`, `touch`, and `echo`; the reduced shell smoke run exercised `rm`,
but the full `make shell-test` target remains unpassed. Review SDFS behavior
against [`NATIVE_UTILITIES_SECURITY.md`](NATIVE_UTILITIES_SECURITY.md); in
particular, rename, descriptor writes, and multi-step `cp` are not crash-atomic.
