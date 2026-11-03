# USB support status

LIWUS has native xHCI support for QEMU boot-protocol USB keyboards and mice.
The driver enumerates/configures these HID devices, polls interrupt-IN reports,
and injects input into the existing LIWUS paths. General USB support remains
incomplete; no Linux code or Linux USB internals are used.

## Current implementation

The kernel finds a PCI xHCI controller, validates its memory BAR, maps its
registers as uncached MMIO, performs the BIOS handoff, allocates DMA structures
within the controller's address limit, then enables bus mastering and starts
the controller. It polls the command/event rings, resets connected root ports,
enables slots, addresses devices, reads their descriptors, selects boot HID
configurations, and polls interrupt-IN endpoints from the timer handler.

Run `make usb-probe-test` to boot a disposable QEMU snapshot with one emulated
USB keyboard and mouse attached. The test injects `a`, mouse movement, and a
button press through QEMU's monitor. It verifies xHCI startup, device
configuration, reports from both devices, the keyboard character echoed by the
native shell, and mouse activity reaching LIWUS. This verifies QEMU's emulated
controller path; physical USB hardware is not yet validated.

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
5. Add hubs and other classes only after the transfer and enumeration layers
   have regression coverage on QEMU and hardware.

No USB controller is allowed to bus-master until the kernel owns initialized
DMA rings and can stop the controller safely. A controller that is absent or
unsupported must not prevent normal LIWUS boot.

## Text-only shell

The current system starts the native command shell; there is no desktop,
window manager, or compositor in this source tree. Kernel output now uses the
80x25 VGA text buffer, and `make run` displays it through QEMU's curses
backend instead of opening a graphical QEMU display. This strict legacy text
backend may not be available on UEFI-only machines without VGA text memory; a
framebuffer console will be needed there as a text renderer. `make run` reuses
`liwus_disk.img`, and the disk's installed applications persist across builds.
To keep the shell in that existing image aligned with the current text-only
initrd, rebuild the shell and replace only `/bin/liwsh.lxe` while QEMU is off:

```sh
make all
make sdfs-put FILE=build/liwus-sdk/bin/liwsh.lxe \
  DEST=/bin/liwsh.lxe DISK=liwus_disk.img REPLACE=1
```

The SDFS installer preserves unrelated files in the image. A fresh SDFS image
gets the shell from the initrd automatically.
