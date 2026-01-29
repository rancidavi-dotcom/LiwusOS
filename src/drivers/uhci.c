#include "uhci.h"
#include "io.h"
#include "serial.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "usb_spec.h"

static uint32_t uhci_io_base = 0;

/* ── Multi-slot interrupt support (keyboard + mouse) ── */
static uhci_td_t *int_td[UHCI_MAX_INT_SLOTS]   = {NULL, NULL};
static void      *int_buf[UHCI_MAX_INT_SLOTS]   = {NULL, NULL};
static int        int_len[UHCI_MAX_INT_SLOTS]   = {0, 0};
static uint8_t    int_type[UHCI_MAX_INT_SLOTS]   = {0, 0}; // 1=kbd, 2=mouse
static int        int_count = 0;

/* ── Slot-based accessors ── */
int uhci_get_int_slot_count(void) { return int_count; }

uhci_td_t *uhci_get_int_td(int slot) {
    if (slot < 0 || slot >= int_count) return NULL;
    return int_td[slot];
}

void *uhci_get_int_buffer(int slot) {
    if (slot < 0 || slot >= int_count) return NULL;
    return int_buf[slot];
}

int uhci_get_int_len(int slot) {
    if (slot < 0 || slot >= int_count) return 0;
    return int_len[slot];
}

uint8_t uhci_get_int_dev_type(int slot) {
    if (slot < 0 || slot >= int_count) return 0;
    return int_type[slot];
}

void uhci_set_int_dev_type(int slot, uint8_t type) {
    if (slot >= 0 && slot < UHCI_MAX_INT_SLOTS) int_type[slot] = type;
}

void uhci_rearm_int(int slot) {
    if (slot >= 0 && slot < int_count && int_td[slot]) {
        int_td[slot]->status = (1 << 23) | (3 << 16);
    }
}

/* ── Legacy accessors (slot 0) ── */
uhci_td_t *uhci_get_interrupt_td(void)  { return uhci_get_int_td(0); }
void *uhci_get_interrupt_buffer(void)   { return uhci_get_int_buffer(0); }
int uhci_get_interrupt_len(void)        { return uhci_get_int_len(0); }
void uhci_rearm_interrupt(void)         { uhci_rearm_int(0); }

/* ── Rebuild frame list: chain all active interrupt TDs ── */
static void uhci_rebuild_frame_list(void) {
    uint32_t *frame_list = (uint32_t *)inl(uhci_io_base + UHCI_FRBASEADD);
    if (!frame_list) return;

    /* Chain: TD[0] -> TD[1] -> ... -> Terminate */
    for (int s = 0; s < int_count; s++) {
        if (s < int_count - 1 && int_td[s + 1]) {
            int_td[s]->link = (uint32_t)int_td[s + 1]; // next TD (bit 0=0 = TD)
        } else {
            int_td[s]->link = 1; // Terminate
        }
    }

    /* All frames point to the head of the chain */
    uint32_t head = (int_count > 0 && int_td[0]) ? (uint32_t)int_td[0] : 1;
    for (int i = 0; i < 1024; i++) {
        frame_list[i] = head;
    }
}

int uhci_send_control(pci_device_t *dev, uint8_t addr, void *setup, void *data, uint16_t len) {
    (void)dev;
    uint32_t io_base = uhci_io_base;

    // 1. Criar TDs
    uhci_td_t *t_setup = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));
    uhci_td_t *t_data = NULL;
    uhci_td_t *t_status = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));

    memset(t_setup, 0, sizeof(uhci_td_t));
    t_setup->status = (1 << 23) | (3 << 16);
    t_setup->token = ((8 - 1) << 21) | (0 << 20) | (0 << 15) | (addr << 8) | 0x2D;
    t_setup->buffer = (uint32_t)setup;

    if (len > 0) {
        t_data = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));
        memset(t_data, 0, sizeof(uhci_td_t));
        t_data->status = (1 << 23) | (3 << 16);
        uint8_t pid = 0x69;
        t_data->token = ((len - 1) << 21) | (1 << 20) | (0 << 15) | (addr << 8) | pid;
        t_data->buffer = (uint32_t)data;
        t_data->link = 1;
        t_setup->link = (uint32_t)t_data | 4;
    }

    memset(t_status, 0, sizeof(uhci_td_t));
    t_status->link = 1;
    t_status->status = (1 << 23) | (3 << 16);
    uint8_t status_pid = (len > 0) ? 0xE1 : 0x69;
    t_status->token = (0x7FF << 21) | (1 << 20) | (0 << 15) | (addr << 8) | status_pid;

    if (t_data) t_data->link = (uint32_t)t_status | 4;
    else t_setup->link = (uint32_t)t_status | 4;

    // 2. Colocar na Frame List (sem QH - TD direto)
    uint16_t frame = inw(io_base + UHCI_FRNUM) & 0x03FF;
    uint32_t *frame_list = (uint32_t *)inl(io_base + UHCI_FRBASEADD);

    uint16_t target_frame = (frame + 2) % 1024;
    frame_list[target_frame] = (uint32_t)t_setup; // TD direto (bit 1 = 0)

    // 3. Esperar
    for (int timeout = 5000000; timeout > 0; timeout--) {
        uint32_t s = t_status->status;
        if (!(s & (1 << 23))) return 0;
        if (s & 0x1D000000) break;
        asm volatile("pause");
    }

    // Limpar frame list
    frame_list[target_frame] = 1;

    serial_print("UHCI: send_control FAIL status=");
    serial_print_hex(t_status->status);
    serial_print("\n");
    return -1;
}

// Registra polling de interrupção (HID) - agora com suporte a múltiplos slots
int uhci_register_interrupt(pci_device_t *dev, uint8_t addr, uint8_t endpoint, void *buffer, uint16_t len) {
    (void)dev;

    if (int_count >= UHCI_MAX_INT_SLOTS) {
        serial_print("UHCI: Max interrupt slots reached!\n");
        return -1;
    }

    int slot = int_count;

    uhci_td_t *td = (uhci_td_t *)kmalloc_a(sizeof(uhci_td_t));
    memset(td, 0, sizeof(uhci_td_t));
    td->link = 1;
    td->status = (1 << 23) | (3 << 16); // Active, Low Speed (3 errors)
    td->token = ((len - 1) << 21) | ((endpoint & 0x0F) << 15) | (addr << 8) | 0x69;
    td->buffer = (uint32_t)buffer;

    int_td[slot] = td;
    int_buf[slot] = buffer;
    int_len[slot] = len;
    int_type[slot] = 0; // Caller will set type via usb_enumerate
    int_count++;

    serial_print("UHCI: Registered interrupt slot ");
    char sstr[4]; itoa(slot, sstr, 10); serial_print(sstr);
    serial_print(" addr="); serial_print_hex(addr);
    serial_print(" ep="); serial_print_hex(endpoint);
    serial_print("\n");

    uhci_rebuild_frame_list();
    return slot;
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
            serial_print("UHCI: Resetting port...\n");
            outw(port_reg, status | 0x0200); // PR (bit 9)
            for(int i=0; i<100000; i++) asm volatile("pause");
            outw(port_reg, inw(port_reg) & ~0x0200);
            for(int i=0; i<500000; i++) asm volatile("pause");

            // Verificar status pós-reset
            uint16_t post_reset = inw(port_reg);
            serial_print("UHCI: Post-reset status="); serial_print_hex(post_reset); serial_print("\n");

            // Se porta não foi habilitada, habilitar manualmente
            if (!(post_reset & 0x0004)) {
                serial_print("UHCI: Enabling port manually...\n");
                outw(port_reg, post_reset | 0x0004);
                for(int i=0; i<10000; i++) asm volatile("pause");
                uint16_t enabled = inw(port_reg);
                serial_print("UHCI: After enable status="); serial_print_hex(enabled); serial_print("\n");
            }

            extern void usb_enumerate(void *controller, uint8_t port, int type);
            usb_enumerate(dev, (uint8_t)p, 1); // 1 = UHCI
        }
    }
}
