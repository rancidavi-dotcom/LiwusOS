#include "nvme.h"
#include "pci.h"
#include "serial.h"
#include "string.h"

/*
 * Minimal polled NVM Express driver.
 *
 * Brings up the controller with one admin queue pair (id 0) and one I/O
 * queue pair (id 1), issues IDENTIFY, then serves block reads/writes with
 * a single 4 KiB bounce buffer so every transfer maps to exactly one page
 * (PRP1 only, no PRP lists).  Interrupts stay masked; completion is polled
 * from the I/O completion queue, matching the polling design used by the
 * AHCI and USB drivers.
 */

extern void vmm_map_page(void *phys, void *virt, uint64_t flags);

/* ---- Controller registers (BAR0 byte offsets) ---- */
#define NVME_CAP   0x00
#define NVME_VS    0x08
#define NVME_INTMS 0x0C
#define NVME_INTMC 0x10
#define NVME_CC    0x14
#define NVME_CSTS  0x1C
#define NVME_AQA   0x24
#define NVME_ASQ   0x28
#define NVME_ACQ   0x30
#define NVME_DBS   0x1000 /* doorbell base */

/* CC bits */
#define NVME_CC_EN     (1u << 0)
#define NVME_CC_CSS_NVM (0u << 4)
#define NVME_CC_MPS_4K  (0u << 7)
#define NVME_CC_IOSQES  (6u << 16) /* 2^6 = 64-byte SQ entry */
#define NVME_CC_IOCQES  (4u << 20) /* 2^4 = 16-byte CQ entry */

/* CSTS bits */
#define NVME_CSTS_RDY  0x1

/* Admin opcodes */
#define NVME_ADMIN_CREATE_SQ 0x01
#define NVME_ADMIN_CREATE_CQ 0x05
#define NVME_ADMIN_IDENTIFY  0x06
#define NVME_IO_WRITE        0x01
#define NVME_IO_READ         0x02

/* Identify CNS values */
#define NVME_CNS_NAMESPACE  0x00
#define NVME_CNS_CONTROLLER 0x01

#define NVME_ADMIN_QID 0
#define NVME_IO_QID    1
#define NVME_Q_DEPTH   16
#define NVME_BOUNCE    4096

typedef struct {
    uint32_t cdw0; /* opc:7 fuse:2 cid:16 */
    uint32_t nsid;
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sq_entry_t;

typedef struct {
    uint32_t result;
    uint32_t rsvd;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status; /* bit0 = phase */
} __attribute__((packed)) nvme_cq_entry_t;

static volatile uint8_t *nvme_mmio = 0;
static int nvme_present = 0;
static uint8_t nvme_nsid = 1;
static uint32_t nvme_lba_size = 512;
static uint64_t nvme_lba_count = 0;
static uint8_t nvme_dstrd = 0;

static nvme_sq_entry_t nvme_asq[NVME_Q_DEPTH] __attribute__((aligned(4096)));
static nvme_cq_entry_t nvme_acq[NVME_Q_DEPTH] __attribute__((aligned(4096)));
static nvme_sq_entry_t nvme_iosq[NVME_Q_DEPTH] __attribute__((aligned(4096)));
static nvme_cq_entry_t nvme_iocq[NVME_Q_DEPTH] __attribute__((aligned(4096)));
static uint8_t nvme_id_buf[4096] __attribute__((aligned(4096)));
static uint8_t nvme_bounce[NVME_BOUNCE] __attribute__((aligned(4096)));

static uint16_t nvme_asq_tail = 0;
static uint16_t nvme_acq_head = 0;
static uint8_t nvme_acq_phase = 1;
static uint16_t nvme_iosq_tail = 0;
static uint16_t nvme_iocq_head = 0;
static uint8_t nvme_iocq_phase = 1;
static uint16_t nvme_cid = 0;
static uint16_t nvme_last_status = 0;

/* ---- MMIO helpers ---- */
static uint32_t nvme_r32(uint32_t off) {
    return *(volatile uint32_t *)(nvme_mmio + off);
}
static void nvme_w32(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(nvme_mmio + off) = val;
}
static uint64_t nvme_r64(uint32_t off) {
    return *(volatile uint64_t *)(nvme_mmio + off);
}
static void nvme_w64(uint32_t off, uint64_t val) {
    *(volatile uint64_t *)(nvme_mmio + off) = val;
}

static void nvme_delay(uint32_t loops) {
    for (volatile uint32_t i = 0; i < loops; i++)
        asm volatile("pause");
}

static void nvme_ring_sq(uint16_t qid, uint16_t tail) {
    uint32_t stride = 4u << nvme_dstrd;
    nvme_w32(NVME_DBS + (uint32_t)(2 * qid) * stride, tail);
}

static void nvme_ring_cq(uint16_t qid, uint16_t head) {
    uint32_t stride = 4u << nvme_dstrd;
    nvme_w32(NVME_DBS + (uint32_t)(2 * qid + 1) * stride, head);
}

static int nvme_wait_csts(int ready) {
    for (uint64_t i = 0; i < 100000000ULL; i++) {
        if ((nvme_r32(NVME_CSTS) & NVME_CSTS_RDY) == (ready ? 1u : 0u))
            return 0;
        asm volatile("pause");
    }
    return -1;
}

/*
 * Poll a completion queue for the next entry. Returns 0 on success, -1 on
 * command error and -2 on timeout. `result` receives DW0 when non-NULL.
 */
static int nvme_poll_cq(uint16_t qid, volatile nvme_cq_entry_t *cq,
                        uint16_t *head, uint8_t *phase, uint32_t *result) {
    for (uint64_t spin = 0; spin < 200000000ULL; spin++) {
        volatile nvme_cq_entry_t *e = &cq[*head];
        uint16_t st = e->status;
        if ((st & 1) == *phase) {
            nvme_last_status = st;
            int ok = (((st >> 1) & 0x7FF) == 0);
            if (result)
                *result = e->result;
            (*head)++;
            if (*head >= NVME_Q_DEPTH) {
                *head = 0;
                *phase ^= 1;
            }
            nvme_ring_cq(qid, *head);
            return ok ? 0 : -1;
        }
    }
    return -2;
}

static int nvme_submit_admin(nvme_sq_entry_t *cmd, uint32_t *result) {
    uint16_t cid = ++nvme_cid;
    cmd->cdw0 = (cmd->cdw0 & 0xFFu) | ((uint32_t)cid << 16);
    nvme_asq[nvme_asq_tail] = *cmd;
    nvme_asq_tail++;
    if (nvme_asq_tail >= NVME_Q_DEPTH)
        nvme_asq_tail = 0;
    nvme_ring_sq(NVME_ADMIN_QID, nvme_asq_tail);
    return nvme_poll_cq(NVME_ADMIN_QID, nvme_acq, &nvme_acq_head,
                        &nvme_acq_phase, result);
}

static int nvme_io_rw(uint64_t lba, uint16_t nlb, void *buf, int write) {
    if (!nvme_present)
        return -1;
    memset(&nvme_iosq[nvme_iosq_tail], 0, sizeof(nvme_sq_entry_t));
    nvme_sq_entry_t *cmd = &nvme_iosq[nvme_iosq_tail];
    uint16_t cid = ++nvme_cid;
    cmd->cdw0 = (write ? NVME_IO_WRITE : NVME_IO_READ) |
                ((uint32_t)cid << 16);
    cmd->nsid = nvme_nsid;
    cmd->prp1 = (uint64_t)(uintptr_t)buf;
    cmd->cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd->cdw11 = (uint32_t)(lba >> 32);
    cmd->cdw12 = (uint32_t)(nlb - 1);
    nvme_iosq_tail++;
    if (nvme_iosq_tail >= NVME_Q_DEPTH)
        nvme_iosq_tail = 0;
    nvme_ring_sq(NVME_IO_QID, nvme_iosq_tail);
    int rc = nvme_poll_cq(NVME_IO_QID, nvme_iocq, &nvme_iocq_head,
                          &nvme_iocq_phase, 0);
    if (rc != 0) {
        serial_print("NVMe: io ");
        serial_print(write ? "wr" : "rd");
        serial_print(" lba=0x");
        serial_print_hex(lba);
        serial_print(" nlb=0x");
        serial_print_hex(nlb);
        serial_print(" rc=0x");
        serial_print_hex((uint64_t)(int64_t)rc);
        serial_print(" st=0x");
        serial_print_hex(nvme_last_status);
        serial_print("\n");
    }
    return rc;
}

static int nvme_identify_raw(uint8_t cns, uint8_t nsid, void *buf) {
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = (uint64_t)(uintptr_t)buf;
    cmd.cdw10 = cns;
    return nvme_submit_admin(&cmd, 0);
}

int nvme_available(void) { return nvme_present; }
uint32_t nvme_sector_size(void) { return nvme_lba_size; }
uint64_t nvme_identify(uint8_t nsid) {
    (void)nsid;
    return nvme_lba_count;
}

static int nvme_transfer(uint64_t lba, uint32_t count, uint8_t *buf, int write) {
    if (!nvme_present || !buf || count == 0)
        return -1;
    uint32_t spb = nvme_lba_size / 512; /* logical blocks per device LBA */
    if (spb == 0 || (lba % spb) != 0)
        return -1;
    uint64_t dlba = lba / spb;
    uint32_t bytes = count * 512;

    while (bytes > 0) {
        uint32_t chunk = bytes > NVME_BOUNCE ? NVME_BOUNCE : bytes;
        chunk -= chunk % nvme_lba_size; /* whole logical blocks only */
        if (chunk == 0)
            return -1;
        uint16_t nlb = (uint16_t)(chunk / nvme_lba_size);
        if (write)
            memcpy(nvme_bounce, buf, chunk);
        int r = nvme_io_rw(dlba, nlb, nvme_bounce, write);
        if (r)
            return r;
        if (!write)
            memcpy(buf, nvme_bounce, chunk);
        dlba += nlb;
        buf += chunk;
        bytes -= chunk;
    }
    return 0;
}

int nvme_read_sector(uint8_t nsid, uint64_t lba, uint32_t count, uint8_t *buf) {
    (void)nsid;
    return nvme_transfer(lba, count, buf, 0) == 0 ? 1 : 0;
}

int nvme_write_sector(uint8_t nsid, uint64_t lba, uint32_t count, uint8_t *buf) {
    (void)nsid;
    return nvme_transfer(lba, count, buf, 1) == 0 ? 1 : 0;
}

int nvme_find_first(uint8_t *nsid_out) {
    if (!nvme_present)
        return -1;
    if (nsid_out)
        *nsid_out = nvme_nsid;
    return 0;
}

void nvme_init(void) {
    if (nvme_present)
        return;

    pci_device_t *dev = pci_get_nvme();
    if (!dev)
        return;

    uint32_t bar0 =
        pci_read_config(dev->bus, dev->device, dev->function, 0x10);
    uint32_t bar1 =
        pci_read_config(dev->bus, dev->device, dev->function, 0x14);
    if (bar0 & 1) {
        serial_print("NVMe: BAR0 nao e memory-mapped\n");
        return;
    }
    uint64_t phys = (uint64_t)(bar0 & 0xFFFFFFF0u);
    if ((bar0 & 0x6) == 0x4) /* 64-bit BAR */
        phys |= ((uint64_t)bar1 << 32);

    uint32_t cmd =
        pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x02; /* memory space enable */
    cmd |= 0x04; /* bus mastering enable (DMA) */
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, cmd);

    /* The register file is 16 KiB (doorbells included). */
    for (int i = 0; i < 4; i++)
        vmm_map_page((void *)(uintptr_t)(phys + i * 4096),
                     (void *)(uintptr_t)(phys + i * 4096), 3);
    nvme_mmio = (volatile uint8_t *)(uintptr_t)phys;

    uint64_t cap = nvme_r64(NVME_CAP);
    nvme_dstrd = (uint8_t)((cap >> 32) & 0xF);

    serial_print("NVMe: BAR0=0x");
    serial_print_hex(phys);
    serial_print(" CAP=0x");
    serial_print_hex(cap);
    serial_print("\n");

    /* Disable the controller and mask interrupts. */
    nvme_w32(NVME_CC, nvme_r32(NVME_CC) & ~NVME_CC_EN);
    if (nvme_wait_csts(0) != 0) {
        serial_print("NVMe: controlador nao desabilitou\n");
        return;
    }
    nvme_w32(NVME_INTMS, 0xFFFFFFFF);

    memset(nvme_asq, 0, sizeof(nvme_asq));
    memset(nvme_acq, 0, sizeof(nvme_acq));
    memset(nvme_iosq, 0, sizeof(nvme_iosq));
    memset(nvme_iocq, 0, sizeof(nvme_iocq));
    nvme_asq_tail = nvme_acq_head = 0;
    nvme_acq_phase = 1;
    nvme_iosq_tail = nvme_iocq_head = 0;
    nvme_iocq_phase = 1;

    nvme_w32(NVME_AQA, ((uint32_t)(NVME_Q_DEPTH - 1) << 16) |
                           (uint32_t)(NVME_Q_DEPTH - 1));
    nvme_w64(NVME_ASQ, (uint64_t)(uintptr_t)nvme_asq);
    nvme_w64(NVME_ACQ, (uint64_t)(uintptr_t)nvme_acq);
    nvme_delay(10000);

    nvme_w32(NVME_CC, NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                          NVME_CC_IOSQES | NVME_CC_IOCQES);
    if (nvme_wait_csts(1) != 0) {
        serial_print("NVMe: controlador nao ficou pronto\n");
        return;
    }

    /* Identify controller to confirm the NVM command set is live. */
    if (nvme_identify_raw(NVME_CNS_CONTROLLER, 0, nvme_id_buf) != 0) {
        serial_print("NVMe: identify controller falhou\n");
        return;
    }

    /* Identify namespace 1. */
    if (nvme_identify_raw(NVME_CNS_NAMESPACE, 1, nvme_id_buf) != 0) {
        serial_print("NVMe: identify namespace falhou\n");
        return;
    }
    uint64_t nsze = *(uint64_t *)(nvme_id_buf + 0);
    uint8_t nlbaf = nvme_id_buf[25];
    uint8_t flbas = nvme_id_buf[26] & 0x0F;
    uint8_t lbads = 9;
    if (flbas <= nlbaf)
        lbads = nvme_id_buf[128 + 4 * flbas + 2];
    if (lbads == 0 || lbads > 16)
        lbads = 9;
    nvme_lba_size = 1u << lbads;
    nvme_lba_count = nsze << (lbads - 9);
    nvme_nsid = 1;

    serial_print("NVMe: nsid=1 lba_size=0x");
    serial_print_hex(nvme_lba_size);
    serial_print(" sectors=0x");
    serial_print_hex(nvme_lba_count);
    serial_print("\n");

    /* Create the I/O completion queue, then the matching submission queue. */
    nvme_sq_entry_t ccmd;
    memset(&ccmd, 0, sizeof(ccmd));
    ccmd.cdw0 = NVME_ADMIN_CREATE_CQ;
    ccmd.prp1 = (uint64_t)(uintptr_t)nvme_iocq; /* queue base address */
    ccmd.cdw10 = ((uint32_t)(NVME_Q_DEPTH - 1) << 16) | (uint32_t)NVME_IO_QID;
    ccmd.cdw11 = 0x1; /* physically contiguous, IRQ vector 0 */
    if (nvme_submit_admin(&ccmd, 0) != 0) {
        serial_print("NVMe: create CQ falhou\n");
        return;
    }
    memset(&ccmd, 0, sizeof(ccmd));
    ccmd.cdw0 = NVME_ADMIN_CREATE_SQ;
    ccmd.prp1 = (uint64_t)(uintptr_t)nvme_iosq; /* queue base address */
    ccmd.cdw10 = ((uint32_t)(NVME_Q_DEPTH - 1) << 16) | (uint32_t)NVME_IO_QID;
    ccmd.cdw11 = ((uint32_t)NVME_IO_QID << 16) | 0x1; /* CQID | PC */
    if (nvme_submit_admin(&ccmd, 0) != 0) {
        serial_print("NVMe: create SQ falhou st=0x");
        serial_print_hex(nvme_last_status);
        serial_print("\n");
        return;
    }

    nvme_present = 1;
    serial_print("NVMe: driver pronto\n");
}
