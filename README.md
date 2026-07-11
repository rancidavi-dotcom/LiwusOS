<p align="center">
  <img src="assets/LiwusOSlogo.png" alt="LiwusOS Logo" width="200"/>
</p>

<h1 align="center">LiwusOS</h1>

<p align="center">
  <strong>An experimental x86_64 operating system with a complete own-stack implementation</strong>
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#architecture">Architecture</a> ·
  <a href="#building">Building</a> ·
  <a href="#running">Running</a> ·
  <a href="#sdk--development">SDK</a> ·
  <a href="#roadmap">Roadmap</a>
</p>

---

LiwusOS is a hobby operating system written primarily in C and x86 Assembly that
covers the entire stack — from bootloader to userspace SDK. It boots via GRUB2
(Multiboot2), runs a monolítico higher-half kernel on x86_64, and implements a
compositor with scene graph architecture, networking stack, multiple filesystems,
and POSIX-compatible syscall interface.

The project is written in **Brazilian Portuguese** (comments, commit messages)
with code identifiers in **English**.

> **Status:** Pre-alpha / Experimental. Not intended for production use.
> Primary development target: QEMU.

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
  - [Boot Process](#boot-process)
  - [Kernel](#kernel)
  - [Memory Management](#memory-management)
  - [Process Management](#process-management)
  - [Syscalls](#syscalls)
  - [Filesystems](#filesystems)
  - [Drivers](#drivers)
  - [Networking](#networking)
  - [GUI Compositor (LGX)](#gui-compositor-lgx)
  - [Terminal and Shell](#terminal-and-shell)
- [Applications](#applications)
- [SDK and Development](#sdk--development)
- [Directory Structure](#directory-structure)
- [Building](#building)
  - [Prerequisites](#prerequisites)
  - [Build Commands](#build-commands)
  - [Cross-Compilation Toolchain](#cross-compilation-toolchain)
- [Running](#running)
  - [QEMU](#qemu)
  - [Testing](#testing)
- [Dependencies](#dependencies)
- [Hardware Compatibility](#hardware-compatibility)
- [Roadmap](#roadmap)
- [Project Status](#project-status)
- [Contributing](#contributing)
- [License](#license)
- [Credits](#credits)

---

## Features

**Kernel**
- x86_64 higher-half monolithic kernel (loaded at 16 MB virtual address)
- 4-level paging (PML4 → PDP → PD → PT) with user/kernel separation
- Preemptive round-robin scheduler (100 Hz PIT timer)
- Process fork() and exec() with ELF32/ELF64 support
- 33 system calls via `int $0x80`

**Filesystems**
- Virtual File System (VFS) with mount-point resolution
- SDFS (Simple Disk File System) — custom persistent filesystem
- FAT32 — read support (write experimental)
- Initrd — tar-based initial ramdisk
- DevFS — device filesystem

**Networking**
- RTL8139 Ethernet driver
- ARP, IPv4, ICMP (ping)
- TCP (experimental), UDP, DNS, DHCP, HTTP

**Graphics**
- LGX Compositor — scene graph architecture with infinite canvas
- Camera system with pan/zoom (fixed-point arithmetic)
- Event bus with typed events and priority dispatch
- Widget toolkit: Window, Button, Label, Panel
- Layout engine (VBOX/HBOX with flex alignment)
- Animation engine (linear tweening)
- Theme engine (dark glassmorphism palette)
- Input tool system (Pan, Select, Move)

**Drivers**
- ATA PIO, ATA BM-IDE DMA, AHCI/SATA
- PCI bus enumeration
- PS/2 keyboard (ABNT2, scancode set 1) and mouse
- USB subsystem (UHCI, EHCI, HID)
- VGA text mode + linear framebuffer
- BGA/VBE GPU (Bochs Graphics Adaptor)
- EDID monitor detection
- PC Speaker audio (melodies)
- COM1 serial (debug output)

**Userspace**
- newlib-based C standard library
- Full POSIX-compatible syscall bridge (libgloss)
- Cross-compilation toolchain: binutils 2.42, GCC 14.1.0, newlib 4.4.0
- Third-party libraries: zlib, libpng, libjpeg, Lua 5.4.6
- LIW package format for app distribution

**Applications**
- Doom (doomgeneric port) with Freedoom data
- Lua 5.4 interpreter
- TCC (Tiny C Compiler) — compile C inside LiwusOS
- Image viewer (PNG/JPEG)
- Calculator (graphical, using Scene Graph SDK)
- Kilo text editor
- GUI terminal emulator
- System settings panel
- File explorer

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                  GRUB2 (Multiboot2 Bootloader)                │
│            kernel.bin + initrd.tar + font.psf                 │
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────┐
│              boot.s — Assembly Entry Point                    │
│     PML4 → PAE → Long Mode → kernel_main(magic, mbi)        │
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────┐
│                     Kernel (Ring 0)                           │
│                                                              │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐   │
│  │   GDT    │ │   IDT    │ │   PMM    │ │     VMM      │   │
│  │ 9 entries│ │ 256 intr │ │ Bitmap   │ │ 4-level pg   │   │
│  │ + TSS    │ │ PIC remap│ │ 4KB pages│ │ PML4→PDP→PD  │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────┘   │
│                                                              │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐   │
│  │  kheap   │ │  Timer   │ │  Tasks   │ │   Syscalls   │   │
│  │ free-list│ │ PIT 100Hz│ │ round-   │ │ int $0x80    │   │
│  │ kmalloc  │ │          │ │ robin    │ │ 33 calls     │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                     Drivers                           │   │
│  │  ATA │ AHCI │ PCI │ PS/2 │ USB │ RTL8139 │ VGA │GPU │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                   Filesystems                         │   │
│  │   VFS ─┬─ initrd (/, read-only)                      │   │
│  │        ├─ SDFS (/house/localhost, persistent)         │   │
│  │        ├─ FAT32 (read, write experimental)            │   │
│  │        └─ devfs (/dev)                                │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                    Networking                         │   │
│  │   RTL8139 → Ethernet → ARP → IPv4 → ICMP            │   │
│  │                                ├─ TCP (experimental)  │   │
│  │                                ├─ UDP                 │   │
│  │                                ├─ DNS                 │   │
│  │                                ├─ DHCP                │   │
│  │                                └─ HTTP (experimental) │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              LGX Compositor (kernel task)              │   │
│  │  Scene Graph │ Event Bus │ Camera │ Layout │ Renderer │   │
│  │  Widgets: Window, Button, Label, Panel                │   │
│  │  Apps: Terminal, Settings, Explorer, Editor, Calc     │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
└──────────────────────────┬───────────────────────────────────┘
                           │ int $0x80
┌──────────────────────────▼───────────────────────────────────┐
│                  Userspace (Ring 3)                           │
│  crt0.S → main() → _exit()                                  │
│  libgloss/syscalls.c — newlib syscall bridge                 │
│  newlib (libc.a, libm.a)                                     │
│  Apps: Doom, Lua, TCC, Calc, View, Kilo, demo_gui           │
└──────────────────────────────────────────────────────────────┘
```

### Boot Process

1. **GRUB2** loads `kernel.bin` and the `initrd.tar` module via Multiboot2, configuring a 1024x768x32 framebuffer
2. **`boot.s`** builds the PML4 page tables (identity-map first 256 MB via 2 MB large pages), enables PAE + Long Mode via EFER MSR, loads a temporary GDT, and jumps to `entry_64`, which calls `kernel_main(magic, mbi_addr)`
3. **`kernel_main()`** initializes subsystems in order:
   - Serial debug output (COM1, 115200 baud)
   - Multiboot2 info parsing (memory map, framebuffer, modules)
   - GDT (9 entries + TSS), IDT (256 entries), PIC remapping
   - PMM (bitmap allocator from memory map)
   - VMM (4-level paging, identity-maps kernel memory)
   - Kernel heap (free-list allocator)
   - Framebuffer mapping (write-combining via PAT1)
   - VFS, drivers (ATA, AHCI, PCI, PS/2, USB, RTL8139)
   - Networking stack (ARP, TCP, UDP, DNS)
   - Initrd loading and SDFS mounting (with first-boot auto-install)
   - Timer (100 Hz), tasking, syscalls
   - GUI compositor and terminal tasks
   - `sti` + idle loop (`HLT`)

### Kernel

The kernel is a **monolithic higher-half** design. All kernel code is linked at virtual address `0x1000000` (16 MB). Key source files:

| File | Description |
|------|-------------|
| `src/kernel/kernel.c` | Entry point, subsystem initialization |
| `src/kernel/gdt.c` | Global Descriptor Table (9 entries + TSS) |
| `src/kernel/idt.c` | Interrupt Descriptor Table, PIC remapping |
| `src/kernel/isr.c` | ISR/IRQ dispatch, exception handling |
| `src/kernel/timer.c` | PIT 8253 timer (100 Hz default) |
| `src/kernel/task.c` | Process management, scheduler, context switch |
| `src/kernel/syscall.c` | Syscall dispatch and implementation |
| `src/kernel/elf.c` | ELF32/ELF64 loader |
| `src/kernel/string.c` | Kernel string library |
| `src/kernel/fast_memcpy.s` | SSE2-optimized memcpy (non-temporal stores) |
| `src/boot/process.s` | Context switch assembly (switch_to, fork) |
| `src/boot/interrupt.s` | ISR/IRQ stubs |

### Memory Management

**Physical Memory Manager (PMM)** — `src/kernel/pmm.c`
- Bitmap allocator: 1 bit per 4 KB page
- Initializes by marking all memory as used, then freeing regions from the Multiboot2 memory map
- Supports >4 GB via 64-bit addresses

**Virtual Memory Manager (VMM)** — `src/kernel/vmm.c`
- 4-level x86_64 paging (PML4 → PDP → PD → PT)
- 4 KB pages with automatic 2 MB large page splitting
- Per-process page directories with kernel inheritance
- Framebuffer mapped with write-combining

**Kernel Heap** — `src/kernel/kheap.c`
- Free-list allocator with 8-byte header per allocation
- `kmalloc()`, `kmalloc_a()` (page-aligned), `kmalloc_ap()` (aligned + physical), `kfree()`
- Page-aligned blocks also release physical pages via PMM

**Virtual Address Layout:**

| Range | Usage |
|-------|-------|
| `0x00000000–0x0FFFFFFF` | Kernel identity-map (first 256 MB) |
| `0x08048000` | ELF user entry point |
| `0x40000000+` | User heap (via `sys_brk`) |
| `0xBFFFF000` | User stack (64 KB, grows downward) |
| `0xFD000000–0xFDFFFFFF` | Framebuffer (16 MB, write-combining) |

### Process Management

- **Scheduler:** Preemptive round-robin with circular doubly-linked list of tasks
- **Context switch:** Full register save/restore (PUSH_ALL/POP_ALL), CR3 switch, TSS.rsp0 update
- **`fork()`** (syscall 14): Copies page directory, registers, heap, CWD
- **`execve()`** (syscall 15): Loads ELF32/ELF64, maps user stack at `0xBFFFF000`, sets argc/argv
- **`waitpid()`** (syscall 7): Blocks parent, reaps zombies, frees kernel stack and PCB
- **`exit()`** (syscall 1): Marks task as ZOMBIE, wakes parent
- **Ctrl+C:** Kills foreground task via `check_ctrl_c()` in scheduler

**Task States:** RUNNING, READY, SLEEPING, ZOMBIE

### Syscalls

33 system calls via `int $0x80` (IDT entry 128, ring-3 accessible):

| # | Name | Description |
|---|------|-------------|
| 1 | `exit` | Terminate process |
| 2 | `brk` | Grow/shrink heap |
| 3 | `read` | Read from fd (pipe, stdin, file) |
| 4 | `write` | Write to fd (stdout, file, pipe) |
| 5 | `open` | Open file (O_CREAT, O_TRUNC, O_APPEND) |
| 6 | `close` | Close fd |
| 7 | `waitpid` | Wait for child process |
| 8 | `getticks` | Get timer ticks |
| 10 | `keyboard_get_event` | Get keyboard event |
| 11 | `keyboard_is_pressed` | Query key state |
| 14 | `fork` | Fork process |
| 15 | `execve` | Execute program |
| 16 | `tcgetattr` | Get terminal attributes |
| 17 | `tcsetattr` | Set terminal attributes |
| 18 | `ioctl` | Device control (TIOCGWINSZ) |
| 19 | `lseek` | Seek in file |
| 20 | `getpid` | Get process ID |
| 21 | `gettimeofday` | Get time of day |
| 22 | `stat` | Get file status |
| 23 | `fstat` | Get fd status |
| 24 | `unlink` | Delete file |
| 25 | `mkdir` | Create directory |
| 26 | `chdir` | Change directory |
| 27 | `getcwd` | Get working directory |
| 28 | `kill` | Send signal to process |
| 29 | `rmdir` | Remove directory |
| 30 | `getdents` | List directory entries |
| 31 | `pipe` | Create pipe (4 KB ring buffer) |
| 32 | `dup` | Duplicate file descriptor |
| 33 | `dup2` | Duplicate fd to specific number |
| 120 | `gui_create_window` | Create GUI canvas/window |
| 121 | `gui_get_buffer` | Get window framebuffer |
| 122 | `gui_refresh` | Signal compositor redraw |
| 123 | `gui_node_move` | Move scene node |
| 124 | `gui_camera_zoom` | Zoom camera |

### Filesystems

**VFS** (`src/fs/vfs.c`)
- Mount-point tree with best-match path resolution
- Function pointer dispatch for read/write/open/close/readdir/finddir/create

**Initrd** (`src/fs/initrd.c`)
- TAR ustar format, loaded as GRUB Multiboot2 module
- Read-only, mounted at `/`
- Auto-copied to SDFS on first boot

**SDFS** (`src/fs/sdfs.c`)
- Custom block-based filesystem for persistent storage
- 4 KB block size with bitmap allocation
- Backends: ATA PIO, BM-IDE DMA, AHCI, ramdisk fallback (64 MB)
- Full API: create, delete, read, write, mkdir, rmdir, readdir

**FAT32** (`src/fs/fat32.c`)
- Read with cluster chain traversal
- Short filename (8.3) support
- Format and write support (experimental)

**DevFS** (`src/fs/devfs.c`)
- `/dev/null`, `/dev/zero`

### Drivers

| Driver | Source | Status | Notes |
|--------|--------|--------|-------|
| ATA PIO | `src/drivers/ata.c` | Functional | LBA28, primary/secondary bus |
| ATA BM-IDE DMA | `src/drivers/ata.c` | Functional | DMA via PIIX3 |
| AHCI/SATA | `src/drivers/ahci.c` | Functional | Command list + FIS |
| PCI | `src/drivers/pci.c` | Functional | 256-bus enumeration |
| PS/2 Keyboard | `src/drivers/keyboard.c` | Functional | Scancode set 1, ABNT2, modifier keys, event queue |
| PS/2 Mouse | `src/drivers/mouse.c` | Functional | Relative movement, 3 buttons |
| RTL8139 NIC | `src/drivers/rtl8139.c` | Functional | PCI detection, MMIO, 4 TX descriptors, RX ring |
| VGA Text | `src/drivers/vga.c` | Functional | 80x25 text mode, ANSI escape sequences |
| Framebuffer | `src/drivers/vga.c` | Functional | Linear framebuffer, PSF font rendering |
| BGA/GPU | `src/drivers/gpu.c` | Functional | Bochs Graphics Adaptor, MTRR write-combining |
| EDID | `src/drivers/edid.c` | Functional | Monitor info parsing |
| USB Core | `src/drivers/usb.c` | Partial | Device enumeration, polling |
| UHCI | `src/drivers/uhci.c` | Partial | USB 1.1 host controller |
| EHCI | `src/drivers/ehci.c` | Partial | USB 2.0 host controller |
| USB HID | `src/drivers/usb_hid.c` | Partial | Keyboard/mouse HID reports |
| PC Speaker | `src/drivers/pcspkr.c` | Functional | Melodies, note frequencies C4-B6 |
| Serial | `src/drivers/serial.c` | Functional | COM1, 115200 baud |

### Networking

The networking stack runs in-kernel using the RTL8139 NIC driver. Default
configuration: IP `10.0.2.15`, gateway `10.0.2.2` (QEMU user-mode NAT).

| Layer | Source | Status |
|-------|--------|--------|
| Ethernet | `src/net/netstack.c` | Functional |
| ARP | `src/net/netstack.c` | Functional — request/reply |
| IPv4 | `src/net/netstack.c` | Functional — routing, packet handling |
| ICMP | `src/net/netstack.c` | Functional — echo request/reply (`ping`) |
| TCP | `src/net/tcp.c` | Experimental — SYN/ACK handshake, no retransmission |
| UDP | `src/net/udp.c` | Functional — basic send/receive |
| DNS | `src/net/dns.c` | Functional — synchronous resolution |
| DHCP | `src/net/dhcp.c` | Experimental — DISCOVER/OFFER only |
| HTTP | `src/net/http.c` | Experimental — GET client |

### GUI Compositor (LGX)

The LGX (Liwus Graphics eXtension) is a **scene graph-based compositor** running
as a kernel task. It implements an **Infinite Canvas** paradigm where all
application windows exist in a navigable 2D space.

**Architecture modules** (`src/kernel/gui/`):

| Module | Files | Description |
|--------|-------|-------------|
| Scene Graph | `scene/node.c`, `scene/scene.c` | Tree with dirty flags, hit-testing, transform accumulation |
| Camera | `scene/camera.c` | Fixed-point pan/zoom/inertia, world↔screen coordinate conversion |
| Compositor | `render/compositor.c` | Frame loop: input → events → camera → transforms → draw → present |
| Renderer | `render/renderer.c`, `render/fb_renderer.c` | Abstract vtable + software backend, alpha blending |
| Event Bus | `core/event_bus.c` | Ring buffer, 64 subscribers, priority dispatch, propagation control |
| Input Manager | `input/input_manager.c` | Polls mouse/keyboard hardware, posts typed events to bus |
| Theme Engine | `core/theme_engine.c` | 12-color dark glassmorphism palette |
| Animation | `core/animation_engine.c` | Linear tweening for x/y/w/h/opacity, 64 animation slots |
| Layout | `layout/layout_engine.c` | VBOX/HBOX with flex weight, alignment (start/center/end/stretch) |
| Tools | `input/tools/` | Pan (WASD/arrows), Select (LMB hit-test), Move (titlebar drag) |
| Window Manager | `window/window_manager.c` | Z-order management, bring-to-front, close events |
| Focus Manager | `window/focus_manager.c` | Keyboard focus tracking |
| App Registry | `core/app_registry.c` | Arrow-key navigation for app launcher |
| Asset Manager | `assets/asset_manager.c` | PSF font loading |

**Widgets:**
- Window Node — titlebar, close button, drag, keyboard callbacks
- Button — hover/press animation, onclick callback
- Label — text rendering via PSF glyphs
- Panel — container with background and border

**Compositor Features:**
- Infinite canvas with dot-grid background
- Zoom in/out (`+`/`-` keys), pan (drag background)
- Return to Home (`H`), Fit to Screen (`F`)
- Radar HUD (minimap, bottom-right corner)
- Off-screen edge indicators
- Radial menu on right-click
- Boot animation
- Wallpaper (generated via Python script)

### Terminal and Shell

The terminal subsystem (`src/kernel/terminal/`) is a modular shell implementation:

- `terminal.c` — main loop, keyboard input, command execution
- `parser.c` — command-line tokenization
- `dispatcher.c` — command dispatch table
- `commands.c` — built-in commands (~30+)

**Built-in Commands:**

| Category | Commands |
|----------|----------|
| Filesystem | `ls`, `cd`, `pwd`, `cat`, `mkdir`, `rmdir`, `rm`, `cp`, `mv`, `touch` |
| System | `top`, `free`, `df`, `uptime`, `uname`, `whoami`, `reboot`, `version`, `meminfo`, `diskinfo` |
| Editor | `edit` (terminal-mode text editor) |
| Network | `ifconfig`, `ping`, `wget`, `host`, `ip` |
| Scripting | `lua` (runs Lua scripts) |
| Development | `exec` (launch ELF from initrd), `lsw` (list windows) |
| Disk | `format`, `mount` |
| Other | `help`, `clear`, `echo`, `tasklist`, `neofetch` |

**Features:**
- TAB completion with VFS integration
- ANSI escape sequences
- Direct ELF execution by name
- Unknown commands attempt ELF execution from initrd

---

## Applications

### Userspace Applications (Ring 3, ELF)

| Application | Source | Description |
|-------------|--------|-------------|
| **Doom** | `apps/doomgeneric/` | doomgeneric port with Freedoom data, renders via syscall 13 |
| **Lua 5.4** | `apps/lua/` | Full Lua interpreter for scripting |
| **TCC** | `apps/tcc/` | Tiny C Compiler — compile and run C programs inside LiwusOS |
| **Calc** | `apps/calc/` | Graphical calculator using Scene Graph SDK |
| **View** | `apps/view/` | Image viewer supporting PNG and JPEG |
| **Kilo** | `apps/kilo/` | Kilo text editor (nano-like) |
| **Editor Nano** | `apps/editor_nano/` | GUI nano-like text editor |
| **Demo GUI** | `apps/demo_gui/` | Scene Graph SDK demonstration |
| **Doomprobe** | `apps/doomprobe/` | Framebuffer graphics probe |
| **C4/Crun** | `apps/c4/` | C compiler in 4 functions |
| **Hello** | `apps/hello/` | Hello World test program |

### Kernel Applications (Ring 0, Tasks)

| Application | Source | Description |
|-------------|--------|-------------|
| Terminal | `src/kernel/terminal/` | Full shell with ~30 built-in commands |
| GUI Terminal | `src/kernel/gui/apps/gui_terminal.c` | 80×24 graphical terminal emulator |
| Settings | `src/kernel/gui/apps/gui_settings.c` | System info, display, sound, network |
| File Explorer | `src/kernel/gui/apps/gui_explorer.c` | Graphical file browser |
| Text Editor | `src/kernel/gui/apps/gui_editor.c` | Graphical text editor |
| Calculator | `src/kernel/gui/apps/gui_calculator.c` | GUI calculator |
| About | `src/kernel/gui/apps/gui_about.c` | About dialog |

---

## SDK and Development

LiwusOS provides a complete SDK for developing userspace applications.

### libgloss Layer (`libgloss/`)

- **`crt0.S`** — C runtime startup: extracts argc/argv, aligns stack, calls `main()`, then `_exit()`
- **`syscalls.c`** — Newlib syscall bridge via `int $0x80`: open, close, read, write, lseek, fstat, stat, gettimeofday, sbrk, kill, getpid, exit, fork, execve, chdir, getcwd, mkdir, unlink, getdents, dup2, pipe, and more

### SDK Libraries (`sdk/lib/`)

| Library | Description |
|---------|-------------|
| `libc.a` | Newlib C standard library |
| `libm.a` | Newlib math library |
| `libgloss.a` | LiwusOS syscall glue layer |
| `libliwus_gui.a` | Scene Graph GUI widget library |
| `liblgx.a` | LGX legacy framebuffer library |
| `libz.a` | zlib compression |
| `libpng.a` | PNG image support |
| `libjpeg.a` | JPEG image support |

### SDK Headers (`sdk/include/`)

Comprehensive newlib-compatible header set (~150 headers):
- Standard C: `stdio.h`, `stdlib.h`, `string.h`, `stdint.h`, `math.h`, `errno.h`, ...
- POSIX: `unistd.h`, `fcntl.h`, `dirent.h`, `pthread.h`, `termios.h`, ...
- System: `sys/stat.h`, `sys/socket.h`, `sys/mman.h`, `sys/reboot.h`, ...
- LiwusOS: `libliw.h` (framebuffer API), `liwus_gui.h` (Scene Graph API)

### SDK Tools (`sdk/tools/`)

| Tool | Language | Description |
|------|----------|-------------|
| `liw-builder` | C | Packages ELF + JSON manifest + resources into `.liw` format |
| `img-gen` | C | Generates test image binaries |
| `img2c.py` | Python | Converts BMP to C header with ARGB pixel data |
| `gen_wallpaper.py` | Python | Generates gradient wallpaper header |
| `gen_ui_assets.py` | Python | Generates UI assets (buttons, icons, shadows) |
| `convert_wallpaper.py` | Python | Converts any image to wallpaper header |

### LIW Package Format

```c
typedef struct {
    uint32_t magic;            // 0x5845574C ("LWEX")
    uint32_t version;          // 1
    uint32_t flags;            // 0
    uint32_t entry_offset;     // ELF binary offset
    uint32_t entry_size;       // ELF binary size
    uint32_t manifest_offset;  // JSON manifest offset
    uint32_t manifest_size;    // JSON manifest size
    uint32_t resources_offset; // Resources bundle offset
    uint32_t resources_size;   // Resources bundle size
    uint8_t padding[32];       // Reserved
} liw_header_t;
```

### Third-Party Libraries (`third_party/`)

| Library | Version | Source |
|---------|---------|--------|
| Lua | 5.4.6 | Full interpreter source |
| TCC | — | Tiny C Compiler (single-file) |
| doomgeneric | — | Doom source port |
| zlib | — | Compression library |
| libpng | — | PNG format support |
| libjpeg | — | JPEG format support |

### LIWOS GUI SDK Usage Example

```c
#include <liwus_gui.h>

int main(void) {
    // Create a window
    Canvas canvas = canvas_create(400, 300, "My App");

    // Add widgets
    Node label = text_create("Hello, LiwusOS!", 0xFFFFFFFF);
    Node button = button_create("Click Me", 120, 36);

    canvas_add(canvas, label);
    canvas_add(canvas, button);

    // Position and zoom
    node_move(label, 100, 50);
    node_move(button, 140, 100);
    camera_zoom(2000); // 2.0x zoom (scaled by 1000)

    return 0;
}
```

### Legacy Framebuffer SDK Usage

```c
#include <libliw.h>

int main(void) {
    liw_fb_info_t fb;
    liw_get_fb_info(&fb);

    // Draw directly to framebuffer
    for (int y = 0; y < fb.height; y++)
        for (int x = 0; x < fb.width; x++)
            liw_draw_pixel(x, y, 0xFF0000FF); // Red

    liw_present_fb();
    return 0;
}
```

---

## Directory Structure

```
LiwusOS/
├── src/                          # Kernel source code
│   ├── boot/                     # Boot assembly, linker script, GRUB config
│   │   ├── boot.s                # Multiboot2 entry, long mode switch
│   │   ├── interrupt.s           # ISR/IRQ stubs
│   │   ├── process.s             # Context switch assembly
│   │   ├── linker.ld             # Kernel linker script (16 MB higher-half)
│   │   └── grub.cfg              # GRUB2 configuration
│   ├── kernel/                   # Kernel core
│   │   ├── kernel.c              # Entry point, subsystem init
│   │   ├── gdt.c                 # Global Descriptor Table
│   │   ├── idt.c                 # Interrupt Descriptor Table
│   │   ├── isr.c                 # Interrupt dispatch
│   │   ├── pmm.c                 # Physical Memory Manager
│   │   ├── vmm.c                 # Virtual Memory Manager
│   │   ├── kheap.c               # Kernel heap allocator
│   │   ├── task.c                # Process management, scheduler
│   │   ├── syscall.c             # System call handler
│   │   ├── elf.c                 # ELF32/ELF64 loader
│   │   ├── timer.c               # PIT timer
│   │   ├── string.c              # String library
│   │   ├── fast_memcpy.s         # SSE2-optimized memcpy
│   │   ├── terminal/             # Shell (terminal, parser, commands)
│   │   └── gui/                  # LGX Compositor and GUI
│   │       ├── gui_main.c/h      # GUI bootstrap
│   │       ├── core/             # Event bus, theme, animation, app registry
│   │       ├── scene/            # Scene graph, camera
│   │       ├── render/           # Compositor, renderer, framebuffer
│   │       ├── layout/           # Layout engine (VBOX/HBOX)
│   │       ├── input/            # Input manager and tools
│   │       ├── widgets/          # Window, Button, Label, Panel
│   │       ├── window/           # Window manager, focus manager
│   │       ├── assets/           # Icons, font data
│   │       ├── math/             # Color, rect, vec2 utilities
│   │       └── apps/             # GUI apps (terminal, settings, etc.)
│   ├── drivers/                  # Hardware drivers
│   ├── fs/                       # Filesystems (VFS, initrd, SDFS, FAT32, devfs)
│   └── net/                      # Networking stack
├── include/                      # Kernel headers (~51 files)
├── apps/                         # Userspace applications
│   ├── doomgeneric/              # Doom port
│   ├── lua/                      # Lua 5.4
│   ├── tcc/                      # Tiny C Compiler
│   ├── calc/                     # Calculator
│   ├── view/                     # Image viewer
│   ├── kilo/                     # Text editor
│   ├── editor_nano/              # GUI nano editor
│   ├── demo_gui/                 # Scene Graph demo
│   └── doomprobe/                # Framebuffer probe
├── libgloss/                     # Userspace runtime (crt0.S + syscalls.c)
├── sdk/                          # Development SDK
│   ├── include/                  # Newlib-compatible headers
│   ├── lib/                      # Pre-compiled libraries
│   └── tools/                    # Build tools (liw-builder, img-gen, etc.)
├── third_party/                  # External libraries (Lua, TCC, zlib, libpng, libjpeg)
├── toolchain/                    # Cross-compilation toolchain build script
├── assets/                       # Logos and images
├── docs/                         # Architecture and API documentation
├── repo/                         # Staging area for initrd contents
├── isodir/                       # ISO staging directory
├── Makefile                      # Main build system
├── Dockerfile                    # Docker build environment
├── build.sh                      # Build via Docker
├── build_app.sh                  # Build individual user app
├── run.sh                        # Build and run in QEMU
├── clean.sh                      # Clean build artifacts
└── README.md                     # This file
```

---

## Building

### Prerequisites

- **Docker** (recommended) — provides a reproducible build environment with all dependencies
- **Alternative:** Linux host with GCC, NASM, GRUB tools, xorriso, mtools, QEMU

### Build Commands

```bash
# Full build + run in QEMU (recommended)
sudo ./run.sh

# Build ISO only (via Docker)
sudo ./build.sh

# Build and run with serial output
make run-serial

# Build and run with debug logs
make run-log

# Build an individual userspace app
./build_app.sh apps/calc/calc.c

# Clean build artifacts
sudo ./clean.sh
```

### Cross-Compilation Toolchain

A custom cross-compiler targeting `x86_64-liwusos` can be built:

```bash
cd toolchain
./build-x86_64-liwusos-toolchain.sh
```

This builds (in order):
1. **Binutils 2.42** — cross-assembler and linker
2. **GCC 14.1.0** (stage 1) — C compiler without libc
3. **Newlib 4.4.0** — C standard library for freestanding targets
4. **libgloss** — LiwusOS syscall layer
5. **GCC 14.1.0** (final) — C + C++ with newlib

Default install prefix: `/opt/liwusos-toolchain`

### Docker Environment

The `Dockerfile` builds a Debian Bookworm image with:
- Cross-compiler: `i686-elf-gcc` (Binutils 2.42 + GCC 13.2.0)
- Build tools: NASM, GRUB tools (grub-mkrescue, grub-file), xorriso, mtools
- QEMU for testing

---

## Running

### QEMU

The primary development and testing environment:

```bash
# Default run (512 MB RAM, AHCI disk, RTL8139 NIC)
sudo ./run.sh

# Manual QEMU command
qemu-system-x86_64 \
  -m 512M \
  -cdrom liwusos.iso \
  -drive id=disk,file=liwus_disk.img,if=none,format=raw \
  -device ahci,id=ahci \
  -device ide-hd,drive=disk,bus=ahci.0 \
  -vga std \
  -serial stdio \
  -net nic,model=rtl8139 \
  -net user,hostfwd=tcp::2222-:2222

# With serial logging
make run-serial

# With debug output (interrupt tracing, guest errors)
make run-log
```

The first boot auto-formats the SDFS disk image and copies the initrd contents
to persistent storage. Subsequent boots use the persistent disk.

### Testing

**Terminal and filesystem:**
```
pwd
ls
mkdir TESTE
cd TESTE
touch ARQUIVO.TXT
ls
```

**Text editor:**
```
edit ARQUIVO.TXT
```

**Lua scripting:**
```
lua hello.lua
```

**Networking:**
```
ifconfig
ping 10.0.2.2 4
```

**System monitoring:**
```
top
free
df
uptime
uname
whoami
```

**Doom:**
The `doomgeneric` binary and `freedoom1.wad` are included in the initrd.

**Compiling C inside LiwusOS:**
```
tcc hello.c -o hello
./hello
```

---

## Dependencies

### Build Dependencies (Docker)

| Package | Purpose |
|---------|---------|
| `gcc`, `g++` | Host compiler for tools |
| `nasm` | Assembler for boot/kernel assembly |
| `xorriso` | ISO 9660 image creation |
| `mtools` | FAT filesystem manipulation for GRUB |
| `grub-pc-bin`, `grub-common` | GRUB2 bootloader tools |
| `qemu-system-x86` | Emulator for testing |
| `wget`, `bzip2`, `tar` | Downloading and extracting toolchain sources |

### Runtime Dependencies

- **QEMU** (qemu-system-x86_64) — for running the OS
- **Freedoom** — `freedoom1.wad` included in the repository for Doom

### Third-Party Source Dependencies

All included in `third_party/`:
- Lua 5.4.6
- TCC (Tiny C Compiler)
- doomgeneric
- zlib
- libpng
- libjpeg

---

## Hardware Compatibility

| Component | Compatibility | Notes |
|-----------|---------------|-------|
| CPU | x86_64 required | Long mode (64-bit) |
| QEMU | Primary target | All features tested here |
| Bochs | Partial | BGA/VBE framebuffer works |
| VirtualBox | Partial | Shutdown/reboot ports |
| RAM | 512 MB recommended | Minimum functional: 128 MB |
| Disk | ATA / AHCI / SATA | PIO, BM-IDE DMA, AHCI |
| Network | RTL8139 | Functional driver |
| Keyboard | PS/2 + USB HID | ABNT2 layout support |
| Mouse | PS/2 + USB HID | Relative, 3 buttons |
| Display | VGA standard | Text mode + framebuffer |
| GPU | VGA std (QEMU) | No 3D acceleration |
| USB | UHCI / EHCI | Partial (no hubs, no hot-plug) |

**Not supported:** WiFi, audio (beyond PC speaker), SMP (multi-core), power management.

---

## Roadmap

### Short-term

1. **Stabilize networking** — TCP retransmission, circular buffer, reliable DHCP
2. **Migrate kernel apps to userspace** — Terminal, settings, explorer as ring-3 ELF
3. **FAT32 write stabilization** — Fix cluster allocation bugs
4. **USB hardening** — Verify controller type before enumeration, add hub support

### Medium-term

5. **Copy-on-Write fork** — Avoid full page copy on fork()
6. **Expand FD table** — Dynamic or larger fixed-size (256+)
7. **Scheduler priorities** — Nice values or priority classes
8. **More userspace ports** — Additional applications and utilities

### Long-term

9. **SMP** — Multi-core support
10. **Audio** — AC97 or HDA audio driver
11. **TLS/HTTPS** — Secure networking
12. **POSIX compliance** — Broader compatibility with Linux programs
13. **Power management** — ACPI, S3/S4 sleep, CPU frequency scaling

---

## Project Status

LiwusOS is a **pre-alpha hobby operating system** with a surprisingly complete
feature set. The following components are functional end-to-end:

- Boot → long mode → kernel initialization → VFS → persistent storage
- Preemptive scheduler with fork/exec/waitpid
- Terminal with ~30 built-in commands
- LGX compositor with scene graph, camera, event bus, and widget toolkit
- GUI terminal, settings, and file explorer running inside the compositor
- User-space ELF loading (32-bit and 64-bit)
- Doom running with framebuffer rendering via syscalls
- Lua 5.4 scripting
- TCC compiling C programs inside the OS
- Networking stack with functional ping (ICMP)

**Known limitations:**
- All system apps (terminal, compositor, settings) run in ring-0
- TCP implementation is experimental (no retransmission, no flow control)
- FAT32 write is unstable
- No SMP, no audio, no power management
- USB support is partial

For a detailed analysis of all issues and technical debt, see
[`RELATORIO_TECNICO.md`](RELATORIO_TECNICO.md).

---

## Contributing

LiwusOS is a personal hobby project. Contributions are welcome but not expected.
If you'd like to contribute:

1. Fork the repository
2. Create a feature branch
3. Test your changes in QEMU
4. Submit a pull request

**Code style:**
- GNU C99 for kernel code
- English identifiers, Portuguese comments acceptable
- 4-space indentation (tabs for assembly)
- Functions prefixed with subsystem (e.g., `gui_`, `net_`, `vfs_`)

---

## License

This is a hobby/educational project. Third-party components retain their
original licenses:

- Lua 5.4 — MIT License
- zlib — zlib/libpng License
- libpng — libpng License
- libjpeg — IJG License
- Doom (doomgeneric) — Doom Source License / GPLv2
- TCC — LGPL v2.1
- Newlib — BSD-style License

---

## Credits

- **Doom** — doomgeneric port, Freedoom data files
- **Lua** — Lua 5.4.6 scripting language
- **TCC** — Tiny C Compiler by Fabrice Bellard
- **Newlib** — C standard library for embedded systems
- **GNU Toolchain** — GCC, Binutils
- **GRUB2** — Grand Unified Bootloader
- **QEMU** — Generic machine emulator
- **OSDev Wiki** — Community reference for OS development
- **Freedoom** — Free Doom content

---

*LiwusOS — An experimental x86_64 operating system with its own stack.*
*Built as a learning project covering the full path from bootloader to userspace.*
