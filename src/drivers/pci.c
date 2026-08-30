#include "pci.h"
#include "io.h"
#include "kheap.h"
#include "serial.h"

static pci_device_t *devices[32];
static int pci_count = 0;

uint32_t pci_read_config(uint16_t bus, uint16_t device, uint16_t function,
                         uint16_t offset) {
  uint32_t address =
      (uint32_t)((((uint32_t)bus) << 16) | (((uint32_t)device) << 11) |
                 (((uint32_t)function) << 8) | (offset & 0xFC) |
                 ((uint32_t)0x80000000));
  outl(0xCF8, address);
  return inl(0xCFC);
}

void pci_write_config(uint16_t bus, uint16_t device, uint16_t function,
                          uint16_t offset, uint32_t val) {
  uint32_t address =
      (uint32_t)((((uint32_t)bus) << 16) | (((uint32_t)device) << 11) |
                 (((uint32_t)function) << 8) | (offset & 0xFC) |
                 ((uint32_t)0x80000000));
  outl(0xCF8, address);
  outl(0xCFC, val);
}

void pci_init() {
  pci_count = 0;
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint16_t dev = 0; dev < 32; dev++) {
      uint32_t val = pci_read_config(bus, dev, 0, 0);
      if ((val & 0xFFFF) == 0xFFFF) continue;

      // Check header type for multi-function support (byte at offset 0x0E)
      uint32_t htype_reg = pci_read_config(bus, dev, 0, 0x0C);
      uint8_t funcs = (htype_reg & 0x800000) ? 8 : 1;

      for (uint8_t fn = 0; fn < funcs; fn++) {
        val = pci_read_config(bus, dev, fn, 0);
        if ((val & 0xFFFF) == 0xFFFF) continue;

        pci_device_t *d = (pci_device_t *)kmalloc(sizeof(pci_device_t));
        d->bus = bus;
        d->device = dev;
        d->function = fn;
        d->vendor_id = val & 0xFFFF;
        d->device_id = (val >> 16) & 0xFFFF;
        uint32_t class_reg = pci_read_config(bus, dev, fn, 0x08);
        d->class_id = (class_reg >> 24) & 0xFF;
        d->subclass_id = (class_reg >> 16) & 0xFF;

        if (d->class_id == 0x0C && d->subclass_id == 0x03) {
            uint8_t pi = (class_reg >> 8) & 0xFF;
            serial_print("PCI: Found USB Controller (");
            if (pi == 0x00) serial_print("UHCI");
            else if (pi == 0x10) serial_print("OHCI");
            else if (pi == 0x20) serial_print("EHCI");
            else if (pi == 0x30) serial_print("xHCI");
            else serial_print("Unknown");
            serial_print(")\n");
        }

        uint32_t int_reg = pci_read_config(bus, dev, fn, 0x3C);
        d->interrupt_line = int_reg & 0xFF;

        if (d->vendor_id == 0x1AF4) {
          serial_print("PCI: FOUND VIRTIO DEVICE!\n");
          if (d->device_id == 0x1050) {
            serial_print("PCI: IT IS THE GPU (VirtIO-GPU)! skipping auto-init to keep UI...\n");
          }
        }

        if (pci_count < 32)
          devices[pci_count++] = d;
      }
    }
  }
}

pci_device_t *pci_get_gpu() {
  for (int i = 0; i < pci_count; i++)
    if (devices[i]->class_id == 0x03)
      return devices[i];
  return (void *)0;
}

pci_device_t *pci_get_device(uint16_t vendor_id, uint16_t device_id) {
  for (int i = 0; i < pci_count; i++) {
    if (devices[i]->vendor_id == vendor_id &&
        devices[i]->device_id == device_id)
      return devices[i];
  }
  return (void *)0;
}

pci_device_t *pci_get_net() {
  for (int i = 0; i < pci_count; i++)
    if (devices[i]->class_id == 0x02 && devices[i]->subclass_id == 0x00)
      return devices[i];
  return (void *)0;
}

pci_device_t *pci_get_wireless() {
  for (int i = 0; i < pci_count; i++) {
    // Class 0x02 Subclass 0x80 (Other) or 0x11 (Wireless)
    if (devices[i]->class_id == 0x02 &&
        (devices[i]->subclass_id == 0x80 || devices[i]->subclass_id == 0x11)) {
      return devices[i];
    }
  }
  return (void *)0;
}

pci_device_t *pci_get_usb(uint8_t interface_type) {
  for (int i = 0; i < pci_count; i++) {
    // Class 0x0C (Serial Bus), Subclass 0x03 (USB)
    if (devices[i]->class_id == 0x0C && devices[i]->subclass_id == 0x03) {
      // Opcionalmente podemos filtrar por interface (UHCI=0x00, OHCI=0x10, EHCI=0x20, xHCI=0x30)
      if (interface_type == 0xFF) return devices[i];
      
      uint32_t class_reg = pci_read_config(devices[i]->bus, devices[i]->device, devices[i]->function, 0x08);
      uint8_t prog_if = (class_reg >> 8) & 0xFF;
      if (prog_if == interface_type) return devices[i];
    }
  }
  return (void *)0;
}

pci_device_t *pci_get_ahci() {
  for (int i = 0; i < pci_count; i++) {
    if (devices[i]->class_id == 0x01 && devices[i]->subclass_id == 0x06) {
      uint32_t class_reg = pci_read_config(devices[i]->bus, devices[i]->device, devices[i]->function, 0x08);
      uint8_t prog_if = (class_reg >> 8) & 0xFF;
      if (prog_if == 0x01) { // AHCI
        return devices[i];
      }
    }
  }
  return (void *)0;
}

pci_device_t *pci_get_audio() {
  for (int i = 0; i < pci_count; i++) {
    // Class 0x04 (Multimedia), Subclass 0x01 (Audio)
    if (devices[i]->class_id == 0x04 && devices[i]->subclass_id == 0x01) {
      return devices[i];
    }
  }
  return (void *)0;
}
