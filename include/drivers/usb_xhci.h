#ifndef LIWUS_USB_XHCI_H
#define LIWUS_USB_XHCI_H

/* Discover and safely inspect the first PCI xHCI host controller.
 * This probe does not start transfers or claim connected USB devices. */
void usb_xhci_probe(void);

#endif
