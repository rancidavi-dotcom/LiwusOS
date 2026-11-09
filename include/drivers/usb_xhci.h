#ifndef LIWUS_USB_XHCI_H
#define LIWUS_USB_XHCI_H

/* Discover and safely inspect the first PCI xHCI host controller.
 * This probe does not start transfers or claim connected USB devices. */
void usb_xhci_probe(void);
/* Polls controller events and recycles boot-HID interrupt transfers. */
void usb_xhci_poll(void);
/* Runtime state for the boot console and hardware diagnostics. */
int usb_xhci_controller_ready(void);
int usb_xhci_keyboard_ready(void);
const char *usb_xhci_boot_status(void);

#endif
