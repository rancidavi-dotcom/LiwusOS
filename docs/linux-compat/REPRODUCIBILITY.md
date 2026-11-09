# Linux Compatibility - Reproducibility

## Build dependencies
- gcc (x86_64 cross or native capable of freestanding x86_64)
- binutils
- make
- grub-mkrescue, xorriso, mtools
- qemu-system-x86_64

## Toolchain
Uses default host toolchain as configured in Makefile. Build is freestanding for kernel, ring3 tests compiled separately.

## Build commands
```sh
make            # kernel.bin + liwusos.iso
make -B kernel.bin
```

## Test commands
```sh
make test        # 51 top-level cases + ring3 tests
make ring3-test  # ring3 integration suite including Linux probes
make smap-test   # SMP + SMAP test (4 CPUs, 4GB)
```

All must pass with `-Werror`.

## QEMU parameters (from Makefile)
- `make run`: standard run
- `make test`: `-machine q35 -m 2048 -display none -monitor none -serial file:test_serial.log -no-reboot -drive file=liwus_disk.img,format=raw,if=none,id=disk -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -cdrom liwusos-test.iso`
- `make smap-test`: similar with `-cpu max -smp 4 -m 4096`

## Test environment
- Disk image: `liwus_disk.img` (AHCI)
- Initrd/test ISO contains ring3 programs including `linux_syscall_probe.elf`, `linux_clone_probe.elf`
- Tests run in QEMU, results checked via serial output

## Linux probes
- `linux_syscall_probe.elf`: ET_EXEC freestanding, uses Linux x86-64 syscall convention (syscall instruction). Tests getpid(39), fstat(5)/stat layout, openat(257) with AT_FDCWD/-ENOENT, invalid syscall -ENOSYS, arch_prctl FS get/set, FS:0 access, mmap(9) with args in R10/MAP_PRIVATE|MAP_ANON, munmap(11), write(1). See `tests/ring3/linux_syscall_probe.S`.
- `linux_clone_probe.elf`: Tests clone with CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD (+CLONE_SETTLS), shared memory, independent FS TLS, thread exit/exit_group, private futex WAIT/WAKE with various error cases.

## External Linux binaries (optional)
If you have a Linux ELF binary to test:
```sh
make smap-test LINUX_STATIC_ELF=/path/to/binary
```
It will be copied as `linux_glibc_hello` into test image temporarily. Note: glibc static binaries built for modern ISA (requiring AVX/XSAVE) may fail with #UD due to missing XSAVE support - this is a known issue.

## Logs and expected results
- Serial logs written to `test_serial.log` (for test targets)
- Expected: `RESULT: 51/51 PASS`, `RESULT: ALL PASS`, `LIWUS_BOOT_READY`, probe messages like `[RING3 RESULT] Linux x86-64 SYSCALL ... PASS` and `[RING3 RESULT] Linux clone ... PASS`
- smap-test also shows AP initialization messages

## Distinguishing kernel vs environment failures
- Kernel panics/crashes show in serial; timeout means QEMU killed or hung
- Test framework reports PASS/FAIL counts
- Check `test_serial.log` for details
- Ensure QEMU has enough memory (-m 2048/4096)
- If tests hang, check for deadlocks in futex/wait queues

## Notes
- No permanent paths to external binaries are stored
- All tests must run with `-Werror` clean build
