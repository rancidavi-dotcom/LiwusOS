# USB support status

Physical test-machine observations and BIOS settings are recorded in
[HARDWARE_PC_H61.md](HARDWARE_PC_H61.md). Consult that profile before asking
the maintainer to repeat hardware details or interpreting host Linux PCI data.

LIWUS has native xHCI support for QEMU boot-protocol USB keyboards and mice.
The driver enumerates/configures these HID devices, polls interrupt-IN reports,
and injects input into the existing LIWUS paths. General USB support remains
incomplete; no Linux code or Linux USB internals are used.

After requesting EHCI ownership, the LIWUS driver disables legacy SMI sources,
resets the controller, starts its configured DMA schedule, then sets
`CONFIGFLAG` as the final host-controller configuration action before it reads
and services root ports. It performs endpoint-zero control transfers and USB
device addressing, configures boot keyboards, and polls periodic interrupt-IN
reports. Full/low-speed periodic queue heads carry
nonzero start-split and complete-split masks plus transaction-translator
hub/port fields. Each failed descriptor request is retried at most three times
without backoff; there is no unbounded retry loop. Failed control-transfer
logs preserve the original SETUP, DATA, and STATUS qTD tokens, the queue-head
token, and USBSTS before schedule recovery. The timer-driven EHCI report rearm
path uses a nonblocking stop/rearm/start state machine; it does not spin
waiting for schedule status inside the timer interrupt.
Its hub path reads the standard configuration and selects `SET_CONFIGURATION`
before sending hub-class requests, then reads hub descriptors and powers/resets
downstream ports. It passes transaction-translator hub/port fields into EHCI
queue heads for low/full-speed devices. QEMU verifies an EHCI-attached keyboard
from descriptors through HID report translation into the kernel keyboard path.
The shell's typed line is not echoed before Enter, so this test does not claim an
end-to-end command submission. The H61 integrated RMH (`8087:0024`) still
requires a physical boot to validate its hub-specific behavior; QEMU's
built-in hub is full-speed and cannot be attached directly to its
high-speed-only EHCI root.
`make usb-ehci-start-test` runs the EHCI input path, while
`make usb-probe-test` preserves the separate xHCI keyboard/mouse regression.

## Current implementation

The kernel finds a PCI xHCI controller, validates its memory BAR, maps its
registers as uncached MMIO, performs the BIOS handoff, allocates DMA structures
within the controller's address limit, then enables bus mastering and starts
the controller. It polls the command/event rings, resets connected root ports,
enables slots, addresses devices, reads their descriptors, selects boot HID
configurations, and polls interrupt-IN endpoints from the timer handler.

`make run` disables QEMU's i8042 controller and uses the pinned Q35/ICH9
machine with two EHCI controllers, matching the H61 machine's broad USB
controller topology. The virtual keyboard and mouse attach to EHCI. QEMU's
built-in `usb-hub` is full-speed and cannot attach to an EHCI high-speed root
port, so this profile cannot reproduce the H61's integrated high-speed RMH
(`8087:0024`) and keeps the devices directly on EHCI for usable interactive
input. The separate `usb-ehci-start-test` validates the same two-controller
profile; `usb-probe-test` remains the xHCI regression.

Run `make usb-probe-test` to boot a disposable QEMU snapshot with one emulated
USB keyboard and mouse attached. The test injects `a`, mouse movement, and a
button press through QEMU's monitor. It verifies xHCI startup, device
configuration, reports from both devices, the keyboard character echoed by the
native shell, and mouse activity reaching LIWUS. This verifies QEMU's emulated
controller path; physical USB hardware is not yet validated. Current scope is
boot-protocol keyboard/mouse interfaces connected directly to a discovered
xHCI root port at boot. USB hubs, hot-plug, multiple-controller selection, and
HID devices that lack boot protocol are not supported yet.

The HID layer decodes standard boot-keyboard and boot-mouse reports into the
existing LIWUS keyboard and mouse input paths. It preserves key-down and
key-up transitions, modifiers, and rollover reports. The xHCI driver uses
control transfers for enumeration and interrupt transfers for reports. PS/2
continues to work as a fallback input path.

The HID layer also has a bounds-checked parser for complete USB configuration
descriptors. It locates boot keyboard/mouse interfaces and their interrupt-IN
endpoints, and rejects truncated or malformed descriptor chains. Host tests
cover it alongside report decoding; the QEMU test also exercises it against
the attached emulated keyboard and mouse.

The command/event ring primitives have host regression coverage for the 16-byte
TRB layout, physical Link TRB, bounded queue, wrap handling, and producer/
consumer cycle bits. QEMU integration now additionally validates DMA setup,
controller reset/start, command completion, root-port reset, Enable Slot,
Address Device, endpoint-zero descriptor/configuration transfers, interrupt
endpoint setup, and keyboard/mouse reports for attached devices.

## Implementation sequence

1. Keep PCI discovery and xHCI capability probing safe on machines without an
   xHCI controller or with an unassigned/invalid BAR.
2. Add bounded DMA allocation and wire the tested rings into xHCI reset/start,
   with polling completion handling before enabling interrupts. (Complete for
   the polling command path.)
3. Address enabled slots; implement endpoint-zero control transfers and
   retrieve configuration descriptors into the tested strict parser. (Port
   reset, Enable Slot, Address Device, descriptor retrieval, configuration
   selection, and HID boot protocol selection are implemented.)
4. Attach interrupt-IN endpoints to the tested HID report decoder and retain
   PS/2 as a fallback. (Implemented and exercised in QEMU.)
5. Boot the current image on the H61 and validate its Intel integrated
   Rate-Matching Hub, then diagnose any mismatches in downstream reset,
   transaction-translator scheduling, or keyboard input from captured logs.

No USB controller is allowed to bus-master until the kernel owns initialized
DMA structures and can stop the controller safely. EHCI now submits endpoint-
zero control transfers and a periodic keyboard report queue after controller
ownership and setup. A controller that is absent or unsupported must not
prevent normal LIWUS boot.

## Text-only shell

The current system starts the native command shell; there is no desktop,
window manager, or compositor in this source tree. Kernel output stays textual.
When GRUB supplies a 32-bit RGB framebuffer, the kernel draws the text console
there; otherwise it falls back to the 80x25 VGA text buffer. This lets UEFI
machines display the shell even when legacy VGA text memory is unavailable.
`make run` displays the console through QEMU's curses backend and reuses
`liwus_disk.img`, so the disk's installed applications persist across builds.
To keep the shell in that existing image aligned with the current text-only
initrd, rebuild the shell and replace only `/bin/liwsh.lxe` while QEMU is off:

```sh
make all
make sdfs-put FILE=build/liwus-sdk/bin/liwsh.lxe \
  DEST=/bin/liwsh.lxe DISK=liwus_disk.img REPLACE=1
```

The SDFS installer preserves unrelated files in the image. A fresh SDFS image
gets the shell from the initrd automatically.
