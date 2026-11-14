#include "usb_msc.h"
#include "string.h"
#include "serial.h"

extern void sdfs_set_usb_partition(uint32_t start_lba, uint32_t total_sectors);

/* Estrutura compativel com ehci_usb_device_t (6 bytes) */
typedef struct {
    uint8_t address;
    uint8_t speed;
    uint8_t max_packet;
    uint8_t hub_address;
    uint8_t hub_port;
    uint8_t class_code;
} msc_usb_device_t;

/* Estrutura interna para o dispositivo MSC ativo */
typedef struct {
    void *controller;
    msc_usb_device_t device;
    uint8_t ep_in;
    uint16_t ep_in_max_packet;
    uint8_t ep_out;
    uint16_t ep_out_max_packet;
    uint8_t toggle_in;
    uint8_t toggle_out;
    uint32_t tag;
    bool available;
    uint64_t sector_count;
    uint32_t sector_size;
    char model[32];
} active_msc_device_t;

static active_msc_device_t g_msc = {0};

/* Funcao externa de transferencia Bulk implementada em usb_ehci.c */
extern int ehci_bulk_transfer_wrap(void *controller, void *device,
                                   uint8_t ep_address, uint16_t max_packet,
                                   uint8_t *data_toggle, void *buffer,
                                   uint32_t length, int is_in);

int usb_msc_find_interface(const uint8_t *desc, uint16_t len,
                           uint8_t *out_config, uint8_t *out_iface,
                           uint8_t *out_ep_in, uint16_t *out_ep_in_max,
                           uint8_t *out_ep_out, uint16_t *out_ep_out_max) {
    if (!desc || len < 9) return -1;

    uint8_t config_val = desc[5];
    uint16_t offset = desc[0];
    bool in_msc_iface = false;
    uint8_t iface_num = 0;
    uint8_t ep_in = 0, ep_out = 0;
    uint16_t ep_in_max = 64, ep_out_max = 64;

    while (offset + 2 <= len) {
        uint8_t dlen = desc[offset];
        uint8_t dtype = desc[offset + 1];
        if (dlen == 0 || offset + dlen > len) break;

        if (dtype == 4) { /* Interface Descriptor */
            if (dlen >= 9) {
                uint8_t if_class = desc[offset + 5];
                uint8_t if_subclass = desc[offset + 6];
                uint8_t if_proto = desc[offset + 7];
                /* Mass Storage (0x08), SCSI transparent (0x06), Bulk-Only (0x50) */
                if (if_class == 0x08 && (if_subclass == 0x06 || if_subclass == 0x05 || if_subclass == 0x02) && if_proto == 0x50) {
                    in_msc_iface = true;
                    iface_num = desc[offset + 2];
                } else {
                    in_msc_iface = false;
                }
            }
        } else if (dtype == 5 && in_msc_iface) { /* Endpoint Descriptor */
            if (dlen >= 7) {
                uint8_t ep_addr = desc[offset + 2];
                uint8_t ep_attr = desc[offset + 3];
                uint16_t max_p = (uint16_t)desc[offset + 4] | ((uint16_t)desc[offset + 5] << 8);

                if ((ep_attr & 0x03) == 0x02) { /* Bulk Endpoint */
                    if (ep_addr & 0x80) {
                        ep_in = ep_addr;
                        ep_in_max = max_p;
                    } else {
                        ep_out = ep_addr;
                        ep_out_max = max_p;
                    }
                }
            }
        }

        if (in_msc_iface && ep_in && ep_out) {
            if (out_config) *out_config = config_val;
            if (out_iface) *out_iface = iface_num;
            if (out_ep_in) *out_ep_in = ep_in;
            if (out_ep_in_max) *out_ep_in_max = ep_in_max;
            if (out_ep_out) *out_ep_out = ep_out;
            if (out_ep_out_max) *out_ep_out_max = ep_out_max;
            return 0;
        }

        offset += dlen;
    }

    return -1;
}

int usb_msc_read_sectors(uint32_t lba, uint32_t count, void *buffer) {
    if (!g_msc.available || count == 0 || !buffer) return -1;

    uint8_t *dst = (uint8_t *)buffer;
    uint32_t remaining = count;
    uint32_t cur_lba = lba;

    /* Transfere em blocos de no maximo 8 setores (4096 bytes) por comando */
    while (remaining > 0) {
        uint32_t batch = (remaining > 8) ? 8 : remaining;
        uint32_t batch_bytes = batch * 512;
        uint32_t tag = ++g_msc.tag;

        usb_msc_cbw_t cbw;
        memset(&cbw, 0, sizeof(cbw));
        cbw.signature = 0x43425355; /* "USBC" */
        cbw.tag = tag;
        cbw.data_transfer_len = batch_bytes;
        cbw.flags = 0x80; /* IN */
        cbw.lun = 0;
        cbw.cdb_length = 10;
        cbw.cdb[0] = SCSI_READ10;
        cbw.cdb[2] = (cur_lba >> 24) & 0xFF;
        cbw.cdb[3] = (cur_lba >> 16) & 0xFF;
        cbw.cdb[4] = (cur_lba >> 8) & 0xFF;
        cbw.cdb[5] = cur_lba & 0xFF;
        cbw.cdb[7] = (batch >> 8) & 0xFF;
        cbw.cdb[8] = batch & 0xFF;

        /* Fase 1: Envia CBW */
        if (ehci_bulk_transfer_wrap(g_msc.controller, &g_msc.device,
                                    g_msc.ep_out, g_msc.ep_out_max_packet,
                                    &g_msc.toggle_out, &cbw, 31, 0) != 0) {
            serial_print("[MSC-R] CBW failed\n");
            return -1;
        }

        /* Fase 2: Recebe dados */
        if (ehci_bulk_transfer_wrap(g_msc.controller, &g_msc.device,
                                    g_msc.ep_in, g_msc.ep_in_max_packet,
                                    &g_msc.toggle_in, dst, batch_bytes, 1) != 0) {
            serial_print("[MSC-R] DATA IN failed\n");
            return -2;
        }

        /* Fase 3: Recebe CSW */
        usb_msc_csw_t csw;
        memset(&csw, 0, sizeof(csw));
        if (ehci_bulk_transfer_wrap(g_msc.controller, &g_msc.device,
                                    g_msc.ep_in, g_msc.ep_in_max_packet,
                                    &g_msc.toggle_in, &csw, 13, 1) != 0) {
            serial_print("[MSC-R] CSW failed\n");
            return -3;
        }

        if (csw.signature != 0x53425355 || csw.tag != tag || csw.status != 0) {
            return -4;
        }

        dst += batch_bytes;
        cur_lba += batch;
        remaining -= batch;
    }

    return 0;
}

int usb_msc_write_sectors(uint32_t lba, uint32_t count, const void *buffer) {
    if (!g_msc.available || count == 0 || !buffer) return -1;

    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t remaining = count;
    uint32_t cur_lba = lba;

    while (remaining > 0) {
        uint32_t batch = (remaining > 8) ? 8 : remaining;
        uint32_t batch_bytes = batch * 512;
        uint32_t tag = ++g_msc.tag;

        usb_msc_cbw_t cbw;
        memset(&cbw, 0, sizeof(cbw));
        cbw.signature = 0x43425355; /* "USBC" */
        cbw.tag = tag;
        cbw.data_transfer_len = batch_bytes;
        cbw.flags = 0x00; /* OUT */
        cbw.lun = 0;
        cbw.cdb_length = 10;
        cbw.cdb[0] = SCSI_WRITE10;
        cbw.cdb[2] = (cur_lba >> 24) & 0xFF;
        cbw.cdb[3] = (cur_lba >> 16) & 0xFF;
        cbw.cdb[4] = (cur_lba >> 8) & 0xFF;
        cbw.cdb[5] = cur_lba & 0xFF;
        cbw.cdb[7] = (batch >> 8) & 0xFF;
        cbw.cdb[8] = batch & 0xFF;

        /* Fase 1: Envia CBW */
        if (ehci_bulk_transfer_wrap(g_msc.controller, &g_msc.device,
                                    g_msc.ep_out, g_msc.ep_out_max_packet,
                                    &g_msc.toggle_out, &cbw, 31, 0) != 0) {
            serial_print("[MSC-W] CBW failed\n");
            return -1;
        }

        /* Fase 2: Envia dados */
        if (ehci_bulk_transfer_wrap(g_msc.controller, &g_msc.device,
                                    g_msc.ep_out, g_msc.ep_out_max_packet,
                                    &g_msc.toggle_out, (void *)src, batch_bytes, 0) != 0) {
            serial_print("[MSC-W] DATA OUT failed\n");
            return -2;
        }

        /* Fase 3: Recebe CSW */
        usb_msc_csw_t csw;
        memset(&csw, 0, sizeof(csw));
        if (ehci_bulk_transfer_wrap(g_msc.controller, &g_msc.device,
                                    g_msc.ep_in, g_msc.ep_in_max_packet,
                                    &g_msc.toggle_in, &csw, 13, 1) != 0) {
            serial_print("[MSC-W] CSW failed\n");
            return -3;
        }

        if (csw.signature != 0x53425355 || csw.tag != tag || csw.status != 0) {
            return -4;
        }

        src += batch_bytes;
        cur_lba += batch;
        remaining -= batch;
    }

    return 0;
}

int usb_msc_register_device(void *controller, void *device,
                            uint8_t ep_in, uint16_t ep_in_max,
                            uint8_t ep_out, uint16_t ep_out_max) {
    g_msc.controller = controller;
    if (device) {
        memcpy(&g_msc.device, device, sizeof(msc_usb_device_t));
    }
    g_msc.ep_in = ep_in;
    g_msc.ep_in_max_packet = ep_in_max;
    g_msc.ep_out = ep_out;
    g_msc.ep_out_max_packet = ep_out_max;
    g_msc.toggle_in = 0;
    g_msc.toggle_out = 0;
    g_msc.tag = 100;
    g_msc.available = true;
    g_msc.sector_count = 0;
    g_msc.sector_size = 512;
    strcpy(g_msc.model, "USB Flash Disk");

    serial_print("[MSC] Registrando pendrive USB Mass Storage...\n");

    /* 1. SCSI INQUIRY */
    uint32_t tag = ++g_msc.tag;
    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = 0x43425355;
    cbw.tag = tag;
    cbw.data_transfer_len = 36;
    cbw.flags = 0x80;
    cbw.lun = 0;
    cbw.cdb_length = 6;
    cbw.cdb[0] = SCSI_INQUIRY;
    cbw.cdb[4] = 36;

    uint8_t inq_buf[36];
    memset(inq_buf, 0, sizeof(inq_buf));

    if (ehci_bulk_transfer_wrap(controller, &g_msc.device, ep_out, ep_out_max, &g_msc.toggle_out, &cbw, 31, 0) == 0 &&
        ehci_bulk_transfer_wrap(controller, &g_msc.device, ep_in, ep_in_max, &g_msc.toggle_in, inq_buf, 36, 1) == 0) {
        usb_msc_csw_t csw;
        ehci_bulk_transfer_wrap(controller, &g_msc.device, ep_in, ep_in_max, &g_msc.toggle_in, &csw, 13, 1);

        char vendor[9], prod[17];
        memcpy(vendor, inq_buf + 8, 8); vendor[8] = '\0';
        memcpy(prod, inq_buf + 16, 16); prod[16] = '\0';
        serial_print("[MSC] Identificacao: ");
        serial_print(vendor);
        serial_print(" ");
        serial_print(prod);
        serial_print("\n");
        strncpy(g_msc.model, prod, sizeof(g_msc.model) - 1);
    }

    /* 2. SCSI READ CAPACITY 10 */
    tag = ++g_msc.tag;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = 0x43425355;
    cbw.tag = tag;
    cbw.data_transfer_len = 8;
    cbw.flags = 0x80;
    cbw.cdb_length = 10;
    cbw.cdb[0] = SCSI_READ_CAPACITY10;

    uint8_t cap_buf[8];
    memset(cap_buf, 0, sizeof(cap_buf));

    if (ehci_bulk_transfer_wrap(controller, &g_msc.device, ep_out, ep_out_max, &g_msc.toggle_out, &cbw, 31, 0) == 0 &&
        ehci_bulk_transfer_wrap(controller, &g_msc.device, ep_in, ep_in_max, &g_msc.toggle_in, cap_buf, 8, 1) == 0) {
        usb_msc_csw_t csw;
        ehci_bulk_transfer_wrap(controller, &g_msc.device, ep_in, ep_in_max, &g_msc.toggle_in, &csw, 13, 1);

        uint32_t last_lba = ((uint32_t)cap_buf[0] << 24) | ((uint32_t)cap_buf[1] << 16) |
                            ((uint32_t)cap_buf[2] << 8) | (uint32_t)cap_buf[3];
        uint32_t blk_size = ((uint32_t)cap_buf[4] << 24) | ((uint32_t)cap_buf[5] << 16) |
                            ((uint32_t)cap_buf[6] << 8) | (uint32_t)cap_buf[7];

        g_msc.sector_count = (uint64_t)last_lba + 1;
        if (blk_size) g_msc.sector_size = blk_size;

        serial_print("[MSC] Setores totais: ");
        serial_print_hex((uint32_t)(g_msc.sector_count >> 32));
        serial_print_hex((uint32_t)g_msc.sector_count);
        serial_print("\n");
    }

    /* 3. Analisa Particao MBR para persistencia */
    uint8_t mbr[512];
    if (usb_msc_read_sectors(0, 1, mbr) == 0) {
        /* Verifica assinatura MBR 0x55AA */
        if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
            /* Particao 2: offset 0x1CE */
            uint32_t p2_start = *(uint32_t *)(mbr + 0x1CE + 8);
            uint32_t p2_sec   = *(uint32_t *)(mbr + 0x1CE + 12);
            if (p2_start > 0 && p2_sec > 0) {
                serial_print("[MSC] Particao 2 persistente encontrada! LBA=");
                serial_print_hex(p2_start);
                serial_print(" Setores=");
                serial_print_hex(p2_sec);
                serial_print("\n");
                sdfs_set_usb_partition(p2_start, p2_sec);
                return 0;
            }
        }
    }

    /* Fallback: se nao houver particao 2, utiliza LBA 204800 (100MB) como particao padrao */
    if (g_msc.sector_count > 204800) {
        sdfs_set_usb_partition(204800, (uint32_t)(g_msc.sector_count - 204800));
    } else {
        sdfs_set_usb_partition(0, (uint32_t)g_msc.sector_count);
    }

    return 0;
}

bool usb_msc_is_available(void) {
    return g_msc.available;
}

uint64_t usb_msc_get_sector_count(void) {
    return g_msc.sector_count;
}

uint32_t usb_msc_get_sector_size(void) {
    return g_msc.sector_size;
}

const char *usb_msc_get_model(void) {
    return g_msc.model;
}
