#include "ehci.h"
#include "usb.h"
#include "usb_spec.h"
#include "io.h"
#include "serial.h"
#include "kheap.h"
#include "string.h"
#include "vmm.h"

static ehci_qh_t *async_list = NULL;
static uint32_t ehci_op_base = 0;

static void ehci_wait_qtd(ehci_qtd_t *qtd) {
    int timeout = 1000000;
    while (timeout--) {
        uint32_t token = qtd->token;
        if (!(token & (1 << 7))) return; // Active bit cleared
        if (token & (1 << 6) || (token & (1 << 5)) || (token & (1 << 4))) {
            serial_print("EHCI: Transfer Error token=");
            serial_print_hex(token);
            serial_print("\n");
            return;
        }
        asm volatile("pause");
    }
    serial_print("EHCI: Transfer Timeout\n");
}

int ehci_send_control(uint8_t addr, usb_setup_packet_t *setup, void *data, uint16_t len) {
    // 1. Criar QH para o dispositivo se não existir (aqui simplificado: usamos um QH global)
    static ehci_qh_t *ctrl_qh = NULL;
    if (!ctrl_qh) {
        ctrl_qh = (ehci_qh_t *)kmalloc_a(sizeof(ehci_qh_t));
        memset(ctrl_qh, 0, sizeof(ehci_qh_t));
        ctrl_qh->horizontal_link = ((uint32_t)async_list) | 2;
        ctrl_qh->endpoint_char = (2 << 12) | (64 << 16) | (1 << 14); // High Speed, MaxPkt 64, DTC
        async_list->horizontal_link = ((uint32_t)ctrl_qh) | 2;
    }

    // Atualiza endereço do dispositivo no QH
    ctrl_qh->endpoint_char = (ctrl_qh->endpoint_char & ~0x7F) | (addr & 0x7F);

    // 2. Criar qTDs
    ehci_qtd_t *q_setup = (ehci_qtd_t *)kmalloc_a(sizeof(ehci_qtd_t));
    ehci_qtd_t *q_data = NULL;
    ehci_qtd_t *q_status = (ehci_qtd_t *)kmalloc_a(sizeof(ehci_qtd_t));

    memset(q_setup, 0, sizeof(ehci_qtd_t));
    q_setup->next_qtd = 1;
    q_setup->alt_next_qtd = 1;
    q_setup->token = (0 << 8) | (8 << 16) | (1 << 7) | (2 << 10); // PID Setup, Len 8, Active, C_ERR 3
    q_setup->buffer[0] = (uint32_t)setup;

    if (len > 0) {
        q_data = (ehci_qtd_t *)kmalloc_a(sizeof(ehci_qtd_t));
        memset(q_data, 0, sizeof(ehci_qtd_t));
        q_data->next_qtd = 1;
        q_data->alt_next_qtd = 1;
        // PID IN (1) ou OUT (0). Para GET_DESCRIPTOR é IN.
        uint8_t pid = (setup->request_type & 0x80) ? 1 : 0;
        q_data->token = (pid << 8) | (len << 16) | (1 << 7) | (3 << 10) | (1 << 31); // Data1
        q_data->buffer[0] = (uint32_t)data;
        q_setup->next_qtd = (uint32_t)q_data;
    }

    memset(q_status, 0, sizeof(ehci_qtd_t));
    q_status->next_qtd = 1;
    q_status->alt_next_qtd = 1;
    // Status é o inverso do dado. Se dado foi IN, status é OUT.
    uint8_t status_pid = (len == 0 || !(setup->request_type & 0x80)) ? 1 : 0;
    q_status->token = (status_pid << 8) | (0 << 16) | (1 << 7) | (3 << 10) | (1 << 31); // Data1

    if (q_data) q_data->next_qtd = (uint32_t)q_status;
    else q_setup->next_qtd = (uint32_t)q_status;

    // 3. Submeter ao QH
    ctrl_qh->overlay.next_qtd = (uint32_t)q_setup;
    ctrl_qh->current_qtd = 0;

    // 4. Esperar status
    ehci_wait_qtd(q_status);

    // TODO: Free qTDs
    return 0;
}

// QH para interrupções (simplificado: na lista assíncrona)
int ehci_register_interrupt(usb_device_t *dev, uint8_t endpoint, void *buffer, uint16_t len) {
    ehci_qh_t *qh = (ehci_qh_t *)kmalloc_a(sizeof(ehci_qh_t));
    memset(qh, 0, sizeof(ehci_qh_t));

    // Endereço do dispositivo e Endpoint
    qh->endpoint_char = (dev->address & 0x7F) | ((endpoint & 0xF) << 8) | (2 << 12) | (64 << 16);
    qh->endpoint_caps = (1 << 30); // 1 dword transfer
    
    // Link na lista assíncrona
    qh->horizontal_link = async_list->horizontal_link;
    async_list->horizontal_link = ((uint32_t)qh) | 2;

    dev->qh = qh;

    // Cria qTD perpétuo (re-ativado pelo driver)
    ehci_qtd_t *qtd = (ehci_qtd_t *)kmalloc_a(sizeof(ehci_qtd_t));
    memset(qtd, 0, sizeof(ehci_qtd_t));
    qtd->next_qtd = 1;
    qtd->alt_next_qtd = 1;
    qtd->token = (1 << 8) | (len << 16) | (1 << 7) | (3 << 10); // PID IN, Active
    qtd->buffer[0] = (uint32_t)buffer;

    qh->overlay.next_qtd = (uint32_t)qtd;

    return 0;
}

void ehci_init(pci_device_t *dev) {
    uint32_t bar = pci_read_config(dev->bus, dev->device, dev->function, 0x10) & ~0xF;
    
    // Mapear o BAR no VMM (Identity Mapping) para permitir acesso MMIO
    vmm_map_page((void*)(bar & 0xFFFFF000), (void*)(bar & 0xFFFFF000), 0x3);
    vmm_map_page((void*)((bar & 0xFFFFF000) + 4096), (void*)((bar & 0xFFFFF000) + 4096), 0x3);

    uint8_t cap_len = *(uint8_t*)bar;
    ehci_op_base = bar + cap_len;
    uint32_t op_base = ehci_op_base;

    serial_print("EHCI: MMIO Base at "); serial_print_hex(bar);
    serial_print(" Op Base at "); serial_print_hex(op_base);
    serial_print("\n");

    // 1. Reset Host Controller
    *(uint32_t*)(op_base + EHCI_USBCMD) |= (1 << 1);
    while (*(uint32_t*)(op_base + EHCI_USBCMD) & (1 << 1)) {
        asm volatile("pause");
    }
    serial_print("EHCI: Reset completed.\n");

    // 2. Criar lista assíncrona (Queue Heads)
    async_list = (ehci_qh_t *)kmalloc_a(sizeof(ehci_qh_t));
    memset(async_list, 0, sizeof(ehci_qh_t));
    
    // QH estático para fechar o loop
    async_list->horizontal_link = ((uint32_t)async_list) | 2; // Tipo QH (2)
    async_list->endpoint_char = (1 << 15); // H (Head of reclamation list)
    async_list->overlay.next_qtd = 1; // Terminado
    async_list->overlay.alt_next_qtd = 1;

    // 3. Configurar endereços no controlador
    *(uint32_t*)(op_base + EHCI_ASYNCLIST) = (uint32_t)async_list;

    // 4. Habilitar o controlador
    uint32_t cmd = *(uint32_t*)(op_base + EHCI_USBCMD);
    cmd |= (1 << 0); // RS (Run/Stop)
    cmd |= (1 << 5); // ASE (Async Schedule Enable)
    *(uint32_t*)(op_base + EHCI_USBCMD) = cmd;

    // 5. Route all ports to EHCI
    *(uint32_t*)(op_base + EHCI_CONFIGFLAG) = 1;

    serial_print("EHCI: Controller is RUNNING.\n");

    // 6. Verificar portas
    uint32_t hcsparams = *(uint32_t*)(bar + EHCI_HCSPARAMS);
    int n_ports = hcsparams & 0x0F;
    
    for (int i = 0; i < n_ports; i++) {
        uint32_t port_status = *(uint32_t*)(op_base + EHCI_PORTSC + (i * 4));
        if (port_status & 1) { // Connect Status
            serial_print("EHCI: Device found on port ");
            char n[4]; itoa(i, n, 10); serial_print(n);
            serial_print("\n");
            
            // Reset Port
            *(uint32_t*)(op_base + EHCI_PORTSC + (i * 4)) |= (1 << 8); // PR
            for(int j=0; j<100000; j++) asm volatile("pause");
            *(uint32_t*)(op_base + EHCI_PORTSC + (i * 4)) &= ~(1 << 8);
            for(int j=0; j<100000; j++) asm volatile("pause");

            extern void usb_enumerate(void *controller, uint8_t port);
            usb_enumerate(dev, (uint8_t)i);
        }
    }
}
