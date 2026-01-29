#include "drivers/scsi.h"
#include "drivers/pci.h"
#include "drivers/serial.h"
#include "uapi/io.h"
#include <stdint.h>
#include <string.h>

#define SCSI_ESP_PCI_VENDOR 0x1022
#define SCSI_ESP_PCI_DEVICE 0x2020

/* am53c974 (ESP100) I/O registers, offset from BAR0. */
#define ESP_TCLO 0x00 /* Transmit Command Low */
#define ESP_TCMID 0x04 /* Transmit Command Mid */
#define ESP_FIFO 0x08 /* FIFO data */
#define ESP_CMD 0x0C /* Command */
#define ESP_RSTAT 0x10 /* Status (read) */
#define ESP_WBUSID 0x10 /* Select target (write) */
#define ESP_RINTR 0x14 /* Interrupt (read) */
#define ESP_WSEL 0x14 /* Select enable (write) */
#define ESP_RSEQ 0x18 /* Sequence (read) */
#define ESP_RFLAGS 0x1C /* Flags (read) */
#define ESP_RCONFIG1 0x20 /* Config 1 (read) */
#define ESP_TCHI 0x38 /* Transmit Command High */
#define ESP_DMA_CMD 0x40 /* DMA command (0x40-0x5F) */
#define ESP_DMA_STC 0x44 /* DMA transfer count */
#define ESP_DMA_SPA 0x48 /* DMA start physical address */
#define ESP_DMA_WBC 0x4C /* DMA last write byte count */
#define ESP_DMA_WAC 0x50 /* DMA working address counter */
#define ESP_DMA_STAT 0x54 /* DMA status */
#define ESP_DMA_SMDLA 0x58 /* DMA software mode last address */
#define ESP_DMA_WMAC 0x5C /* DMA working mode address counter */

/* ESP commands. */
#define ESP_CMD_DMA 0x80
#define ESP_CMD_SELATN 0x42
#define ESP_CMD_TI 0x10 /* Transfer Information */
#define ESP_CMD_ICCS 0x11 /* Initiator Command Complete Sequence */
#define ESP_CMD_RESET 0x02

/* ESP status / interrupt bits. */
#define ESP_RSTAT_INT 0x80
#define ESP_RINTR_DC 0x20 /* Disconnect: no target */
#define ESP_RINTR_BS 0x10 /* Bus service */
#define ESP_RINTR_FC 0x08 /* Function complete */
#define ESP_RINTR_IL 0x40 /* Illegal phase */

/* DMA command bits. */
#define DMA_CMD_IDLE 0x000
#define DMA_CMD_START 0x003
#define DMA_CMD_FROM_DEV 0x080 /* direction: data in */

#define SCSI_DEV_ID_BYTE 0x00 /* LUN 0 */
#define SCSI_ISIZE 96
#define SCSI_MAX_XFER (16 * 512) /* bounce buffer size */
#define SCSI_DMA_TIMEOUT 250000000ULL
#define SCSI_INT_TIMEOUT 12000000ULL

static uint32_t scsi_base = 0;
static volatile int scsi_bus_busy = 0;

/* Serialize whole SCSI operations (enquiry polls vs. media disk reads).
 * Preemption-safe: while spinning, interrupts are briefly enabled so the
 * lock holder can run to completion. */
static void scsi_bus_lock(void) {
  asm volatile("cli");
  while (scsi_bus_busy) {
    asm volatile("sti; hlt; cli");
  }
  scsi_bus_busy = 1;
  asm volatile("sti");
}

static void scsi_bus_unlock(void) {
  asm volatile("cli");
  scsi_bus_busy = 0;
  asm volatile("sti");
}

/* DMA bounce buffers (physically usable by the controller). */
static uint8_t scsi_cmd_buf[4096] __attribute__((aligned(4096)));
static uint8_t scsi_xfer_buf[SCSI_MAX_XFER] __attribute__((aligned(4096)));
static uint8_t scsi_status_buf[16] __attribute__((aligned(4096)));

static void scsi_dev_write(uint8_t reg, uint8_t val) {
  outb(scsi_base + reg, val);
}

static uint8_t scsi_dev_read(uint8_t reg) { return inb(scsi_base + reg); }

static void scsi_dma_write(uint8_t off, uint32_t val) {
  outl(scsi_base + off, val);
}

/* ---- low level helpers ---- */

static void scsi_esp_reset(void) {
  scsi_dev_write(ESP_CMD, ESP_CMD_RESET);
  scsi_dma_write(ESP_DMA_CMD, DMA_CMD_IDLE);
}

/* Program the chip transfer count (TCLO/MID/HI) on the SCSI core and the
 * PCI DMA controller (STC/SPA) for the next transfer. The chip STC must be
 * set BEFORE writing ESP_CMD: esp_run_cmd reloads the internal TC from it,
 * and do_cmd() only fires once TC reaches zero. */
static void scsi_dma_setup(uint32_t stc, uint32_t addr) {
  scsi_dev_write(ESP_TCLO, (uint8_t)stc);
  scsi_dev_write(ESP_TCMID, (uint8_t)(stc >> 8));
  scsi_dev_write(ESP_TCHI, (uint8_t)(stc >> 16));
  scsi_dma_write(ESP_DMA_STC, stc);
  scsi_dma_write(ESP_DMA_SPA, addr);
}

/* Collect interrupts until `mask` matches one already raised. */
static int scsi_wait_irq(uint8_t mask) {
  uint8_t got = 0;
  uint64_t ticks = 0;
  while (!(got & mask)) {
    if (scsi_dev_read(ESP_RSTAT) & ESP_RSTAT_INT) {
      got |= scsi_dev_read(ESP_RINTR);
    }
    if (++ticks > SCSI_INT_TIMEOUT) {
      serial_print("scsi: IRQ TIMEOUT got=0x");
      serial_print_hex(got);
      serial_print(" rstat=0x");
      serial_print_hex(scsi_dev_read(ESP_RSTAT));
      serial_print(" rintr=0x");
      serial_print_hex(scsi_dev_read(ESP_RINTR));
      serial_print(" dma_cmd=0x");
      serial_print_hex(inl(scsi_base + ESP_DMA_CMD));
      serial_print(" dma_stat=0x");
      serial_print_hex(inl(scsi_base + ESP_DMA_STAT));
      serial_print(" tc=0x");
      serial_print_hex((scsi_dev_read(ESP_TCHI) << 16) |
                       (scsi_dev_read(ESP_TCMID) << 8) |
                       scsi_dev_read(ESP_TCLO));
      serial_print(" fifo=0x");
      serial_print_hex(scsi_dev_read(ESP_RFLAGS));
      serial_print("\n");
      return -1;
    }
    asm volatile("pause");
  }
  return (int)got;
}

static void scsi_drain_irqs(void) {
  uint64_t ticks = 0;
  while ((scsi_dev_read(ESP_RSTAT) & ESP_RSTAT_INT) && ticks < 5000000ULL) {
    scsi_dev_read(ESP_RINTR);
    ticks++;
  }
}

/* ---- SCSI issue: select target 0 and run one command ---- */

static int scsi_issue(const uint8_t *cdb, int cdblen, uint8_t *data_in,
                      int inlen) {
  uint32_t cmd_addr = (uint32_t)(uintptr_t)scsi_cmd_buf;
  uint32_t stat_addr = (uint32_t)(uintptr_t)scsi_status_buf;
  int ir;

  if (!scsi_base) return -1;

  scsi_esp_reset();
  scsi_dev_write(ESP_WBUSID, 0); /* target 0 */

  /* stage { identify, cdb } and send it with SELATN + DMA. */
  scsi_cmd_buf[0] = SCSI_DEV_ID_BYTE;
  memcpy(scsi_cmd_buf + 1, cdb, cdblen);
  scsi_dma_setup(1 + (uint32_t)cdblen, cmd_addr);
  scsi_dma_write(ESP_DMA_CMD, DMA_CMD_START); /* load WBC/WAC first */
  scsi_dev_write(ESP_CMD, ESP_CMD_SELATN | ESP_CMD_DMA);

  ir = scsi_wait_irq(ESP_RINTR_DC | ESP_RINTR_BS | ESP_RINTR_FC);
  if (ir < 0) goto err;
  if (ir & ESP_RINTR_DC) return -2; /* no target on the bus */

  /* data-in phase. */
  if (inlen > 0) {
    uint8_t *dst = data_in ? data_in : scsi_xfer_buf;
    scsi_dma_setup((uint32_t)inlen, (uint32_t)(uintptr_t)dst);
    scsi_dma_write(ESP_DMA_CMD, DMA_CMD_START | DMA_CMD_FROM_DEV);
    scsi_dev_write(ESP_CMD, ESP_CMD_TI | ESP_CMD_DMA);
    ir = scsi_wait_irq(ESP_RINTR_BS | ESP_RINTR_DC);
    if (ir < 0) goto err;
    if (ir & ESP_RINTR_DC) return -2;
  }

  /* ICCS: read status + message byte. */
  scsi_dma_setup(2, stat_addr);
  scsi_dma_write(ESP_DMA_CMD, DMA_CMD_START | DMA_CMD_FROM_DEV);
  scsi_dev_write(ESP_CMD, ESP_CMD_ICCS | ESP_CMD_DMA);
  ir = scsi_wait_irq(ESP_RINTR_FC);
  if (ir < 0) goto err;

  scsi_drain_irqs();

  if (scsi_status_buf[0] != 0) return -3; /* CHECK CONDITION etc. */
  return 0;

err:
  scsi_esp_reset();
  return -1;
}

/* ---- public API ---- */

static int scsi_inquiry(void) {
  uint8_t cdb[6] = {0x12, 0x00, 0x00, 0x00, SCSI_ISIZE, 0x00};
  return scsi_issue(cdb, 6, scsi_xfer_buf, SCSI_ISIZE);
}

void scsi_init(void) {
  pci_device_t *dev;

  scsi_base = 0;
  dev = pci_get_device(SCSI_ESP_PCI_VENDOR, SCSI_ESP_PCI_DEVICE);
  if (!dev) {
    serial_print("scsi: am53c974 not found\n");
    return;
  }

  scsi_base = pci_read_config(dev->bus, dev->device, dev->function, 0x10) &
              0xFFFFFFFC;
  pci_write_config(dev->bus, dev->device, dev->function, 0x04,
                   (pci_read_config(dev->bus, dev->device, dev->function,
                                    0x04) &
                    ~0x7) |
                       0x7); /* enable I/O + memory + bus master */
  serial_print("scsi: am53c974 at io 0x");
  serial_print_hex(scsi_base);
  serial_print("\n");
}

int scsi_present(void) {
  int rc;
  if (!scsi_base) return 0;
  scsi_bus_lock();
  rc = scsi_inquiry();
  scsi_bus_unlock();
  return rc == 0;
}

int scsi_read_blocks(uint32_t lba, uint32_t count, uint8_t *buf) {
  uint8_t cdb[10];
  uint32_t max = SCSI_MAX_XFER / 512;
  uint32_t done = 0;
  int rc = 0;

  if (!scsi_base || count == 0) return -1;
  scsi_bus_lock();
  while (done < count) {
    uint32_t chunk = count - done;
    if (chunk > max) chunk = max;

    cdb[0] = 0x28; /* READ(10) */
    cdb[1] = 0;
    cdb[2] = (uint8_t)((lba + done) >> 24);
    cdb[3] = (uint8_t)((lba + done) >> 16);
    cdb[4] = (uint8_t)((lba + done) >> 8);
    cdb[5] = (uint8_t)(lba + done);
    cdb[6] = 0;
    cdb[7] = (uint8_t)(chunk >> 8);
    cdb[8] = (uint8_t)(chunk & 0xFF);
    cdb[9] = 0;

    rc = scsi_issue(cdb, 10, buf + done * 512, chunk * 512);
    if (rc != 0) {
      scsi_bus_unlock();
      return rc;
    }
    done += chunk;
  }
  scsi_bus_unlock();
  return 0;
}