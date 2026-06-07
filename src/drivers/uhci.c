#include "uhci.h"
#include "io.h"
#include "serial.h"
#include "kheap.h"
#include "string.h"
#include "uhci.h"
#include "io.h"
#include "serial.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "usb_spec.h"

static uint32_t uhci_io_base = 0;

static void uhci_wait_td(uhci_td_t *td) {
    int timeout = 1000000;
    while (timeout--) {
        uint32_t status = td->status;
        if (!(status & (1 << 23))) return; // Active bit cleared
        if (status & 0x00FF0000) { // Qualquer bit de erro entre 16-23
            serial_print("UHCI: Transfer Error status=");
            serial_print_hex(status);
            serial_print("\n");
            return;
        }
        asm volatile("pause");
    }
    serial_print("UHCI: Transfer Timeout\n");
}

int uhci_send_control(pci_device_t *dev, uint8_t addr, void *setup, void *data, uint16_t len) {
    (void)dev;
    uint32_t io_base = uhci_io_base;

    // 1. Criar TDs
    uhci_td_t *t_setup = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));
    uhci_td_t *t_data = NULL;
    uhci_td_t *t_status = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));

    memset(t_setup, 0, sizeof(uhci_td_t));
    t_setup->link = 1; // Terminado
    t_setup->status = (1 << 23) | (3 << 27); // Active, 3 retries
    // Token: MaxLen (11 bits), DeviceAddr (7 bits), Endpoint (4 bits), DataToggle (1 bit), PID (8 bits)
    // Setup PID = 0x2D
    t_setup->token = ((8 - 1) << 21) | (0 << 15) | (addr << 8) | 0x2D;
    t_setup->buffer = (uint32_t)setup;

    if (len > 0) {
        t_data = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));
        memset(t_data, 0, sizeof(uhci_td_t));
        t_data->link = 1;
        t_data->status = (1 << 23) | (3 << 27);
        // IN PID = 0x69, OUT PID = 0xE1. Para descritores é IN.
        uint8_t pid = 0x69; 
        t_data->token = ((len - 1) << 21) | (1 << 19) | (0 << 15) | (addr << 8) | pid; // Data1
        t_data->buffer = (uint32_t)data;
        t_setup->link = (uint32_t)t_data | 4; // Depth first
    }

    memset(t_status, 0, sizeof(uhci_td_t));
    t_status->link = 1;
    t_status->status = (1 << 23) | (3 << 27);
    // Status é OUT (0xE1) se recebemos dados, ou IN (0x69) se enviamos.
    uint8_t status_pid = (len > 0) ? 0xE1 : 0x69;
    t_status->token = (0x7FF << 21) | (1 << 19) | (0 << 15) | (addr << 8) | status_pid;

    if (t_data) t_data->link = (uint32_t)t_status | 4;
    else t_setup->link = (uint32_t)t_status | 4;

    // 2. Colocar na Frame List (muito simplificado: colocamos no frame atual + 2)
    uint16_t frame = inw(io_base + UHCI_FRNUM) & 0x03FF;
    uint32_t *frame_list = (uint32_t *)inl(io_base + UHCI_FRBASEADD);

    // QH para o controle
    uhci_qh_t *qh = (uhci_qh_t *)kmalloc_a(sizeof(uhci_qh_t));
    qh->head = 1;
    qh->element = (uint32_t)t_setup;

    uint16_t target_frame = (frame + 2) % 1024;
    frame_list[target_frame] = (uint32_t)qh | 2; // Link para QH

    // 3. Esperar
    uhci_wait_td(t_status);

    // Limpar frame list
    frame_list[target_frame] = 1;

    return 0;
}

// Registra polling de interrupção (HID)
int uhci_register_interrupt(pci_device_t *dev, uint8_t addr, uint8_t endpoint, void *buffer, uint16_t len) {
    (void)dev;
    uint32_t io_base = uhci_io_base;

    // 1. Criar TD de interrupção
    uhci_td_t *td = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));
    memset(td, 0, sizeof(uhci_td_t));
    td->link = 1; // Terminado
    td->status = (1 << 23) | (3 << 27); // Active, 3 retries
    // IN PID = 0x69
    td->token = ((len - 1) << 21) | ((endpoint & 0x0F) << 15) | (addr << 8) | 0x69;
    td->buffer = (uint32_t)buffer;

    // 2. QH para este endpoint
    uhci_qh_t *qh = (uhci_qh_t *)kmalloc_a(sizeof(uhci_qh_t));
    qh->head = 1;
    qh->element = (uint32_t)td;

    // 3. Colocar na lista periódica (Frame List)
    // Para simplificar, colocamos em todos os frames para polling de 1ms
    uint32_t *frame_list = (uint32_t *)inl(io_base + UHCI_FRBASEADD);
    for (int i = 0; i < 1024; i++) {
        if (frame_list[i] & 1) { // Frame vazio
            frame_list[i] = (uint32_t)qh | 2;
        } else {
            // Se já houver algo, deveríamos encadear, mas aqui simplificamos
        }
    }

    return 0;
}

void uhci_init(pci_device_t *dev) {
    // BAR4 (0x20) é comum para UHCI
    uint32_t io_base = pci_read_config(dev->bus, dev->device, dev->function, 0x20) & (~0x3);
    if (io_base == 0) {
        io_base = pci_read_config(dev->bus, dev->device, dev->function, 0x10) & (~0x3);
    }
    uhci_io_base = io_base;

    serial_print("UHCI: IO Base at ");
    serial_print_hex(io_base);
    serial_print("\n");

    // Habilitar IO e Bus Master
    uint32_t pci_cmd = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    pci_cmd |= (1 << 0) | (1 << 2);
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, pci_cmd);

    // 1. Reset Global do Controlador
    outw(io_base + UHCI_USBCMD, 0x0004); // GRESET
    for(int i=0; i<1000; i++) asm volatile("pause");
    outw(io_base + UHCI_USBCMD, 0x0000);
    for(int i=0; i<1000; i++) asm volatile("pause");

    // 2. Aloca Frame List (precisa ser alinhada a 4KB)
    uint32_t *frame_list = (uint32_t *)kmalloc_a(4096);
    memset(frame_list, 0, 4096);
    for (int i = 0; i < 1024; i++) frame_list[i] = 1;

    // 3. Configura Frame List
    outl(io_base + UHCI_FRBASEADD, (uint32_t)frame_list);
    outw(io_base + UHCI_FRNUM, 0);

    // 4. Limpa Status
    outw(io_base + UHCI_USBSTS, 0x003F);

    // 5. Inicia o Controlador
    outw(io_base + UHCI_USBCMD, 0x0001); // RUN

    serial_print("UHCI: Controller started.\n");

    // 6. Enumerar Portas
    for (int p = 0; p < 2; p++) {
        uint32_t port_reg = io_base + UHCI_PORTSC1 + (p * 2);
        uint16_t status = inw(port_reg);

        if (status & 0x0001) { // Device Connected
            serial_print("UHCI: Device detected on port ");
            char num[4]; itoa(p, num, 10); serial_print(num);
            serial_print(" status="); serial_print_hex(status);
            serial_print("\n");

            // Reset Port
            outw(port_reg, status | 0x0200); // PR
            for(int i=0; i<50000; i++) asm volatile("pause");
            outw(port_reg, inw(port_reg) & ~0x0200);
            for(int i=0; i<50000; i++) asm volatile("pause");

            extern void usb_enumerate(void *controller, uint8_t port, int type);
            usb_enumerate(dev, (uint8_t)p, 1); // 1 = UHCI
        }
    }
}
