<h1 align="center">
  <img src="assets/LiwusOSlogo.png" alt="LiwusOS logo" width="128" />
  <br />
  LiwusOS
</h1>

<p align="center">
  <strong>A small x86_64 operating system with its own kernel, filesystem and userspace.</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/architecture-x86__64-blue" alt="x86_64" />
  <img src="https://img.shields.io/badge/language-C%20%2F%20Assembly-green" alt="C / Assembly" />
  <img src="https://img.shields.io/badge/status-alpha-orange" alt="alpha" />
  <img src="https://img.shields.io/badge/license-MIT-red" alt="MIT" />
</p>

---

## What this is

LiwusOS is an experimental, self-contained operating system for x86_64. It boots
from Multiboot2/GRUB, runs its own kernel in long mode, and executes ring-3
programs loaded from its own on-disk filesystem.

The current tree is deliberately **lean and text-mode only**. A previous version
had an in-kernel GUI, SMP bring-up, networking and audio; it was retired because
it could not be maintained or tested. It is preserved as the `legacy-gui` tag and
is **not** part of the build.

The project is now focused on making the existing kernel solid before adding a
Linux ABI compatibility layer. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Implemented

| Area | State |
|---|---|
| Boot | Multiboot2 + GRUB, own 32→64-bit transition, identity map |
| CPU | GDT, IDT, ISR/IRQ, PIC, LAPIC, PIT at 100 Hz, RTC |
| Memory | Bitmap PMM, 4-level paging, kernel heap (1 GiB physical cap) |
| Processes | Preemptive scheduler, `fork`, `execve`, `waitpid`, `kill`, jobs |
| Syscalls | 41 calls through `int $0x80` (see [docs/ABI.md](docs/ABI.md)) |
| Userspace | Ring 3 with validated user/kernel copies |
| Binaries | Static ELF32 and ELF64 (`PT_LOAD` only) |
| Filesystem | VFS + SDFS on ATA/AHCI disks: journal, CRC32, permissions, rename |
| Initrd | tar archive installed into SDFS on first boot |
| Drivers | ACPI, PCI, AHCI, ATA (PIO and bus-master DMA), PS/2 keyboard and mouse, serial, VGA text |
| Shell | Ring-3 shell with pipes, redirection and background jobs |
| Tests | 42 in-kernel tests + ring-3 isolation/exec regression suite |

## Not implemented (yet)

GUI, GPU acceleration, networking, audio, USB, SMP scheduling, threads/TLS,
signals, demand paging, COW, file-backed `mmap`, NX/SMEP/SMAP, dynamic linking,
`/proc`, `/dev`, PTYs, and the Linux ABI itself. The roadmap explains the order
in which these are planned.

## Repository layout

```
src/boot/        Multiboot header, long-mode entry, interrupt stubs
src/kernel/arch/ GDT, IDT, ISR, APIC, PIT, RTC, context switch
src/kernel/core/ kernel init, syscalls, ELF loader
src/kernel/mm/   PMM, VMM, kernel heap, user/kernel copy
src/kernel/sched/scheduler, fork, wait, exit
src/kernel/terminal/ in-kernel text terminal and built-in commands
src/fs/          VFS, SDFS, journal, CRC32, initrd
src/drivers/     ACPI, PCI, AHCI, ATA, keyboard, mouse, serial, VGA
programs/        ring-3 init, shell, editor and C example
tests/           in-kernel tests and the ring-3 integration suite
docs/            ABI, roadmap and memory model
```

## Building

Requirements: `gcc`, `binutils`, `make`, `grub-mkrescue`, `xorriso`, `mtools`
and `qemu-system-x86_64` for running.

```sh
make            # kernel.bin + liwusos.iso
make run        # boot the ISO in QEMU
```

The build is warning-free and uses `-Werror`.

## Tests

```sh
make test        # 42 in-kernel tests (SDFS, terminal, scheduler)
make ring3-test  # fork/exec/pipes/mmap/usercopy/ELF isolation checks
```

Both boot the test ISO under QEMU and fail on a non-zero result. The serial log
is written to `test_serial.log`.

## Documentation

- [docs/ROADMAP.md](docs/ROADMAP.md) — the 58 steps and gates towards the Linux ABI
- [docs/ABI_LINUX.md](docs/ABI_LINUX.md) — compatibility architecture decisions
- [docs/MM.md](docs/MM.md) — target memory model (higher-half, direct map, VMAs)
- [docs/ABI.md](docs/ABI.md) — the native syscall ABI as it exists today

## Status and scope

This is a hobby/educational kernel. It is not a Linux distribution, does not
aim to be POSIX-complete today, and should not be used for anything that needs
security or data integrity guarantees. The 1 GiB physical cap and the 3 GiB user
address space are known limitations that Gate 1 of the roadmap removes.

## License

MIT. See [LICENSE](LICENSE).
