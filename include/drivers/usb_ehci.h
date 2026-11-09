#ifndef LIWUS_USB_EHCI_H
#define LIWUS_USB_EHCI_H

/* Initialize EHCI and start a safe, terminated async DMA reclamation head. */
void usb_ehci_probe(void);
void usb_ehci_poll(void);
void usb_ehci_service(void);
int usb_ehci_controller_ready(void);
int usb_ehci_keyboard_ready(void);
const char *usb_ehci_boot_status(void);

#endif
