#include "pci.h"
#include "io.h"
#include "kheap.h"

static pci_device_t* devices[32];
static int pci_count = 0;

uint32_t pci_read_config(uint16_t bus, uint16_t device, uint16_t function, uint16_t offset) {
    uint32_t address = (uint32_t)((((uint32_t)bus) << 16) | (((uint32_t)device) << 11) | (((uint32_t)function) << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_init() {
    pci_count = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint16_t dev = 0; dev < 32; dev++) {
            uint32_t val = pci_read_config(bus, dev, 0, 0);
            if ((val & 0xFFFF) != 0xFFFF) {
                pci_device_t* d = (pci_device_t*)kmalloc(sizeof(pci_device_t));
                d->bus = bus; d->device = dev; d->function = 0;
                d->vendor_id = val & 0xFFFF;
                d->device_id = (val >> 16) & 0xFFFF;
                uint32_t class_reg = pci_read_config(bus, dev, 0, 0x08);
                d->class_id = (class_reg >> 24) & 0xFF;
                d->subclass_id = (class_reg >> 16) & 0xFF;
                
                uint32_t int_reg = pci_read_config(bus, dev, 0, 0x3C);
                d->interrupt_line = int_reg & 0xFF;

                if (pci_count < 32) devices[pci_count++] = d;
            }
        }
    }
}

pci_device_t* pci_get_gpu() {
    for(int i=0; i<pci_count; i++) if (devices[i]->class_id == 0x03) return devices[i];
    return (void*)0;
}

pci_device_t* pci_get_net() {
    for(int i=0; i<pci_count; i++) if (devices[i]->class_id == 0x02 && devices[i]->subclass_id == 0x00) return devices[i];
    return (void*)0;
}

pci_device_t* pci_get_wireless() {
    for(int i=0; i<pci_count; i++) {
        // Class 0x02 Subclass 0x80 (Other) or 0x11 (Wireless)
        if (devices[i]->class_id == 0x02 && (devices[i]->subclass_id == 0x80 || devices[i]->subclass_id == 0x11)) {
            return devices[i];
        }
    }
    return (void*)0;
}