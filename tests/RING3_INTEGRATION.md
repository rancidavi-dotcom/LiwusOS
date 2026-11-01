# Ring-3 integration tests

Run the integration suite with:

```sh
make ring3-test
```

The target builds a small freestanding ELF64 at `0x400000`, adds it to the
test initrd, boots the existing `KERNEL_TEST` image in QEMU without attaching
the workspace disk image, and captures serial output at
`/tmp/liwusos-ring3-integration.serial`.

The user executables check `int 0x80` calls (`getpid`, timer, write, and
`execve`), `brk`, anonymous `mmap`/`munmap`, `fork`/`waitpid`, private memory
after fork, ELF64 image replacement, `argc`/`argv`/`envp`, invalid executable
and user-pointer handling, twelve repeated execs, normal exit status, writes
to a read-only ELF segment, and reads from a supervisor mapping. They verify
that failed execs preserve the old image and protection faults terminate only
the child. See [EXECVE_INTEGRATION.md](EXECVE_INTEGRATION.md) for details.

## Results

Verified in QEMU on 2026-10-25:

- Baseline, before functional changes: the framebuffer configuration entered
  repeated page faults while writing the framebuffer. With `-vga none`, boot
  reached `LIWUS_BOOT_READY`, then faulted on the APIC mapping.
- The heap/PMM overlap fix restored boot with and without the framebuffer;
  both runs reached `LIWUS_BOOT_READY` without a page fault.
- The first integration run exposed an uninitialized kernel-stack canary for
  user tasks and a parent-wakeup state that the scheduler never selected.
  Both paths were corrected, including child insertion in `fork`.
- Final serial run reported all ring-3 checks as `PASS`,
  `[RING3 RESULT] ALL PASS`, kernel-observed exit status 0, and
  `RESULT: 42/42 PASS` for the full kernel test runner.
- The kernel printed `LIWUS_BOOT_READY` after the test runner completed.

The suite does not cover 32-bit ELF execution, real hardware, SMP, COW, or
demand paging. Those paths were not part of this integration target.
