# ELF64 `execve` integration

Run the QEMU integration suite with:

```sh
make ring3-test
```

The test initrd contains `ring3_suite`, a second valid ELF64 (`exec_target`),
and a deliberately invalid executable (`invalid_elf`). The tests exercise
`fork` → `execve` → `waitpid`, check the new image's `argc`, `argv`, and
`envp`, and verify the child PID remains waitable under the original PID.
They also check invalid ELF handling, invalid filename/`argv`/`envp` pointers,
continued execution after failures, and twelve repeated image replacements.
The runner compares free PMM memory before and after the user-process suite.

## Implementation notes

- `execve` resolves SDFS paths first and falls back to the existing initrd
  lookup. It does not add a filesystem layer.
- It copies bounded argument and environment vectors into kernel memory while
  the old address space is active, then loads the ELF and constructs its
  stack in a new page directory.
- A load or stack allocation error switches back to the old page directory,
  frees the incomplete new one, and returns an error. On success it updates
  the current task's image state and saved `iretq` frame, then frees the old
  address space. PID, parent, file descriptors, and task identity remain on
  the existing task object.
- The heap arena is now claimed from a contiguous run of firmware-available
  pages. The PMM reserves that run, framebuffer pages are reserved explicitly,
  and returning a heap object to its free list no longer makes heap pages
  allocatable as physical frames.

## Results and limits

The final QEMU run passed the ring-3 checks and the existing kernel runner
reported `42/42 PASS`, `RESULT: ALL PASS`, and `LIWUS_BOOT_READY`. The serial
log also showed twelve successful `exec_target` launches and no decline in
free PMM memory across the ring-3 run (the observed free count increased by
64 KiB). The log is written to
`/tmp/liwusos-ring3-integration.serial` by `make ring3-test`.

The PMM reports allocation failure and `execve` cleans up that path, but this
workspace has no deterministic allocation-failure injector. A forced memory
exhaustion run was therefore not part of the QEMU tests. ELF32 `execve`, real
hardware, SMP, COW, and demand paging remain unverified.
