# USB support status

LIWUS is beginning native USB support through an independent kernel USB
driver. A USB host stack is not implemented yet. Linux code and Linux USB
internals are not used.

## Current implementation

The kernel finds a PCI xHCI controller, validates its memory BAR, enables PCI
memory decoding (without enabling bus mastering), maps its capability page as
uncached MMIO, reads the xHCI capabilities and root-port connection/speed
status, and initializes one command ring plus one event ring. The controller is
reset and started only after all DMA structures are allocated and programmed.
The current command path polls completion events and successfully submits an
xHCI No-Op command. It has no endpoint or device enumeration yet.

Run `make usb-probe-test` to boot a disposable QEMU snapshot with one emulated
USB keyboard and mouse attached. The test verifies xHCI capability reads, at
least two connected root ports, a completed command-ring No-Op, reset root
ports, enabled device slots, and normal kernel boot. This demonstrates basic
controller transport and slot allocation. Device addressing, configuration,
and key or mouse input are still unimplemented.

The HID layer now decodes standard boot-keyboard and boot-mouse reports into
the existing LIWUS keyboard and mouse input paths. It preserves key-down and
key-up transitions, modifiers, and rollover reports. The xHCI driver does not
yet submit USB transfers or enumerate devices, so USB keyboards and mice are
still not usable; PS/2 remains the active input path.

The HID layer also has a bounds-checked parser for complete USB configuration
descriptors. It locates boot keyboard/mouse interfaces and their interrupt-IN
endpoints, and rejects truncated or malformed descriptor chains. Host tests
cover this parser alongside report decoding. Device enumeration and descriptor
retrieval are still unimplemented, so this parser is not yet exercised against
a physical or emulated USB device.

The command/event ring primitives have host regression coverage for the 16-byte
TRB layout, physical Link TRB, bounded queue, wrap handling, and producer/
consumer cycle bits. QEMU integration now additionally validates DMA setup,
controller reset/start, command completion, root-port reset, and Enable Slot
commands for an attached keyboard and mouse. USB HID still does not provide
input until Address Device, descriptor/configuration control transfers, and
interrupt transfers are implemented.

## Implementation sequence

1. Keep PCI discovery and xHCI capability probing safe on machines without an
   xHCI controller or with an unassigned/invalid BAR.
2. Add bounded DMA allocation and wire the tested rings into xHCI reset/start,
   with polling completion handling before enabling interrupts. (Complete for
   the polling command path.)
3. Address enabled slots; implement endpoint-zero control transfers and
   retrieve configuration descriptors into the tested strict parser. (Port
   reset and Enable Slot are implemented; Address Device remains.)
4. Attach interrupt-IN endpoints to the tested HID report decoder and retain
   PS/2 as a fallback.
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
