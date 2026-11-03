# USB support status

LIWUS is beginning native USB support through an independent kernel USB
driver. A USB host stack is not implemented yet. Linux code and Linux USB
internals are not used.

## Current implementation

The kernel finds a PCI xHCI controller, validates its memory BAR, enables PCI
memory decoding (without enabling bus mastering), maps its capability page as
uncached MMIO, and reads the xHCI version and root-port count. This is a
read-only capability probe; it deliberately does not reset or start the
controller and does not submit DMA.

The HID layer now decodes standard boot-keyboard and boot-mouse reports into
the existing LIWUS keyboard and mouse input paths. It preserves key-down and
key-up transitions, modifiers, and rollover reports. The xHCI driver does not
yet submit USB transfers or enumerate devices, so USB keyboards and mice are
still not usable; PS/2 remains the active input path.

The next low-level piece is now present and host-tested: command-ring and
event-ring primitives with the 16-byte TRB layout, a physically addressed Link
TRB, a bounded command queue, wrap handling, and producer/consumer cycle-bit
tracking. The ring segment is exactly one page and kept separate from software
metadata so it can be backed by a single physically contiguous DMA page. These
primitives are not wired to MMIO yet and do not grant the controller bus
mastering; they do not change the current input capability.

## Implementation sequence

1. Keep PCI discovery and xHCI capability probing safe on machines without an
   xHCI controller or with an unassigned/invalid BAR.
2. Add bounded DMA allocation and wire the tested rings into xHCI reset/start,
   with polling completion handling before enabling interrupts.
3. Enumerate root ports and devices; implement control transfers and descriptor
   parsing with strict length and bounds checks.
4. Add USB HID boot-keyboard input and retain PS/2 as a fallback.
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
