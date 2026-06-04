#include "uhci.h"
#include "io.h"
#include "serial.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"

void uhci_init(pci_device_t *dev) {
    // BAR4 (0x20) é comum para UHCI
    uint32_t io_base = pci_read_config(dev->bus, dev->device, dev->function, 0x20) & (~0x3);
    if (io_base == 0) {
        // Tenta BAR0 se BAR4 falhar
        io_base = pci_read_config(dev->bus, dev->device, dev->function, 0x10) & (~0x3);
    }

    serial_print("UHCI: IO Base at ");
    serial_print_hex(io_base);
    serial_print("\n");

    // 1. Reset Global do Controlador
    outw(io_base + UHCI_USBCMD, 0x0004); // GRESET
    for(int i=0; i<1000; i++) asm volatile("pause");
    outw(io_base + UHCI_USBCMD, 0x0000);
    for(int i=0; i<1000; i++) asm volatile("pause");

    // 2. Aloca Frame List (precisa ser alinhada a 4KB)
    uint32_t *frame_list = (uint32_t *)kmalloc_a(4096);
    memset(frame_list, 0, 4096);
    // Preenche com 'Terminado' (Bit 0 = 1)
    for (int i = 0; i < 1024; i++) frame_list[i] = 1;

    // 3. Configura Frame List no Controlador
    outl(io_base + UHCI_FRBASEADD, (uint32_t)frame_list);
    outw(io_base + UHCI_FRNUM, 0);

    // 4. Limpa Status e Ativa interrupções
    outw(io_base + UHCI_USBSTS, 0x003F); // Limpa tudo
    // outw(io_base + UHCI_USBINTR, 0x000F); // Ativa todas as interrupções

    // 5. Inicia o Controlador
    outw(io_base + UHCI_USBCMD, 0x0001); // RUN

    serial_print("UHCI: Controller started.\n");

    // 6. Enumerar Portas (Basicamente Reset)
    for (int p = 0; p < 8; p++) {
        uint32_t port_reg = io_base + UHCI_PORTSC1 + (p * 2);
        uint16_t status = inw(port_reg);
        
        // Verifica se a porta existe (deve retornar algo válido, não 0xFFFF)
        if (status == 0xFFFF) break;

        if (status & 0x0001) { // Device Connected
            serial_print("UHCI: Device detected on port ");
            char num[4]; itoa(p, num, 10); serial_print(num);
            serial_print(" status="); serial_print_hex(status);
            serial_print("\n");
            
            // Reset Port
            outw(port_reg, status | 0x0200); // PR (Port Reset)
            for(int i=0; i<10000; i++) asm volatile("pause");
            outw(port_reg, inw(port_reg) & ~0x0200);
            for(int i=0; i<10000; i++) asm volatile("pause");
        }
    }
}
