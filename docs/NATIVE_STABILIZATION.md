# Native System Stabilization Report

## A. Initial state

- Branch `lean-kernel`, starting HEAD `24b42e7`.
- Pre-existing local changes were present in `docs/ABI_LINUX.md` and
  `docs/ROADMAP.md`; `docs/PROJECT_HANDOFF.md`, `docs/linux-compat/`, and
  `docs/native-abi/` were untracked. They were preserved and kept out of the
  stabilization checkpoint.
- The pre-existing baseline reported 49 kernel cases and 23 ring-3 markers.

## B. Console diagnosis

The missing `touch` was not lost by COM1. The harness used the one child-exit
line both as the expected output of a no-output command and as its separate
exit-status marker. It consumed that line once, waited for it a second time,
and consequently never sent `touch`.

Controlled diagnostics on that failing run measured 684 host-transmitted bytes,
684 guest UART receives, zero UART overrun indications, 684 stdin reads, and
683 delivered bytes (the last byte was the diagnostic Ctrl-T). The bounded
trace ended at `mkdir /toolbox-test\n`. The diagnostic code and Ctrl-T hook
were removed after collecting the evidence. The harness now represents
no-output as `None`, then waits once for the child status and prompt.

## C. Serial driver and shell

The driver remains the existing polling COM1 implementation: UART interrupts
are disabled, RX is drained by the kernel reader at a 10 ms sleep cadence, and
the UART FIFO is enabled. Interrupt-driven RX was not justified by the
diagnosis. The QEMU harness sends bounded groups of four bytes at 50 ms
intervals; within this tested input pattern there were no missing or repeated
lines. The 100-command sequence used unique numbered `echo` replies and waited
for each cooked-input echo, program output, child status, and next prompt.

The shell integration also covered line overflow draining, unsupported shell
syntax, missing and invalid executables, child return statuses, `exit`, Ctrl-D
and shell restart, and idle CPU use. The passing run measured 0.020 QEMU CPU
seconds during a one-second idle interval (limit 0.75 seconds).

## D. Filesystem defects fixed

SDFS V2 deletion changes an entry type to `DEL` but retains its original
`name_len` and serialized payload. Kernel directory walkers had advanced only
two bytes, then parsed the deleted name and metadata as live entries. This
caused truncated names and omitted files after delete/create sequences. The
walkers now skip the complete V2 record (and use the V1 record size for
read-only V1 media); malformed tombstone lengths stop the scan. A tombstone is
reused only when the new serialized entry is the same size, so no invalid
padding remains in the directory stream.

The offline SDFS installer had the same two-byte tombstone assumption. Its
directory walk, lookup, and append/reuse paths now follow the kernel slot
boundaries. A host regression creates a tombstone, walks a surviving neighbor,
and reuses the exact-size slot for the replacement.

The existing kernel directory-list test now deletes an entry, creates another,
and checks that enumeration returns each live entry exactly once and omits the
deleted name. The real shell integration exercises the same transition.

## E. Native applications and evidence

All named utilities remain separate static ELF64 `.lxe` programs using the
Native SDK and generic shell resolution. The QEMU integration invokes them as
ring-3 children through `fork`/`execve`/`waitpid`; it checks names with and
without `.lxe` where applicable, child status, expected errors, and real file
contents.

| Application | Verified behavior | Remaining gaps |
|---|---|---|
| `ls` | `/`, `/bin`, post-delete/create directory, missing path; getdents-backed records | No broad enumeration soak over large directories |
| `cat` | Text, empty file, binary file, and exact readback through 65536 bytes; missing path and directory errors | No injected read/write device fault during a child |
| `mkdir` | Creates a real directory used by subsequent commands and persistent tests | No QEMU disk-full command case |
| `rm` | Removes a real entry; later listing omits it; ring-3 DAC probes cover denied removal | No crash-point test for the multi-step operation |
| `mv` | Renames a real file and preserves readable contents; kernel tests cover rename and collision | Rename is not crash-atomic |
| `cp` | Exact copy/readback at 0, 1, 15, 16, 255, 256, 4095, 4096, 4097, 8192, and 65536 bytes; binary data and existing-destination refusal | No deterministic QEMU disk-full or process-kill mid-copy test |
| `touch` | Creates an empty file and preserves existing binary content | Timestamp semantics remain intentionally limited |
| `echo` | Arguments, zero arguments, newline, and 100 uniquely numbered replies | No shell redirection or expansion is supported |

## F. VFS, permissions, and persistence

The changes preserve the existing native syscall/VFS interfaces. Ring-3 DAC
probes in `make test-all` continue to test read/write/execute denial, directory
traversal, descriptor access, unlink lifetime, truncate, and permission checks.
The four-boot persistence test passed on one temporary AHCI image: it installed
application A, created and verified native utility files, replaced A with the
larger B image, then verified B and the utility files on boots 3 and 4. The
test also confirmed UID 1000 and modes 0755/0644, and that the offline installer
refuses a disk while QEMU has it open.

The first rerun exposed the installer tombstone bug described above. After its
fix, all seven host installer tests and the four-boot QEMU persistence test
passed. No personal disk image was used.

## G. SDFS transaction and concurrency limits

The existing journal protects whole-file replacement through its copy-on-write
path and one-block metadata commit. It does not make `rename`, in-place
descriptor writes, unlink plus block reclamation, or multi-call `cp` atomic
across power loss. A crash can leave leaked allocation or a missing source
name after rename; those guarantees were not expanded in this milestone.

The metadata write path and open-reference list have locks, but bitmap
allocation and all directory mutation sequences do not share a filesystem-wide
mutation lock. User tasks execute on the BSP in the current scheduler, so the
QEMU tests demonstrate sequential behavior, not concurrent filesystem safety
under SMP. No global lock or scheduler restructuring was added without a
supported concurrent workload and lock-order protocol.

## H. Test results

Final checks after the SDFS kernel change:

- `make -B kernel.bin`: PASS, `-Werror` enabled.
- `make test-all`: PASS — test, ring-3, and SMAP modes; each reported 49 unique
  kernel cases and 23 ring-3 cases.
- `make test-stress`: PASS — 50,000 fixed-seed VMA model steps.
- `make test-unit`: PASS — serial harness self-test, 5,000 VMA model steps,
  and shell parser tests.
- `make shell-test`: PASS; the full QEMU integration passed ten sequential
  times at 50 ms send pacing, followed by an additional `make shell-test` run
  at the default 200 ms pacing. Each run used a temporary disk, 100 numbered
  shell commands, and exact copy/readback checks through 65536 bytes. The
  zero-argument `echo` assertion was added during the repetition series and
  passed in its later runs and the final default-paced run.
- `make app-install-test`: PASS after the host installer fix — seven host
  installer cases and four boots on one reused temporary SDFS image.
- `git diff --check`: to be recorded after the final documentation/checkpoint
  edits.

The serial transcript `test-logs/shell-integration.serial`, persistent boot
transcripts, and test-run logs are ignored generated files; they are local
evidence and are not part of the source commit. The shell command to reproduce
the paced run is:

```sh
LIW_SHELL_SEND_DELAY=0.05 make shell-test
```

## I. Linux personality

No Linux personality code was changed. The `make test-all` ring-3 and Linux
syscall probes passed with the existing manifest.

## J. Git and checkpoint

The implementation is on `lean-kernel`, based on `24b42e7`. A local checkpoint
will include only the SDFS kernel fix, the offline installer fix, their
regressions, the shell harness corrections/stress checks, and this report.
It will not include the pre-existing ABI/roadmap edits or untracked handoff and
compatibility documents. No push is planned.

## K. Completion state

**MILESTONE PARCIAL.** The interactive utility workflow, the console harness,
the discovered SDFS directory corruption, and four-boot persistence all have
reproducible passing evidence. Full completion is not claimed because rename
and multi-call operations lack crash-atomic guarantees, and concurrent SDFS
mutation is not proven safe for SMP. Those limits are documented and remain
follow-up work.
