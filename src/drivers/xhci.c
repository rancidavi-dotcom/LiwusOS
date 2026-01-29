#include "xhci.h"
#include "usb.h"
#include "usb_spec.h"
#include "serial.h"
#include "kheap.h"
#include "string.h"
#include "vmm.h"

/* ── Registradores de capacidade (offset a partir do BAR0) ── */
#define XHCI_HCSPARAMS1   0x04
#define XHCI_HCCPARAMS1   0x10
#define XHCI_DBOFF        0x14
#define XHCI_RTSOFF       0x18

/* ── Registradores operacionais (offset a partir de BAR + CAPLENGTH) ── */
#define XHCI_USBCMD       0x00
#define XHCI_USBSTS       0x04
#define XHCI_CRCR         0x18
#define XHCI_DCBAAP       0x30
#define XHCI_CONFIG       0x38
#define XHCI_PORT_REGS    0x400

/* ── Interrupter 0 (offset a partir de BAR + RTSOFF) ── */
#define XHCI_IR0          0x20
#define XHCI_IMAN         0x00
#define XHCI_ERSTSZ       0x08
#define XHCI_ERSTBA       0x10
#define XHCI_ERDP         0x18

#define USBCMD_RS         (1u << 0)
#define USBCMD_HCRST      (1u << 1)
#define USBSTS_HCH        (1u << 0)
#define USBSTS_CNR        (1u << 11)

#define PORTSC_CCS        (1u << 0)
#define PORTSC_PED        (1u << 1)
#define PORTSC_PR         (1u << 4)
#define PORTSC_PP         (1u << 9)
#define PORTSC_SPEED_SHIFT 10
#define PORTSC_SPEED_MASK  0xF
#define PORTSC_CSC        (1u << 17)
#define PORTSC_PEC        (1u << 18)
#define PORTSC_WRC        (1u << 19)
#define PORTSC_OCC        (1u << 20)
#define PORTSC_PRC        (1u << 21)
#define PORTSC_PLC        (1u << 22)
#define PORTSC_CEC        (1u << 23)

#define PORTSC_CHANGE_MASK (PORTSC_CSC | PORTSC_PEC | PORTSC_WRC | PORTSC_OCC | \
                            PORTSC_PRC | PORTSC_PLC | PORTSC_CEC)

#define TRB_NORMAL        1
#define TRB_SETUP         2
#define TRB_DATA          3
#define TRB_STATUS        4
#define TRB_LINK          6
#define TRB_ENABLE_SLOT   9
#define TRB_ADDRESS_DEV   11
#define TRB_CONFIG_EP     12
#define TRB_EVALUATE_CTX  13
#define TRB_TRANSFER_EV   32
#define TRB_CMD_COMPLETE  33
#define TRB_PORT_CHANGE   34

#define XHCI_EP_CONTROL   4
#define XHCI_EP_INTR_IN   7

#define XHCI_TRB_TYPE(c)  (((c) >> 10) & 0x3F)

#define XHCI_CMD_TRBS     64
#define XHCI_EVT_TRBS     128
#define XHCI_EP_TRBS      32
#define XHCI_MAX_SLOTS    64

typedef struct {
    xhci_trb_t *trbs;
    uint64_t phys;
    uint32_t size;
    uint32_t enqueue;
    uint32_t cycle;
} xhci_ring_t;

typedef struct {
    int used;
    uint8_t slot_id;
    uint8_t port;
    uint8_t speed;
    uint8_t ep_in_dci;
    uint16_t int_len;
    int int_outstanding;
    int int_error_logged;
    xhci_ring_t ep0;
    xhci_ring_t intr;
    uint8_t *int_buffer;
    uint8_t *dev_ctx;
    uint8_t *in_ctx;
    usb_device_t *udev;
} xhci_slot_t;

static uint64_t xhci_bar;
static uint64_t xhci_op;
static uint64_t xhci_db;
static uint64_t xhci_rt;
static uint32_t xhci_ctx_stride = 32;
static uint32_t xhci_max_slots;
static uint32_t xhci_max_ports;
static int xhci_ready;
static pci_device_t *xhci_pci;

static uint8_t *xhci_dcbaa;
static xhci_ring_t xhci_cmd;
static xhci_ring_t xhci_evt;
static xhci_slot_t xhci_slots[XHCI_MAX_SLOTS + 1];

extern void usb_hid_handle_report(usb_device_t *dev, uint8_t *data, int len);
extern void usb_register_device(usb_device_t *dev);

static void xhci_handle_transfer_event(xhci_trb_t *ev);

static inline uint32_t mmio_r32(uint64_t a) {
    return *(volatile uint32_t *)(uintptr_t)a;
}
static inline void mmio_w32(uint64_t a, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)a = v;
}
static inline void mmio_w64(uint64_t a, uint64_t v) {
    mmio_w32(a, (uint32_t)v);
    mmio_w32(a + 4, (uint32_t)(v >> 32));
}

static void xhci_delay(uint32_t n) {
    for (volatile uint32_t i = 0; i < n; i++)
        asm volatile("pause");
}

static inline void xhci_doorbell(uint32_t slot, uint32_t target) {
    mmio_w32(xhci_db + (uint64_t)slot * 4, target);
}

static void xhci_ring_init(xhci_ring_t *r, void *mem, uint32_t total) {
    r->trbs = (xhci_trb_t *)mem;
    r->phys = (uint64_t)(uintptr_t)mem;
    r->size = total;
    r->enqueue = 0;
    r->cycle = 1;
}

static void xhci_ring_write(xhci_ring_t *r, uint64_t param, uint32_t status,
                            uint32_t control) {
    if (r->enqueue == r->size - 1) {
        xhci_trb_t *l = &r->trbs[r->enqueue];
        l->param = r->phys;
        l->status = 0;
        l->control = (TRB_LINK << 10) | (1u << 1) | r->cycle;
        r->enqueue = 0;
        r->cycle ^= 1u;
    }
    xhci_trb_t *t = &r->trbs[r->enqueue++];
    t->param = param;
    t->status = status;
    t->control = control | r->cycle;
}

static int xhci_poll_event(xhci_trb_t *out) {
    xhci_trb_t *t = &xhci_evt.trbs[xhci_evt.enqueue];
    if ((t->control & 1u) != xhci_evt.cycle)
        return 0;
    *out = *t;
    xhci_evt.enqueue++;
    if (xhci_evt.enqueue >= xhci_evt.size) {
        xhci_evt.enqueue = 0;
        xhci_evt.cycle ^= 1u;
    }
    uint64_t erdp = xhci_evt.phys + (uint64_t)xhci_evt.enqueue * 16;
    mmio_w32(xhci_rt + XHCI_IR0 + XHCI_ERDP,
             (uint32_t)(erdp & ~0xFULL) | (1u << 3));
    mmio_w32(xhci_rt + XHCI_IR0 + XHCI_ERDP + 4, (uint32_t)(erdp >> 32));
    return 1;
}

static int xhci_wait_cmd(uint8_t *slot_out) {
    for (uint32_t spins = 0; spins < 30000000u; spins++) {
        xhci_trb_t ev;
        while (xhci_poll_event(&ev)) {
            uint32_t type = XHCI_TRB_TYPE(ev.control);
            if (type == TRB_CMD_COMPLETE) {
                if (slot_out)
                    *slot_out = (uint8_t)((ev.control >> 24) & 0xFF);
                uint32_t code = (ev.status >> 24) & 0xFF;
                return (code == 1) ? 0 : -(int)code;
            } else if (type == TRB_TRANSFER_EV) {
                xhci_handle_transfer_event(&ev);
            }
        }
        asm volatile("pause");
    }
    return -1000;
}

static int xhci_submit_cmd(uint32_t type, uint64_t param, uint8_t slot,
                           uint8_t *slot_out) {
    xhci_ring_write(&xhci_cmd, param, 0,
                    (type << 10) | ((uint32_t)slot << 24));
    xhci_doorbell(0, 0);
    return xhci_wait_cmd(slot_out);
}

static int xhci_wait_transfer(uint8_t slot, uint8_t dci, uint32_t *residual) {
    for (uint32_t spins = 0; spins < 30000000u; spins++) {
        xhci_trb_t ev;
        while (xhci_poll_event(&ev)) {
            uint32_t type = XHCI_TRB_TYPE(ev.control);
            if (type == TRB_TRANSFER_EV) {
                uint8_t s = (uint8_t)((ev.control >> 24) & 0xFF);
                uint8_t e = (uint8_t)((ev.control >> 16) & 0x1F);
                if (s == slot && e == dci) {
                    uint32_t code = (ev.status >> 24) & 0xFF;
                    if (residual)
                        *residual = ev.status & 0xFFFFFF;
                    if (code == 1 || code == 13)
                        return 0;
                    return -(int)code;
                }
                xhci_handle_transfer_event(&ev);
            }
        }
        asm volatile("pause");
    }
    return -1000;
}

static uint32_t *xhci_ctx(uint8_t *base, uint32_t index) {
    return (uint32_t *)(base + (uint64_t)index * xhci_ctx_stride);
}

static uint16_t xhci_ep0_mps(uint8_t speed) {
    switch (speed) {
        case 3: return 64;
        case 4: return 512;
        default: return 8;
    }
}

static void xhci_submit_intr(xhci_slot_t *s) {
    if (s->ep_in_dci == 0)
        return;
    xhci_ring_write(&s->intr, (uint64_t)(uintptr_t)s->int_buffer, s->int_len,
                    (TRB_NORMAL << 10) | (1u << 5) | (1u << 2));
    s->int_outstanding = 1;
    xhci_doorbell(s->slot_id, s->ep_in_dci);
}

static void xhci_handle_transfer_event(xhci_trb_t *ev) {
    uint8_t slot = (uint8_t)((ev->control >> 24) & 0xFF);
    uint8_t ep = (uint8_t)((ev->control >> 16) & 0x1F);
    uint32_t code = (ev->status >> 24) & 0xFF;

    if (slot == 0 || slot > XHCI_MAX_SLOTS)
        return;
    xhci_slot_t *s = &xhci_slots[slot];
    if (!s->used || !s->udev)
        return;
    if (ep != s->ep_in_dci || !s->int_outstanding)
        return;

    uint32_t residual = ev->status & 0xFFFFFF;
    int received = (int)s->int_len - (int)residual;
    if (received < 0)
        received = 0;

    if (code == 1 || code == 13) {
        if (received > 0)
            usb_hid_handle_report(s->udev, s->int_buffer, received);
        s->int_error_logged = 0;
    } else if (!s->int_error_logged) {
        serial_print("XHCI: interrupt transfer error code=");
        serial_print_hex(code);
        serial_print("\n");
        s->int_error_logged = 1;
    }

    s->int_outstanding = 0;
    xhci_submit_intr(s);
}

static int xhci_control(uint8_t slot, usb_setup_packet_t *setup, void *data,
                        uint16_t len) {
    xhci_slot_t *s = &xhci_slots[slot];
    if (!s->used)
        return -1;

    uint64_t setup_word = 0;
    memcpy(&setup_word, setup, 8);

    uint32_t trt = 0;
    if (len > 0)
        trt = (setup->request_type & 0x80) ? 2u : 3u;

    xhci_ring_write(&s->ep0, setup_word, 8,
                    (TRB_SETUP << 10) | (1u << 6) | (trt << 16));

    if (len > 0) {
        uint32_t dir = (setup->request_type & 0x80) ? (1u << 16) : 0u;
        xhci_ring_write(&s->ep0, (uint64_t)(uintptr_t)data, len & 0x1FFFF,
                        (TRB_DATA << 10) | dir);
    }

    uint32_t status_dir =
        (uint32_t)((len == 0 || !(setup->request_type & 0x80)) ? (1u << 16) : 0u);
    xhci_ring_write(&s->ep0, 0, 0,
                    (TRB_STATUS << 10) | status_dir | (1u << 5));

    xhci_doorbell(slot, 1);
    return xhci_wait_transfer(slot, 1, NULL);
}

static int xhci_address_device(xhci_slot_t *s, uint16_t mps) {
    memset(s->in_ctx, 0, 4096);
    uint32_t *icc = xhci_ctx(s->in_ctx, 0);
    icc[1] = (1u << 0) | (1u << 1);

    uint32_t *slotc = xhci_ctx(s->in_ctx, 1);
    slotc[0] = ((uint32_t)s->speed << 20) | (1u << 27);
    slotc[1] = (uint32_t)s->port << 16;

    uint32_t *ep0 = xhci_ctx(s->in_ctx, 2);
    ep0[1] = (3u << 1) | ((uint32_t)XHCI_EP_CONTROL << 3) | ((uint32_t)mps << 16);
    ep0[2] = (uint32_t)(s->ep0.phys & ~0xFULL) | 1u;
    ep0[3] = (uint32_t)(s->ep0.phys >> 32);
    ep0[4] = 8;

    return xhci_submit_cmd(TRB_ADDRESS_DEV, (uint64_t)(uintptr_t)s->in_ctx,
                           s->slot_id, NULL);
}

static int xhci_evaluate_ep0(xhci_slot_t *s, uint16_t mps) {
    memset(s->in_ctx, 0, 4096);
    uint32_t *icc = xhci_ctx(s->in_ctx, 0);
    icc[1] = (1u << 0) | (1u << 1);

    uint32_t *slotc = xhci_ctx(s->in_ctx, 1);
    slotc[0] = ((uint32_t)s->speed << 20) | (1u << 27);
    slotc[1] = (uint32_t)s->port << 16;

    uint32_t *ep0 = xhci_ctx(s->in_ctx, 2);
    ep0[1] = (3u << 1) | ((uint32_t)XHCI_EP_CONTROL << 3) | ((uint32_t)mps << 16);
    ep0[2] = (uint32_t)(s->ep0.phys & ~0xFULL) | 1u;
    ep0[3] = (uint32_t)(s->ep0.phys >> 32);
    ep0[4] = 8;

    return xhci_submit_cmd(TRB_EVALUATE_CTX, (uint64_t)(uintptr_t)s->in_ctx,
                           s->slot_id, NULL);
}

static int xhci_configure_intr(xhci_slot_t *s, uint8_t ep_addr, uint16_t mps,
                               uint8_t interval) {
    uint8_t epnum = ep_addr & 0x0F;
    uint8_t dci = (uint8_t)(epnum * 2 + ((ep_addr & 0x80) ? 1 : 0));
    uint8_t ep_type = (ep_addr & 0x80) ? XHCI_EP_INTR_IN : 3;

    uint32_t ival = interval;
    if (s->speed == 3 || s->speed == 4) {
        if (interval == 0)
            ival = 0;
        else {
            ival = 1u << (interval - 1);
            if (ival > 255)
                ival = 255;
        }
    }

    memset(s->in_ctx, 0, 4096);
    uint32_t *icc = xhci_ctx(s->in_ctx, 0);
    icc[1] = (1u << 0) | (1u << dci);

    uint32_t *slotc = xhci_ctx(s->in_ctx, 1);
    slotc[0] = ((uint32_t)s->speed << 20) | ((uint32_t)dci << 27);
    slotc[1] = (uint32_t)s->port << 16;

    uint32_t *ep = xhci_ctx(s->in_ctx, dci + 1);
    ep[0] = ival << 16;
    ep[1] = (3u << 1) | ((uint32_t)ep_type << 3) | ((uint32_t)mps << 16);
    ep[2] = (uint32_t)(s->intr.phys & ~0xFULL) | 1u;
    ep[3] = (uint32_t)(s->intr.phys >> 32);
    ep[4] = 8;

    int r = xhci_submit_cmd(TRB_CONFIG_EP, (uint64_t)(uintptr_t)s->in_ctx,
                            s->slot_id, NULL);
    if (r == 0)
        s->ep_in_dci = dci;
    return r;
}

static uint32_t xhci_port_read(uint8_t port) {
    return mmio_r32(xhci_op + XHCI_PORT_REGS + (uint64_t)(port - 1) * 0x10);
}
static void xhci_port_write(uint8_t port, uint32_t v) {
    mmio_w32(xhci_op + XHCI_PORT_REGS + (uint64_t)(port - 1) * 0x10, v);
}

static int xhci_reset_port(uint8_t port, uint8_t *speed_out) {
    uint32_t psc = xhci_port_read(port);
    if (!(psc & PORTSC_CCS))
        return -1;

    xhci_port_write(port, psc | PORTSC_CHANGE_MASK);
    xhci_delay(10000);

    psc = xhci_port_read(port);
    xhci_port_write(port, (psc & ~PORTSC_PR) | PORTSC_PR);

    uint32_t v = psc;
    for (int i = 0; i < 200000; i++) {
        v = xhci_port_read(port);
        if (v & PORTSC_PRC)
            break;
        if (!(v & PORTSC_PR) && (v & PORTSC_PED))
            break;
        asm volatile("pause");
    }
    xhci_port_write(port, v | PORTSC_PRC | PORTSC_CHANGE_MASK);
    xhci_delay(20000);

    v = xhci_port_read(port);
    if (!(v & PORTSC_CCS))
        return -1;
    *speed_out = (uint8_t)((v >> PORTSC_SPEED_SHIFT) & PORTSC_SPEED_MASK);
    return 0;
}

static void xhci_setup_device(uint8_t slot, uint8_t port, uint8_t speed) {
    xhci_slot_t *s = &xhci_slots[slot];
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->slot_id = slot;
    s->port = port;
    s->speed = speed;
    s->int_len = 8;

    void *m;
    m = kmalloc_a(4096);
    memset(m, 0, 4096);
    xhci_ring_init(&s->ep0, m, XHCI_EP_TRBS);
    m = kmalloc_a(4096);
    memset(m, 0, 4096);
    xhci_ring_init(&s->intr, m, XHCI_EP_TRBS);
    s->dev_ctx = (uint8_t *)kmalloc_a(4096);
    memset(s->dev_ctx, 0, 4096);
    s->in_ctx = (uint8_t *)kmalloc_a(4096);
    memset(s->in_ctx, 0, 4096);
    s->int_buffer = (uint8_t *)kmalloc(8);
    memset(s->int_buffer, 0, 8);

    ((uint64_t *)xhci_dcbaa)[slot] = (uint64_t)(uintptr_t)s->dev_ctx;

    int ar = xhci_address_device(s, xhci_ep0_mps(speed));
    if (ar < 0) {
        serial_print("XHCI: Address Device failed code=");
        serial_print_hex((uint64_t)(-ar));
        serial_print(" speed=");
        serial_print_hex(speed);
        serial_print(" port=");
        serial_print_hex(port);
        serial_print("\n");
        s->used = 0;
        return;
    }

    usb_setup_packet_t setup;
    usb_device_descriptor_t *desc = (usb_device_descriptor_t *)kmalloc(18);
    memset(desc, 0, 18);

    setup.request_type = 0x80;
    setup.request = USB_REQ_GET_DESCRIPTOR;
    setup.value = (USB_DESC_DEVICE << 8);
    setup.index = 0;
    setup.length = 8;
    int cr = xhci_control(slot, &setup, desc, 8);
    if (cr < 0) {
        serial_print("XHCI: GET_DESCRIPTOR(8) failed code=");
        serial_print_hex((uint64_t)(-cr));
        serial_print("\n");
        kfree(desc);
        return;
    }

    uint16_t mps = desc->max_packet_size;
    if (mps && mps != xhci_ep0_mps(speed))
        xhci_evaluate_ep0(s, mps);

    setup.length = sizeof(usb_device_descriptor_t);
    if (xhci_control(slot, &setup, desc, sizeof(usb_device_descriptor_t)) < 0) {
        serial_print("XHCI: GET_DESCRIPTOR(18) failed\n");
        kfree(desc);
        return;
    }

    usb_config_descriptor_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    setup.value = (USB_DESC_CONFIG << 8);
    setup.length = sizeof(usb_config_descriptor_t);
    if (xhci_control(slot, &setup, &cfg, sizeof(cfg)) < 0) {
        serial_print("XHCI: GET_DESCRIPTOR(config) failed\n");
        kfree(desc);
        return;
    }

    uint16_t total = cfg.total_length;
    if (total == 0 || total > 512) {
        serial_print("XHCI: bad config total_length\n");
        kfree(desc);
        return;
    }
    uint8_t *full = (uint8_t *)kmalloc(total);
    memset(full, 0, total);
    setup.length = total;
    if (xhci_control(slot, &setup, full, total) < 0) {
        serial_print("XHCI: GET_DESCRIPTOR(full config) failed\n");
        kfree(full);
        kfree(desc);
        return;
    }

    setup.request_type = 0x00;
    setup.request = USB_REQ_SET_CONFIG;
    setup.value = cfg.config_value;
    setup.index = 0;
    setup.length = 0;
    if (xhci_control(slot, &setup, NULL, 0) < 0) {
        serial_print("XHCI: SET_CONFIG failed\n");
        kfree(full);
        kfree(desc);
        return;
    }

    serial_print("XHCI: device configured on slot ");
    serial_print_hex(slot);
    serial_print("\n");

    uint8_t *ptr = full + sizeof(usb_config_descriptor_t);
    int current_type = 0;
    while (ptr < full + total) {
        uint8_t len = ptr[0];
        uint8_t type = ptr[1];
        if (len == 0)
            break;

        if (type == USB_DESC_INTERFACE) {
            usb_interface_descriptor_t *iface = (usb_interface_descriptor_t *)ptr;
            current_type = 0;
            if (iface->interface_class == 0x03) {
                if (iface->interface_protocol == 0x01)
                    current_type = 1;
                else if (iface->interface_protocol == 0x02)
                    current_type = 2;

                if (current_type != 0) {
                    usb_setup_packet_t sp;
                    sp.request_type = 0x21;
                    sp.request = 0x0B;
                    sp.value = 0;
                    sp.index = iface->interface_num;
                    sp.length = 0;
                    xhci_control(slot, &sp, NULL, 0);
                }
            }
        } else if (type == USB_DESC_ENDPOINT && current_type != 0) {
            usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t *)ptr;
            if (ep->endpoint_address & 0x80) {
                uint16_t ep_mps = ep->max_packet_size & 0x7FF;
                if (ep_mps == 0)
                    ep_mps = 8;
                if (xhci_configure_intr(s, ep->endpoint_address, ep_mps,
                                        ep->interval) == 0) {
                    usb_device_t *udev = (usb_device_t *)kmalloc(sizeof(usb_device_t));
                    memset(udev, 0, sizeof(*udev));
                    udev->address = slot;
                    udev->port = port;
                    udev->type = (uint8_t)current_type;
                    udev->hcd = 3;
                    udev->slot = slot;
                    udev->ep_in = s->ep_in_dci;
                    udev->controller = xhci_pci;
                    s->udev = udev;
                    usb_register_device(udev);

                    serial_print(current_type == 1
                                     ? "XHCI: HID KEYBOARD ready\n"
                                     : "XHCI: HID MOUSE ready\n");
                    xhci_submit_intr(s);
                }
                current_type = 0;
            }
        }
        ptr += len;
    }

    kfree(full);
    kfree(desc);
}

void xhci_init(pci_device_t *dev) {
    xhci_pci = dev;

    uint32_t pci_cmd = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    pci_cmd |= (1 << 1) | (1 << 2);
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, pci_cmd);

    uint32_t bar0 = pci_read_config(dev->bus, dev->device, dev->function, 0x10);
    uint32_t bar1 = pci_read_config(dev->bus, dev->device, dev->function, 0x14);
    uint64_t bar;
    if ((bar0 & 0x6) == 0x4)
        bar = ((uint64_t)(bar1 & ~0xF) << 32) | (uint64_t)(bar0 & ~0xF);
    else
        bar = bar0 & ~0xF;
    xhci_bar = bar;

    for (uint64_t off = 0; off < 0x4000; off += 0x1000)
        vmm_map_page((void *)(uintptr_t)(bar + off),
                     (void *)(uintptr_t)(bar + off), 0x3);

    uint8_t caplength = *(volatile uint8_t *)(uintptr_t)bar;
    xhci_op = bar + caplength;
    xhci_rt = bar + mmio_r32(bar + XHCI_RTSOFF);
    xhci_db = bar + mmio_r32(bar + XHCI_DBOFF);

    uint32_t hcs1 = mmio_r32(bar + XHCI_HCSPARAMS1);
    xhci_max_slots = hcs1 & 0xFF;
    xhci_max_ports = (hcs1 >> 24) & 0xFF;
    uint32_t hcc1 = mmio_r32(bar + XHCI_HCCPARAMS1);
    xhci_ctx_stride = (hcc1 & (1u << 2)) ? 64 : 32;

    serial_print("XHCI: BAR=");
    serial_print_hex(bar);
    serial_print(" slots=");
    serial_print_hex(xhci_max_slots);
    serial_print(" ports=");
    serial_print_hex(xhci_max_ports);
    serial_print(" ctx=");
    serial_print_hex(xhci_ctx_stride);
    serial_print("\n");

    /* Reset do controlador: limpa slots/endereços que o firmware (SeaBIOS)
     * possa ter deixado habilitados, evitando erros de "port already
     * assigned" no Address Device. */
    mmio_w32(xhci_op + XHCI_USBCMD, USBCMD_HCRST);
    for (int i = 0;
         i < 1000000 &&
         (mmio_r32(xhci_op + XHCI_USBCMD) & USBCMD_HCRST);
         i++)
        asm volatile("pause");
    for (int i = 0;
         i < 1000000 &&
         (mmio_r32(xhci_op + XHCI_USBSTS) & USBSTS_CNR);
         i++)
        asm volatile("pause");
    for (int i = 0;
         i < 1000000 && !(mmio_r32(xhci_op + XHCI_USBSTS) & USBSTS_HCH);
         i++)
        asm volatile("pause");

    xhci_dcbaa = (uint8_t *)kmalloc_a(4096);
    memset(xhci_dcbaa, 0, 4096);

    void *cm = kmalloc_a(4096);
    memset(cm, 0, 4096);
    xhci_ring_init(&xhci_cmd, cm, XHCI_CMD_TRBS);

    void *em = kmalloc_a(4096);
    memset(em, 0, 4096);
    xhci_ring_init(&xhci_evt, em, XHCI_EVT_TRBS);

    void *erst_mem = kmalloc_a(4096);
    memset(erst_mem, 0, 4096);
    xhci_erst_entry_t *erst = (xhci_erst_entry_t *)erst_mem;
    erst[0].base = xhci_evt.phys;
    erst[0].size = XHCI_EVT_TRBS;

    mmio_w32(xhci_op + XHCI_CONFIG, xhci_max_slots);
    mmio_w64(xhci_op + XHCI_DCBAAP, (uint64_t)(uintptr_t)xhci_dcbaa);
    mmio_w64(xhci_op + XHCI_CRCR, xhci_cmd.phys | 1u);

    mmio_w32(xhci_rt + XHCI_IR0 + XHCI_ERSTSZ, 1);
    mmio_w64(xhci_rt + XHCI_IR0 + XHCI_ERSTBA, (uint64_t)(uintptr_t)erst);
    mmio_w64(xhci_rt + XHCI_IR0 + XHCI_ERDP, xhci_evt.phys | (1u << 3));
    mmio_w32(xhci_rt + XHCI_IR0 + XHCI_IMAN, 1u);

    mmio_w32(xhci_op + XHCI_USBCMD, USBCMD_RS);
    for (int i = 0;
         i < 1000000 && (mmio_r32(xhci_op + XHCI_USBSTS) & USBSTS_HCH);
         i++)
        asm volatile("pause");
    serial_print("XHCI: controller running\n");

    xhci_ready = 1;

    for (uint32_t port = 1; port <= xhci_max_ports; port++) {
        uint32_t psc = xhci_port_read(port);
        if (!(psc & PORTSC_CCS))
            continue;

        serial_print("XHCI: device on port ");
        serial_print_hex(port);
        serial_print("\n");

        uint8_t speed = 0;
        if (xhci_reset_port((uint8_t)port, &speed) < 0) {
            serial_print("XHCI: port reset failed\n");
            continue;
        }

        uint8_t slot = 0;
        if (xhci_submit_cmd(TRB_ENABLE_SLOT, 0, 0, &slot) < 0 || slot == 0) {
            serial_print("XHCI: Enable Slot failed\n");
            continue;
        }

        serial_print("XHCI: port reset ok speed=");
        serial_print_hex(speed);
        serial_print(" slot=");
        serial_print_hex(slot);
        serial_print("\n");

        xhci_setup_device(slot, (uint8_t)port, speed);
    }

    serial_print("XHCI: init done\n");
}

void usb_poll_xhci(void) {
    if (!xhci_ready)
        return;

    xhci_trb_t ev;
    while (xhci_poll_event(&ev)) {
        uint32_t type = XHCI_TRB_TYPE(ev.control);
        if (type == TRB_TRANSFER_EV) {
            xhci_handle_transfer_event(&ev);
        } else if (type == TRB_PORT_CHANGE) {
            uint8_t port = (uint8_t)((ev.param >> 24) & 0xFF);
            if (port >= 1 && port <= xhci_max_ports) {
                uint32_t psc = xhci_port_read(port);
                xhci_port_write(port, psc | PORTSC_CHANGE_MASK);
            }
        }
    }
}
