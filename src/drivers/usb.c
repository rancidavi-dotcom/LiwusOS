#include "usb.h"
#include "usb_spec.h"
#include "serial.h"
#include "pci.h"
#include "kheap.h"
#include "string.h"

// Protótipos de drivers de controlador específicos
extern void uhci_init(pci_device_t *dev);
extern void ehci_init(pci_device_t *dev);
extern int ehci_send_control(uint8_t addr, usb_setup_packet_t *setup, void *data, uint16_t len);

extern int uhci_send_control(pci_device_t *dev, uint8_t addr, void *setup, void *data, uint16_t len);

static usb_device_t *usb_devices = NULL;
static uint8_t next_usb_addr = 1;

void usb_register_device(usb_device_t *dev) {
    dev->next = usb_devices;
    usb_devices = dev;
}

void usb_enumerate(void *controller, uint8_t port, int type) {
    serial_print("USB: Enumerating device on port ");
    char pstr[4]; itoa(port, pstr, 10); serial_print(pstr);
    serial_print(" (Type="); itoa(type, pstr, 10); serial_print(pstr);
    serial_print(")\n");

    pci_device_t *pci_dev = (pci_device_t *)controller;

    // 1. Get Device Descriptor
    usb_setup_packet_t setup;
    usb_device_descriptor_t *desc = kmalloc(sizeof(usb_device_descriptor_t));
    
    setup.request_type = 0x80;
    setup.request = USB_REQ_GET_DESCRIPTOR;
    setup.value = (USB_DESC_DEVICE << 8);
    setup.index = 0;
    setup.length = 8;

    int res = -1;
    if (type == 1) res = uhci_send_control(pci_dev, 0, &setup, desc, 8);
    else if (type == 2) res = ehci_send_control(0, &setup, desc, 8);

    if (res < 0) {
        serial_print("USB: Failed to get descriptor (addr 0)\n");
        return;
    }

    // 2. Set Address
    uint8_t addr = next_usb_addr++;
    setup.request_type = 0x00;
    setup.request = USB_REQ_SET_ADDRESS;
    setup.value = addr;
    setup.index = 0;
    setup.length = 0;

    if (type == 1) res = uhci_send_control(pci_dev, 0, &setup, NULL, 0);
    else if (type == 2) res = ehci_send_control(0, &setup, NULL, 0);

    if (res < 0) {
        serial_print("USB: Failed to set address\n");
        return;
    }
    for(int i=0; i<10000; i++) asm volatile("pause");

    // 3. Get Full Descriptor
    setup.request_type = 0x80;
    setup.request = USB_REQ_GET_DESCRIPTOR;
    setup.value = (USB_DESC_DEVICE << 8);
    setup.index = 0;
    setup.length = sizeof(usb_device_descriptor_t);

    if (type == 1) res = uhci_send_control(pci_dev, addr, &setup, desc, sizeof(usb_device_descriptor_t));
    else if (type == 2) res = ehci_send_control(addr, &setup, desc, sizeof(usb_device_descriptor_t));

    if (res < 0) {
        serial_print("USB: Failed to get full descriptor\n");
        return;
    }

    // 4. Get Config Descriptor
    usb_config_descriptor_t *cfg = kmalloc(sizeof(usb_config_descriptor_t));
    setup.request_type = 0x80;
    setup.request = USB_REQ_GET_DESCRIPTOR;
    setup.value = (USB_DESC_CONFIG << 8);
    setup.index = 0;
    setup.length = sizeof(usb_config_descriptor_t);

    ehci_send_control(addr, &setup, cfg, sizeof(usb_config_descriptor_t));
    
    // Read full config (including interfaces and endpoints)
    uint16_t total_len = cfg->total_length;
    uint8_t *full_cfg = kmalloc(total_len);
    setup.length = total_len;
    ehci_send_control(addr, &setup, full_cfg, total_len);

    // 5. Set Configuration
    setup.request_type = 0x00;
    setup.request = USB_REQ_SET_CONFIG;
    setup.value = cfg->config_value;
    setup.index = 0;
    setup.length = 0;
    if (type == 1) res = uhci_send_control(pci_dev, addr, &setup, NULL, 0);
    else if (type == 2) res = ehci_send_control(addr, &setup, NULL, 0);

    serial_print("USB: Configured device. Parsing interfaces...\n");

    // 6. Parse Interfaces
    uint8_t *ptr = full_cfg + sizeof(usb_config_descriptor_t);
    usb_device_t *usb_dev = kmalloc(sizeof(usb_device_t));
    memset(usb_dev, 0, sizeof(usb_device_t));
    usb_dev->address = addr;
    usb_dev->port = port;
    usb_dev->controller = controller;

    while (ptr < full_cfg + total_len) {
        uint8_t len = ptr[0];
        uint8_t type_desc = ptr[1];

        if (type_desc == USB_DESC_INTERFACE) {
            usb_interface_descriptor_t *iface = (usb_interface_descriptor_t *)ptr;
            if (iface->interface_class == 0x03) { // HID
                if (iface->interface_protocol == 0x01) {
                    serial_print("USB: Found KEYBOARD interface\n");
                    usb_dev->type = 1;
                } else if (iface->interface_protocol == 0x02) {
                    serial_print("USB: Found MOUSE interface\n");
                    usb_dev->type = 2;
                }
            }
        } else if (type_desc == USB_DESC_ENDPOINT && usb_dev->type != 0) {
            usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t *)ptr;
            if (ep->endpoint_address & 0x80) { // IN endpoint
                serial_print("USB: Setting up HID polling on EP ");
                char estr[4]; itoa(ep->endpoint_address & 0x7F, estr, 10); serial_print(estr);
                serial_print("\n");
                
                uint8_t *report_buf = kmalloc(8);
                if (type == 1) {
                    extern int uhci_register_interrupt(pci_device_t *pci_dev, uint8_t addr, uint8_t endpoint, void *buffer, uint16_t len);
                    uhci_register_interrupt(pci_dev, addr, ep->endpoint_address & 0x0F, report_buf, 8);
                } else {
                    extern int ehci_register_interrupt(usb_device_t *dev, uint8_t endpoint, void *buffer, uint16_t len);
                    ehci_register_interrupt(usb_dev, ep->endpoint_address & 0x0F, report_buf, 8);
                }
                break; 
            }
        }
        ptr += len;
    }

    usb_register_device(usb_dev);
    
    kfree(desc);
    kfree(cfg);
    kfree(full_cfg);
}

#include "ehci.h"
#include "task.h"

extern void usb_hid_handle_report(usb_device_t *dev, uint8_t *data, int len);

void usb_poll_task() {
    while (1) {
        usb_device_t *dev = usb_devices;
        while (dev) {
            if (dev->qh) {
                ehci_qh_t *qh = (ehci_qh_t *)dev->qh;
                // Se o motor do EHCI parou de executar o overlay (Active bit = 0)
                if (!(qh->overlay.token & (1 << 7))) {
                    // Dado recebido! (Buffer está no QH overlay buffer[0])
                    uint8_t *data = (uint8_t *)qh->overlay.buffer[0];
                    int len = (qh->overlay.token >> 16) & 0x7FFF;
                    int received = 8 - len; 
                    
                    usb_hid_handle_report(dev, data, received);

                    // Re-armar o qTD/TD (Simplificado: apenas reativa o bit Active)
                    if (dev->qh) {
                        if (dev->type == 1 || dev->type == 2) { // KBD/MOUSE
                            // Para EHCI QH
                            ehci_qh_t *eqh = (ehci_qh_t *)dev->qh;
                            eqh->overlay.token |= (1 << 7);
                            eqh->overlay.token = (eqh->overlay.token & ~(0x7FFF << 16)) | (8 << 16);
                            // Para UHCI, o TD já está na Frame List e o hardware limpa o bit Active.
                            // Nota: A estrutura dev->qh precisaria ser adaptada para UHCI se quisermos polling perfeito.
                        }
                    }
                }
            }
            dev = dev->next;
        }
        for(int i=0; i<10; i++) switch_task();
    }
}

void usb_init() {
    serial_print("USB: Initializing USB Stack...\n");

    // Procura por controladores UHCI (Universal Host Controller Interface)
    pci_device_t *uhci = pci_get_usb(0x00);
    if (uhci) {
        serial_print("USB: Found UHCI Controller at PCI ");
        uhci_init(uhci);
    }

    // Procura por controladores EHCI (Enhanced Host Controller Interface)
    pci_device_t *ehci = pci_get_usb(0x20);
    if (ehci) {
        serial_print("USB: Found EHCI Controller at PCI\n");
        ehci_init(ehci);
    }

    // Inicia thread de polling USB
    create_task_named(usb_poll_task, "usb_poll");
}
