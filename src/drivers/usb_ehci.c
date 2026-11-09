#include "usb_ehci.h"

#include "kernel_layout.h"
#include "io.h"
#include "kheap.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "usb_hid.h"
#include "usb_ehci_schedule.h"
#include "vga.h"
#include "vmm.h"
#include "keyboard.h"

#define EHCI_INTERFACE 0x20
#define EHCI_BAR0_OFFSET 0x10
#define EHCI_PCI_COMMAND 0x04
#define EHCI_PCI_MEMORY_ENABLE (1u << 1)
#define EHCI_PCI_BUS_MASTER (1u << 2)
#define EHCI_PCI_STATUS_RTA (1u << 28)
#define EHCI_PCI_STATUS_RMA (1u << 29)
#define EHCI_USBCMD_RUN (1u << 0)
#define EHCI_USBCMD_RESET (1u << 1)
#define EHCI_USBCMD_ASYNC_ENABLE (1u << 5)
#define EHCI_USBCMD_PERIODIC_ENABLE (1u << 4)
#define EHCI_USBCMD_PPCEE (1u << 15)
#define EHCI_USBSTS_HALTED (1u << 12)
#define EHCI_USBSTS_PERIODIC_STATUS (1u << 14)
#define EHCI_USBSTS_ASYNC_STATUS (1u << 15)
#define EHCI_USBSTS_HOST_ERROR (1u << 4)
#define EHCI_HCC_EECP_SHIFT 8
#define EHCI_HCC_EECP_MASK 0xFFu
#define EHCI_LEGACY_CAP_ID 0x01u
#define EHCI_LEGACY_BIOS_OWNED (1u << 16)
#define EHCI_LEGACY_OS_OWNED (1u << 24)
#define EHCI_LEGACY_SMI_STATUS_MASK (7u << 29)
#define EHCI_CONFIGURED (1u << 0)
#define EHCI_MAX_CONTROLLERS 8
#define EHCI_WAIT_SPINS 5000000u
#define EHCI_LINK_TERMINATE (1u << 0)
#define EHCI_LINK_QH (1u << 1)
#define EHCI_QH_HEAD (1u << 15)
#define EHCI_QH_SPEED_HIGH (2u << 12)
#define EHCI_QH_MAX_PACKET_64 (64u << 16)
#define EHCI_QH_DTC (1u << 14)
#define EHCI_QH_CONTROL_EP (1u << 27)
#define EHCI_QTD_ACTIVE (1u << 7)
#define EHCI_QTD_HALTED (1u << 6)
#define EHCI_QTD_BUFFER_ERROR (1u << 5)
#define EHCI_QTD_BABBLE (1u << 4)
#define EHCI_QTD_TRANSACTION_ERROR (1u << 3)
#define EHCI_QTD_PID_OUT (0u << 8)
#define EHCI_QTD_PID_IN (1u << 8)
#define EHCI_QTD_PID_SETUP (2u << 8)
#define EHCI_QTD_CERR_3 (3u << 10)
#define EHCI_QTD_IOC (1u << 15)
#define EHCI_QTD_TOGGLE (1u << 31)
#define EHCI_PORT_CONNECTED (1u << 0)
#define EHCI_PORT_ENABLE (1u << 2)
#define EHCI_PORT_RESET (1u << 8)
#define EHCI_PORT_LOW_SPEED (1u << 10)
#define EHCI_PORT_HIGH_SPEED (1u << 11)
#define EHCI_PORT_POWER (1u << 12)
#define EHCI_PORT_OWNER (1u << 13)
#define EHCI_PORT_CONNECT_CHANGE (1u << 1)
#define EHCI_PORT_ENABLE_CHANGE (1u << 3)
#define EHCI_PORT_OVERCURRENT_CHANGE (1u << 5)
#define EHCI_PORT_CHANGE_MASK (EHCI_PORT_CONNECT_CHANGE | \
                               EHCI_PORT_ENABLE_CHANGE | \
                               EHCI_PORT_OVERCURRENT_CHANGE)
#define EHCI_MAX_DMA_PAGES 8
#define EHCI_ROOT_HUB_VENDOR 0x8087
#define EHCI_ROOT_HUB_PRODUCT 0x0024
#define USB_DESC_DEVICE 1
#define USB_DESC_CONFIGURATION 2
#define USB_DESC_HUB 0x29
#define USB_CLASS_HUB 9
#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_ADDRESS 5
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_SET_INTERFACE 11
#define USB_REQ_GET_STATUS 0
#define USB_REQ_SET_FEATURE 3
#define USB_REQ_CLEAR_FEATURE 1
#define USB_FEATURE_PORT_POWER 8
#define USB_FEATURE_PORT_RESET 4
#define USB_FEATURE_C_PORT_CONNECTION 16
#define USB_FEATURE_C_PORT_RESET 20
#define EHCI_HUB_MAX_PORTS 16
#define EHCI_CONTROL_WAIT_SPINS 10000000u
#define EHCI_DESCRIPTOR_ATTEMPTS 3u



typedef struct {
  uint8_t address;
  uint8_t speed;
  uint8_t max_packet;
  uint8_t hub_address;
  uint8_t hub_port;
  uint8_t class_code;
} ehci_usb_device_t;

typedef struct {
  pci_device_t *pci;
  uint64_t bar;
  volatile uint32_t *operational;
  ehci_queue_head_t *async_head;
  uint64_t async_head_phys;
  ehci_queue_head_t *control_qh;
  uint64_t control_qh_phys;
  ehci_qtd_t *control_qtds;
  uint64_t control_qtds_phys;
  uint8_t *control_buffer;
  uint64_t control_buffer_phys;
  uint32_t *periodic_list;
  uint64_t periodic_list_phys;
  ehci_queue_head_t *interrupt_qh;
  uint64_t interrupt_qh_phys;
  ehci_qtd_t *interrupt_qtd;
  uint64_t interrupt_qtd_phys;
  uint8_t *report_buffer;
  uint64_t report_buffer_phys;
  uint16_t report_length;
  usb_hid_boot_interface_t hid_interface;
  void *dma_pages[EHCI_MAX_DMA_PAGES];
  uint8_t dma_page_count;
  uint32_t port_count;
  uint8_t cap_length;
  int started;
  int async_started;
  int keyboard_configured;
  uint8_t root_scan_ticks;
  volatile uint8_t root_scan_pending;
  uint16_t root_port_connected;
  uint8_t control_errors_logged;
  uint32_t report_count;
  int input_activity_reported;
} ehci_controller_t;

static ehci_controller_t ehci_controllers[EHCI_MAX_CONTROLLERS];
static int ehci_controller_total;
static int ehci_started_total;
static const char *ehci_status = "usb ehci not probed";

static void ehci_log(const char *message) {
  serial_print(message);
  serial_print("\n");
  vga_puts(message);
  vga_puts("\n");
}

static char *ehci_append_text(char *out, const char *text) {
  while (*text) *out++ = *text++;
  return out;
}

static char *ehci_append_hex(char *out, uint32_t value, uint8_t digits) {
  static const char hex[] = "0123456789abcdef";
  for (int shift = ((int)digits - 1) * 4; shift >= 0; shift -= 4)
    *out++ = hex[(value >> shift) & 0xF];
  return out;
}

static char *ehci_append_bdf(char *out, const pci_device_t *pci) {
  out = ehci_append_hex(out, pci->bus, 2);
  *out++ = ':';
  out = ehci_append_hex(out, pci->device, 2);
  *out++ = '.';
  return ehci_append_hex(out, pci->function, 1);
}

static void ehci_log_runtime_state(const ehci_controller_t *controller) {
  volatile uint32_t *op = controller->operational;
  char line[112];
  char *p = ehci_append_text(line, "[ehci] ");
  p = ehci_append_bdf(p, controller->pci);
  p = ehci_append_text(p, " CMD=");
  p = ehci_append_hex(p, op[0], 8);
  p = ehci_append_text(p, " STS=");
  p = ehci_append_hex(p, op[1], 8);
  p = ehci_append_text(p, " CF=");
  p = ehci_append_hex(p, op[0x10], 8);
  p = ehci_append_text(p, " ASYNC=");
  p = ehci_append_hex(p, op[0x18 / 4], 8);
  p = ehci_append_text(p, " PERIODIC=");
  p = ehci_append_hex(p, op[0x14 / 4], 8);
  p = ehci_append_text(p, " INTR=");
  p = ehci_append_hex(p, op[0x08 / 4], 8);
  *p = '\0';
  ehci_log(line);

  uint32_t port_count = ((volatile uint32_t *)
      phys_to_virt(controller->bar))[1] & 0xF;
  for (uint32_t port = 0; port < port_count; port++) {
    p = ehci_append_text(line, "[ehci] ");
    p = ehci_append_bdf(p, controller->pci);
    p = ehci_append_text(p, " PORT");
    p = ehci_append_hex(p, port, 1);
    *p++ = '=';
    p = ehci_append_hex(p, op[0x11 + port], 8);
    *p = '\0';
    ehci_log(line);
  }
}

static int ehci_wait_mask(volatile uint32_t *reg, uint32_t mask,
                          uint32_t expected) {
  for (uint32_t i = 0; i < EHCI_WAIT_SPINS; ++i) {
    if ((*reg & mask) == expected) return 0;
    __asm__ volatile("pause" ::: "memory");
  }
  return -1;
}

static void *ehci_dma_page(ehci_controller_t *controller) {
  if (!controller || controller->dma_page_count >= EHCI_MAX_DMA_PAGES)
    return NULL;
  /* Keep EHCI DMA objects in the kernel's already-claimed, low physical
   * heap arena. On the H61 target, standalone PMM frames land beyond that
   * arena and EHCI receives a PCI master abort while reading its qTDs. */
  void *page = kmalloc_a(PAGE_SIZE);
  if (!page) return NULL;
  uint64_t physical = virt_to_phys(page);
  if ((physical & (PAGE_SIZE - 1)) || physical >= 0x100000000ULL) {
    return NULL;
  }
  memset(page, 0, PAGE_SIZE);
  controller->dma_pages[controller->dma_page_count++] = page;
  return page;
}

static void ehci_release_dma_pages(ehci_controller_t *controller) {
  if (!controller) return;
  /* kheap owns these frames; leave them reserved for the life of the kernel. */
  while (controller->dma_page_count)
    controller->dma_pages[--controller->dma_page_count] = NULL;
  controller->async_head = NULL;
  controller->control_qh = NULL;
  controller->control_qtds = NULL;
  controller->control_buffer = NULL;
}

static int ehci_wait_bios_release(const pci_device_t *pci,
                                  uint8_t legacy_offset) {
  for (uint32_t i = 0; i < EHCI_WAIT_SPINS; ++i) {
    uint32_t legacy = pci_read_config(pci->bus, pci->device, pci->function,
                                     legacy_offset);
    if (!(legacy & EHCI_LEGACY_BIOS_OWNED)) return 0;
    __asm__ volatile("pause" ::: "memory");
  }
  return -1;
}

static void ehci_disable_legacy_smis(const pci_device_t *pci,
                                     uint8_t legacy_offset) {
  uint32_t control_status = pci_read_config(
      pci->bus, pci->device, pci->function, (uint8_t)(legacy_offset + 4));
  /* The SMI enables are RW bits 0-5 and 13-15. Write only observed W1C
   * ownership/PCI-command/BAR status bits back as ones; all other bits are
   * zero so RO shadows and reserved fields are not echoed into the register. */
  pci_write_config(pci->bus, pci->device, pci->function,
                   (uint8_t)(legacy_offset + 4),
                   control_status & EHCI_LEGACY_SMI_STATUS_MASK);
}

static int ehci_request_os_ownership(const pci_device_t *pci,
                                     uint32_t hccparams) {
  uint8_t offset = (hccparams >> EHCI_HCC_EECP_SHIFT) & EHCI_HCC_EECP_MASK;
  for (uint32_t visited = 0; offset && visited < 48; ++visited) {
    if (offset < 0x40 || offset > 0xFC || (offset & 3)) return -1;
    uint32_t capability =
        pci_read_config(pci->bus, pci->device, pci->function, offset);
    uint8_t id = capability & 0xFF;
    uint8_t next = (capability >> 8) & 0xFF;
    if (id == EHCI_LEGACY_CAP_ID) {
      if (offset > 0xF8) return -1;
      pci_write_config(pci->bus, pci->device, pci->function, offset,
                       capability | EHCI_LEGACY_OS_OWNED);
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
      if (ehci_wait_bios_release(pci, offset) < 0) return -1;
      ehci_disable_legacy_smis(pci, offset);
      return 0;
    }
    if (!next) return 0;
    uint16_t next_offset = offset + next;
    if (next_offset <= offset || next_offset > 0xFC) return -1;
    offset = (uint8_t)next_offset;
  }
  return offset ? -1 : 0;
}

static int ehci_read_bar0(const pci_device_t *pci, uint64_t *bar_out) {
  uint32_t low = pci_read_config(pci->bus, pci->device, pci->function,
                                 EHCI_BAR0_OFFSET);
  if (!low || low == 0xFFFFFFFF || (low & 1)) return -1;
  uint8_t type = (low >> 1) & 3;
  if (type == 0) {
    *bar_out = low & ~0xFULL;
  } else if (type == 2) {
    uint32_t high = pci_read_config(pci->bus, pci->device, pci->function,
                                    EHCI_BAR0_OFFSET + 4);
    *bar_out = ((uint64_t)high << 32) | (low & ~0xFULL);
  } else {
    return -1;
  }
  return *bar_out ? 0 : -1;
}

static int ehci_enable_mmio(const pci_device_t *pci) {
  uint16_t command = (uint16_t)pci_read_config(
      pci->bus, pci->device, pci->function, EHCI_PCI_COMMAND);
  if (!(command & EHCI_PCI_MEMORY_ENABLE)) {
    pci_write_config(pci->bus, pci->device, pci->function, EHCI_PCI_COMMAND,
                     command | EHCI_PCI_MEMORY_ENABLE);
    command = (uint16_t)pci_read_config(pci->bus, pci->device, pci->function,
                                        EHCI_PCI_COMMAND);
  }
  return (command & EHCI_PCI_MEMORY_ENABLE) ? 0 : -1;
}

static int ehci_map_registers(ehci_controller_t *controller) {
  uint64_t bar = controller->bar;
  if (!bar || bar >= DIRECT_MAP_SIZE || DIRECT_MAP_SIZE - bar < 0x200)
    return -1;

  uint64_t first_page = bar & ~((uint64_t)PAGE_SIZE - 1);
  uint64_t last_page = (bar + 0x1FF) & ~((uint64_t)PAGE_SIZE - 1);
  for (uint64_t page = first_page;; page += PAGE_SIZE) {
    if (vmm_map_page_in(kernel_directory, (void *)page, phys_to_virt(page),
                        PTE_P | PTE_W | PTE_PCD | PTE_NX) < 0)
      return -1;
    if (page == last_page) break;
  }

  volatile uint8_t *caps = (volatile uint8_t *)phys_to_virt(bar);
  controller->cap_length = caps[0];
  if (controller->cap_length < 0x10 || controller->cap_length > 0x40)
    return -1;
  controller->operational =
      (volatile uint32_t *)(caps + controller->cap_length);
  return 0;
}

static int ehci_start_controller(ehci_controller_t *controller) {
  volatile uint32_t *cap_regs =
      (volatile uint32_t *)phys_to_virt(controller->bar);
  uint32_t hcsparams = cap_regs[1];
  uint32_t hccparams = cap_regs[2];
  uint32_t port_count = hcsparams & 0xF;
  if (port_count == 0 || port_count > 15) return -1;

  if (ehci_request_os_ownership(controller->pci, hccparams) < 0) {
    ehci_log("USB EHCI: firmware ownership handoff timed out");
    return -1;
  }

  volatile uint32_t *op = controller->operational;
  uint32_t command = op[0];
  if (command & EHCI_USBCMD_RUN) {
    op[0] = command & ~EHCI_USBCMD_RUN;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
  }
  if (ehci_wait_mask(&op[1], EHCI_USBSTS_HALTED, EHCI_USBSTS_HALTED) < 0) {
    ehci_log("USB EHCI: controller did not halt");
    return -1;
  }

  command = op[0];
  op[0] = command | EHCI_USBCMD_RESET;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (ehci_wait_mask(&op[0], EHCI_USBCMD_RESET, 0) < 0) {
    ehci_log("USB EHCI: host-controller reset timed out");
    return -1;
  }
  if (ehci_wait_mask(&op[1], EHCI_USBSTS_HALTED, EHCI_USBSTS_HALTED) < 0) {
    ehci_log("USB EHCI: reset did not leave controller halted");
    return -1;
  }

  /* Clear CTRLDSSEGMENT (operational offset 0x10, dword index 4) to ensure
   * 64-bit capable host controllers keep data structure DMA in the low 4GB. */
  op[0x10 / 4] = 0;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);

  /* Start the host controller first. CONFIGFLAG is set only after the
   * controller and its DMA schedule are fully configured, as required by
   * the EHCI/PCH initialization sequence. */
  command = op[0] & EHCI_USBCMD_PPCEE;
  op[0] = command | EHCI_USBCMD_RUN;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (ehci_wait_mask(&op[1], EHCI_USBSTS_HALTED, 0) < 0) {
    op[0] &= ~EHCI_USBCMD_RUN;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    (void)ehci_wait_mask(&op[1], EHCI_USBSTS_HALTED, EHCI_USBSTS_HALTED);
    ehci_log("USB EHCI: host controller failed to start");
    return -1;
  }

  controller->started = 1;
  return 0;
}

static int ehci_start_async_schedule(ehci_controller_t *controller) {
  /* Keep every EHCI DMA object below 4 GiB. This works on controllers with or
   * without 64-bit addressing and keeps the first DMA milestone conservative. */
  void *schedule_page = ehci_dma_page(controller);
  void *qtd_page = ehci_dma_page(controller);
  void *buffer_page = ehci_dma_page(controller);
  if (!schedule_page || !qtd_page || !buffer_page) {
    ehci_release_dma_pages(controller);
    return -1;
  }
  ehci_queue_head_t *head = (ehci_queue_head_t *)schedule_page;
  uint64_t physical = virt_to_phys(head);
  if ((physical & 31u) || physical >= 0x100000000ULL) {
    ehci_release_dma_pages(controller);
    return -1;
  }

  controller->control_qh = (ehci_queue_head_t *)((uint8_t *)schedule_page +
                                                 sizeof(*head));
  controller->control_qh_phys = virt_to_phys(controller->control_qh);
  controller->control_qtds = (ehci_qtd_t *)qtd_page;
  controller->control_qtds_phys = virt_to_phys(qtd_page);
  controller->control_buffer = (uint8_t *)buffer_page;
  controller->control_buffer_phys = virt_to_phys(buffer_page);
  controller->async_head_phys = physical;
  controller->async_head = head;

  /* PCI Status keeps abort indications sticky across firmware handoff. Clear
   * only the W1C Received Target/Master Abort bits so the fatal-DMA log can
   * distinguish a new EHCI memory abort from stale firmware state. Preserve
   * the PCI Command half of the dword and write zero to all other status bits. */
  uint32_t command_status = pci_read_config(
      controller->pci->bus, controller->pci->device,
      controller->pci->function, EHCI_PCI_COMMAND);
  char pci_line[80];
  char *p = ehci_append_text(pci_line, "USB EHCI: PCI status before DMA=");
  p = ehci_append_hex(p, command_status >> 16, 4);
  *p = '\0';
  ehci_log(pci_line);
  pci_write_config(controller->pci->bus, controller->pci->device,
                    controller->pci->function, EHCI_PCI_COMMAND,
                    (command_status & 0xFFFFu) | EHCI_PCI_STATUS_RTA |
                        EHCI_PCI_STATUS_RMA);
  command_status = pci_read_config(
      controller->pci->bus, controller->pci->device,
      controller->pci->function, EHCI_PCI_COMMAND);
  p = ehci_append_text(pci_line, "USB EHCI: PCI status after clear=");
  p = ehci_append_hex(p, command_status >> 16, 4);
  *p = '\0';
  ehci_log(pci_line);

  /* The head plus an idle endpoint-zero QH form a circular asynchronous
   * schedule. Later control requests activate qTDs on the initialized QH.
   * MULT (bits 31:30) must be 0 for non-periodic QHs per EHCI 1.0 Table 3-20. */
  head->horizontal_link = (uint32_t)controller->control_qh_phys |
                          EHCI_LINK_QH;
  head->endpoint_characteristics =
      EHCI_QH_HEAD | EHCI_QH_SPEED_HIGH | EHCI_QH_MAX_PACKET_64;
  head->endpoint_capabilities = 0;
  head->next_qtd = EHCI_LINK_TERMINATE;
  head->alternate_next_qtd = EHCI_LINK_TERMINATE;
  controller->control_qh->horizontal_link =
      (uint32_t)physical | EHCI_LINK_QH;
  controller->control_qh->endpoint_characteristics =
      EHCI_QH_SPEED_HIGH | EHCI_QH_DTC | EHCI_QH_MAX_PACKET_64;
  controller->control_qh->endpoint_capabilities = 0;
  controller->control_qh->next_qtd = EHCI_LINK_TERMINATE;
  controller->control_qh->alternate_next_qtd = EHCI_LINK_TERMINATE;

  uint16_t command = (uint16_t)pci_read_config(
      controller->pci->bus, controller->pci->device,
      controller->pci->function, EHCI_PCI_COMMAND);
  pci_write_config(controller->pci->bus, controller->pci->device,
                    controller->pci->function, EHCI_PCI_COMMAND,
                    command | EHCI_PCI_MEMORY_ENABLE | EHCI_PCI_BUS_MASTER);
  command = (uint16_t)pci_read_config(controller->pci->bus,
                                      controller->pci->device,
                                      controller->pci->function,
                                      EHCI_PCI_COMMAND);
  if ((command & (EHCI_PCI_MEMORY_ENABLE | EHCI_PCI_BUS_MASTER)) !=
      (EHCI_PCI_MEMORY_ENABLE | EHCI_PCI_BUS_MASTER)) {
    ehci_release_dma_pages(controller);
    return -1;
  }

  controller->operational[0x10 / 4] = 0;
  controller->operational[0x18 / 4] = (uint32_t)physical;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  controller->operational[0] |= EHCI_USBCMD_ASYNC_ENABLE;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (ehci_wait_mask(&controller->operational[1], EHCI_USBSTS_ASYNC_STATUS,
                     EHCI_USBSTS_ASYNC_STATUS) < 0) {
    controller->operational[0] &= ~EHCI_USBCMD_ASYNC_ENABLE;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (ehci_wait_mask(&controller->operational[1],
                       EHCI_USBSTS_ASYNC_STATUS, 0) == 0) {
      uint16_t current = (uint16_t)pci_read_config(
          controller->pci->bus, controller->pci->device,
          controller->pci->function, EHCI_PCI_COMMAND);
      pci_write_config(controller->pci->bus, controller->pci->device,
                        controller->pci->function, EHCI_PCI_COMMAND,
                        current & ~EHCI_PCI_BUS_MASTER);
      ehci_release_dma_pages(controller);
      controller->async_head_phys = 0;
    }
    return -1;
  }
  controller->async_started = 1;
  return 0;
}

static int ehci_async_schedule_set(ehci_controller_t *controller, int enable) {
  if (controller->operational[1] & EHCI_USBSTS_HALTED) {
    if (!enable) return 0;
    return -1;
  }
  volatile uint32_t *command = &controller->operational[0];
  uint32_t requested = enable ? EHCI_USBCMD_ASYNC_ENABLE : 0;
  *command = (*command & ~EHCI_USBCMD_ASYNC_ENABLE) | requested;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (ehci_wait_mask(&controller->operational[1], EHCI_USBSTS_ASYNC_STATUS,
                     enable ? EHCI_USBSTS_ASYNC_STATUS : 0) < 0)
    return -1;
  return 0;
}

static void ehci_log_async_transition_failure(ehci_controller_t *controller,
                                              const char *stage) {
  if (!controller || controller->control_errors_logged >= 8) return;
  char line[128];
  char *p = ehci_append_text(line, "USB EHCI: async transition failed stage=");
  p = ehci_append_text(p, stage);
  p = ehci_append_text(p, " CMD=");
  p = ehci_append_hex(p, controller->operational[0], 8);
  p = ehci_append_text(p, " STS=");
  p = ehci_append_hex(p, controller->operational[1], 8);
  p = ehci_append_text(p, " ASS=");
  p = ehci_append_hex(p, (controller->operational[1] >> 15) & 1u, 1);
  *p = '\0';
  ehci_log(line);
  if ((controller->operational[1] &
       (EHCI_USBSTS_HOST_ERROR | EHCI_USBSTS_HALTED)) ==
      (EHCI_USBSTS_HOST_ERROR | EHCI_USBSTS_HALTED)) {
    uint32_t pci_command_status = pci_read_config(
        controller->pci->bus, controller->pci->device,
        controller->pci->function, EHCI_PCI_COMMAND);
    char dma_line[224];
    p = ehci_append_text(dma_line, "USB EHCI: fatal DMA PCI_STS=");
    p = ehci_append_hex(p, pci_command_status >> 16, 4);
    p = ehci_append_text(p, " ASYNC=");
    p = ehci_append_hex(p, controller->operational[0x18 / 4], 8);
    p = ehci_append_text(p, " SEG=");
    p = ehci_append_hex(p, controller->operational[0x10 / 4], 8);
    p = ehci_append_text(p, " qh_phys=");
    p = ehci_append_hex(p, (uint32_t)controller->control_qh_phys, 8);
    p = ehci_append_text(p, " qtd_phys=");
    p = ehci_append_hex(p, (uint32_t)controller->control_qtds_phys, 8);
    p = ehci_append_text(p, " buf_phys=");
    p = ehci_append_hex(p, (uint32_t)controller->control_buffer_phys, 8);
    *p = '\0';
    ehci_log(dma_line);

    p = ehci_append_text(dma_line, "USB EHCI: fatal qTD=");
    p = ehci_append_hex(p, controller->control_qtds[0].token, 8);
    *p++ = '/';
    p = ehci_append_hex(p, controller->control_qtds[1].token, 8);
    *p++ = '/';
    p = ehci_append_hex(p, controller->control_qtds[2].token, 8);
    p = ehci_append_text(p, " QH_CUR=");
    p = ehci_append_hex(p, controller->control_qh->current_qtd, 8);
    p = ehci_append_text(p, " QH_NEXT=");
    p = ehci_append_hex(p, controller->control_qh->next_qtd, 8);
    p = ehci_append_text(p, " QH_TOKEN=");
    p = ehci_append_hex(p, controller->control_qh->token, 8);
    *p = '\0';
    ehci_log(dma_line);
  }
  controller->control_errors_logged++;
}

static uint32_t ehci_control_endpoint_state(const ehci_usb_device_t *device) {
  uint32_t state = device->address | ((uint32_t)device->speed << 12) |
                   EHCI_QH_DTC |
                   ((uint32_t)device->max_packet << 16);
  if (device->speed != 2) state |= EHCI_QH_CONTROL_EP;
  return state;
}

static int ehci_control_transfer(ehci_controller_t *controller,
                                 const ehci_usb_device_t *device,
                                 uint8_t request_type, uint8_t request,
                                 uint16_t value, uint16_t index,
                                 uint16_t length) {
  if (!controller || !controller->async_started || !device ||
      !controller->control_qh || !controller->control_qtds ||
      !controller->control_buffer || length > PAGE_SIZE - 64)
    return -1;

  ehci_qtd_t *qtd = controller->control_qtds;
  uint8_t *setup = controller->control_buffer;
  uint64_t setup_phys = controller->control_buffer_phys;
  uint8_t *data = setup + 64;
  uint64_t data_phys = setup_phys + 64;
  memset(qtd, 0, sizeof(*qtd) * 3);
  memset(setup, 0, 64);
  if (length) memset(data, 0, length);
  /* HID SET_REPORT(Output, report 0): Caps Lock LED bit. */
  if (request_type == 0x21 && request == 9 && value == 0x0200 && length == 1)
    data[0] = 0x02;
  setup[0] = request_type;
  setup[1] = request;
  setup[2] = (uint8_t)value;
  setup[3] = (uint8_t)(value >> 8);
  setup[4] = (uint8_t)index;
  setup[5] = (uint8_t)(index >> 8);
  setup[6] = (uint8_t)length;
  setup[7] = (uint8_t)(length >> 8);

  uint64_t qtd_phys = controller->control_qtds_phys;
  uint64_t status_phys = qtd_phys + sizeof(*qtd) * (length ? 2u : 1u);
  qtd[0].next_qtd = (uint32_t)(qtd_phys + sizeof(*qtd));
  qtd[0].alternate_next_qtd = EHCI_LINK_TERMINATE;
  qtd[0].token = EHCI_QTD_ACTIVE | EHCI_QTD_CERR_3 | EHCI_QTD_PID_SETUP |
                 (8u << 16);
  qtd[0].buffer[0] = (uint32_t)setup_phys;

  uint32_t status_index;
  if (length) {
    qtd[1].next_qtd = (uint32_t)status_phys;
    qtd[1].alternate_next_qtd = (uint32_t)status_phys;
    qtd[1].token = EHCI_QTD_ACTIVE | EHCI_QTD_CERR_3 |
                   ((request_type & 0x80) ? EHCI_QTD_PID_IN
                                          : EHCI_QTD_PID_OUT) |
                   EHCI_QTD_TOGGLE | ((uint32_t)length << 16);
    qtd[1].buffer[0] = (uint32_t)data_phys;
    status_index = 2;
  } else {
    status_index = 1;
  }
  qtd[status_index].next_qtd = EHCI_LINK_TERMINATE;
  qtd[status_index].alternate_next_qtd = EHCI_LINK_TERMINATE;
  qtd[status_index].token = EHCI_QTD_ACTIVE | EHCI_QTD_CERR_3 |
                            EHCI_QTD_IOC | EHCI_QTD_TOGGLE |
                            ((!length || !(request_type & 0x80))
                                 ? EHCI_QTD_PID_IN
                                 : EHCI_QTD_PID_OUT);

  uint32_t endpoint = ehci_control_endpoint_state(device);
  uint32_t capabilities = 0; /* MULT must be 0 for control/bulk QHs per EHCI 1.0 Table 3-20 */
  if (device->speed != 2 && device->hub_address) {
    capabilities |= (uint32_t)device->hub_address << 16;
    capabilities |= (uint32_t)device->hub_port << 23;
  }
  if (ehci_async_schedule_set(controller, 0) < 0) {
    ehci_log_async_transition_failure(controller, "stop-before-qh");
    return -1;
  }
  ehci_queue_head_t *qh = controller->control_qh;
  qh->endpoint_characteristics = endpoint;
  qh->endpoint_capabilities = capabilities;
  qh->current_qtd = 0;
  qh->next_qtd = (uint32_t)qtd_phys;
  qh->alternate_next_qtd = EHCI_LINK_TERMINATE;
  qh->token = 0;
  memset(qh->buffer, 0, sizeof(qh->buffer));
  memset(qh->extended_buffer, 0, sizeof(qh->extended_buffer));
  __atomic_thread_fence(__ATOMIC_RELEASE);
  if (ehci_async_schedule_set(controller, 1) < 0) {
    ehci_log_async_transition_failure(controller, "start-request");
    return -1;
  }

  int completed = 0;
  for (uint32_t spin = 0; spin < EHCI_CONTROL_WAIT_SPINS; ++spin) {
    uint32_t token = qtd[status_index].token;
    if (!(token & EHCI_QTD_ACTIVE)) {
      completed = !(token & (EHCI_QTD_HALTED | EHCI_QTD_BUFFER_ERROR |
                             EHCI_QTD_BABBLE | EHCI_QTD_TRANSACTION_ERROR));
      break;
    }
    if (controller->operational[1] &
        (EHCI_USBSTS_HOST_ERROR | EHCI_USBSTS_HALTED))
      break;
    __asm__ volatile("pause" ::: "memory");
  }

  if (ehci_async_schedule_set(controller, 0) < 0) {
    ehci_log_async_transition_failure(controller, "stop-after-transfer");
    controller->async_started = 0;
    return -1;
  }
  /* Snapshot hardware-owned state before recovering the schedule. If an
   * earlier qTD (SETUP or DATA) failed, the final STATUS qTD can remain ACTIVE
   * and conceal the actual error. */
  uint32_t setup_token = qtd[0].token;
  uint32_t data_token = length ? qtd[1].token : 0;
  uint32_t status_token = qtd[status_index].token;
  uint32_t qh_token = controller->control_qh->token;
  uint32_t usb_status = controller->operational[1];
  if (!completed) {
    for (uint32_t i = 0; i <= status_index; ++i)
      qtd[i].token &= ~EHCI_QTD_ACTIVE;
  }
  qh->current_qtd = 0;
  qh->next_qtd = EHCI_LINK_TERMINATE;
  qh->alternate_next_qtd = EHCI_LINK_TERMINATE;
  qh->token = 0;
  memset(qh->buffer, 0, sizeof(qh->buffer));
  memset(qh->extended_buffer, 0, sizeof(qh->extended_buffer));
  __atomic_thread_fence(__ATOMIC_RELEASE);
  if (ehci_async_schedule_set(controller, 1) < 0) {
    ehci_log_async_transition_failure(controller, "restart-after-recovery");
    controller->async_started = 0;
    return -1;
  }
  if (!completed && controller->control_errors_logged < 8) {
    char line[192];
    char *p = ehci_append_text(line, "USB EHCI: control transfer failed req=");
    p = ehci_append_hex(p, request, 2);
    p = ehci_append_text(p, " type=");
    p = ehci_append_hex(p, request_type, 2);
    p = ehci_append_text(p, " addr=");
    p = ehci_append_hex(p, device->address, 2);
    p = ehci_append_text(p, " value=");
    p = ehci_append_hex(p, value, 4);
    p = ehci_append_text(p, " index=");
    p = ehci_append_hex(p, index, 4);
    p = ehci_append_text(p, " len=");
    p = ehci_append_hex(p, length, 4);
    p = ehci_append_text(p, " qtd_setup=");
    p = ehci_append_hex(p, setup_token, 8);
    if (length) {
      p = ehci_append_text(p, " qtd_data=");
      p = ehci_append_hex(p, data_token, 8);
    }
    p = ehci_append_text(p, " qtd_status=");
    p = ehci_append_hex(p, status_token, 8);
    p = ehci_append_text(p, " qh=");
    p = ehci_append_hex(p, qh_token, 8);
    p = ehci_append_text(p, " usbsts=");
    p = ehci_append_hex(p, usb_status, 8);
    *p = '\0';
    ehci_log(line);
    controller->control_errors_logged++;
  }
  return completed ? 0 : -1;
}

static uint16_t ehci_read_le16(const uint8_t *bytes) {
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static int ehci_get_descriptor_retry(ehci_controller_t *controller,
                                     ehci_usb_device_t *device,
                                     uint8_t request_type,
                                     uint8_t descriptor_type,
                                     uint8_t descriptor_index,
                                     uint16_t language_or_port,
                                     uint16_t length) {
  uint16_t value = ((uint16_t)descriptor_type << 8) | descriptor_index;
  for (uint32_t attempt = 0; attempt < EHCI_DESCRIPTOR_ATTEMPTS; ++attempt) {
    if (ehci_control_transfer(controller, device, request_type,
                              USB_REQ_GET_DESCRIPTOR, value,
                              language_or_port, length) == 0)
      return 0;
  }
  return -1;
}

static void ehci_delay_ms(uint32_t milliseconds) {
  while (milliseconds) {
    uint32_t slice = milliseconds > 50 ? 50 : milliseconds;
    uint16_t count = (uint16_t)(1193u * slice);
    uint8_t speaker = inb(0x61);
    outb(0x61, speaker & (uint8_t)~0x03u);
    outb(0x43, 0xB0); /* PIT channel 2, lo/hi byte, one-shot mode. */
    outb(0x42, (uint8_t)count);
    outb(0x42, (uint8_t)(count >> 8));
    outb(0x61, (speaker & (uint8_t)~0x02u) | 0x01u);
    while (!(inb(0x61) & 0x20u)) __asm__ volatile("pause" ::: "memory");
    outb(0x61, speaker & (uint8_t)~0x01u);
    milliseconds -= slice;
  }
}

static void ehci_write_port_control(volatile uint32_t *portsc,
                                    uint32_t value) {
  *portsc = value & ~EHCI_PORT_CHANGE_MASK;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static int ehci_reset_root_port(ehci_controller_t *controller,
                                uint32_t port_index) {
  volatile uint32_t *portsc = &controller->operational[0x44 / 4 + port_index];
  uint32_t status = *portsc;
  if (!(status & EHCI_PORT_CONNECTED)) return -1;
  if (status & EHCI_PORT_OWNER) {
    ehci_write_port_control(portsc, status & ~EHCI_PORT_OWNER);
    status = *portsc;
  }
  if (!(status & EHCI_PORT_ENABLE)) {
    uint32_t control = status & (EHCI_PORT_POWER | EHCI_PORT_OWNER);
    ehci_write_port_control(portsc, control | EHCI_PORT_RESET);
    ehci_delay_ms(50);
    ehci_write_port_control(portsc, control);
    /* Do not infer reset completion from a fixed delay alone. EHCI defines
     * PR clearing as the completion handshake; Linux follows the same rule. */
    if (ehci_wait_mask(portsc, EHCI_PORT_RESET, 0) < 0) {
      ehci_log("USB EHCI: root port reset bit did not clear");
      return -1;
    }
    ehci_delay_ms(10);
    status = *portsc;
  }
  if (!(status & EHCI_PORT_CONNECTED) || !(status & EHCI_PORT_ENABLE))
    return -1;
  char line[64];
  char *p = ehci_append_text(line, "USB EHCI: root port reset complete status=");
  p = ehci_append_hex(p, status, 8);
  *p = '\0';
  ehci_log(line);
  return 0;
}

static void ehci_set_device_address(ehci_usb_device_t *device,
                                    uint8_t address) {
  device->address = address;
  __atomic_thread_fence(__ATOMIC_RELEASE);
}

static void ehci_log_initial_descriptor_bytes(ehci_controller_t *controller,
                                              const uint8_t *descriptor) {
  char line[192];
  char *p = ehci_append_text(line, "USB EHCI: initial descriptor bytes=");
  for (uint32_t i = 0; i < 8; ++i) {
    if (i) *p++ = ' ';
    p = ehci_append_hex(p, descriptor[i], 2);
  }
  if (controller->control_qtds) {
    ehci_qtd_t *qtd = controller->control_qtds;
    p = ehci_append_text(p, " residual=");
    p = ehci_append_hex(p, (qtd[1].token >> 16) & 0x7fffu, 4);
    p = ehci_append_text(p, " tokens=");
    p = ehci_append_hex(p, qtd[0].token, 8);
    *p++ = '/';
    p = ehci_append_hex(p, qtd[1].token, 8);
    *p++ = '/';
    p = ehci_append_hex(p, qtd[2].token, 8);
    p = ehci_append_text(p, " qh_cur=");
    p = ehci_append_hex(p, controller->control_qh->current_qtd, 8);
    p = ehci_append_text(p, " qh_tok=");
    p = ehci_append_hex(p, controller->control_qh->token, 8);
  }
  *p = '\0';
  ehci_log(line);
}

static int ehci_get_device_descriptor(ehci_controller_t *controller,
                                      ehci_usb_device_t *device,
                                      uint8_t *descriptor, uint16_t length) {
  if (length > PAGE_SIZE - 64 ||
      ehci_get_descriptor_retry(controller, device, 0x80, USB_DESC_DEVICE, 0,
                                0, length) < 0)
    return -1;
  memcpy(descriptor, controller->control_buffer + 64, length);
  return 0;
}

static int ehci_set_configuration(ehci_controller_t *controller,
                                  ehci_usb_device_t *device,
                                  uint8_t configuration) {
  return ehci_control_transfer(controller, device, 0x00,
                               USB_REQ_SET_CONFIGURATION, configuration, 0,
                               0);
}

static int ehci_get_configuration(ehci_controller_t *controller,
                                  ehci_usb_device_t *device,
                                  uint8_t *descriptor, uint16_t length) {
  if (length > PAGE_SIZE - 64 ||
      ehci_get_descriptor_retry(controller, device, 0x80,
                                USB_DESC_CONFIGURATION, 0, 0, length) < 0)
    return -1;
  memcpy(descriptor, controller->control_buffer + 64, length);
  return 0;
}

static int ehci_start_periodic_keyboard(ehci_controller_t *controller,
                                        const ehci_usb_device_t *device,
                                        const usb_hid_boot_interface_t *hid) {
  uint8_t endpoint = hid->endpoint_address & 0x0f;
  if (!endpoint || !(hid->endpoint_address & 0x80) ||
      hid->max_packet_size < 8 || hid->max_packet_size > PAGE_SIZE ||
      !hid->interval)
    return -1;

  void *frame_page = ehci_dma_page(controller);
  void *qh_page = ehci_dma_page(controller);
  void *qtd_page = ehci_dma_page(controller);
  void *report_page = ehci_dma_page(controller);
  if (!frame_page || !qh_page || !qtd_page || !report_page) return -1;
  controller->periodic_list = (uint32_t *)frame_page;
  controller->periodic_list_phys = virt_to_phys(frame_page);
  controller->interrupt_qh = (ehci_queue_head_t *)qh_page;
  controller->interrupt_qh_phys = virt_to_phys(qh_page);
  controller->interrupt_qtd = (ehci_qtd_t *)qtd_page;
  controller->interrupt_qtd_phys = virt_to_phys(qtd_page);
  controller->report_buffer = (uint8_t *)report_page;
  controller->report_buffer_phys = virt_to_phys(report_page);
  controller->report_length = hid->max_packet_size;
  controller->hid_interface = *hid;

  uint32_t endpoint_state = device->address |
                            ((uint32_t)endpoint << 8) |
                            ((uint32_t)device->speed << 12) |
                            ((uint32_t)hid->max_packet_size << 16);
  uint32_t caps;
  uint32_t frame_stride;
  if (ehci_periodic_schedule(device->speed, hid->interval,
                             device->hub_address, device->hub_port,
                             &caps, &frame_stride) < 0)
    return -1;

  ehci_queue_head_t *qh = controller->interrupt_qh;
  memset(qh, 0, sizeof(*qh));
  qh->horizontal_link = EHCI_LINK_TERMINATE;
  qh->endpoint_characteristics = endpoint_state;
  qh->endpoint_capabilities = caps;
  qh->next_qtd = (uint32_t)controller->interrupt_qtd_phys;
  qh->alternate_next_qtd = EHCI_LINK_TERMINATE;
  qh->token = 0;

  ehci_qtd_t *qtd = controller->interrupt_qtd;
  memset(qtd, 0, sizeof(*qtd));
  qtd->next_qtd = EHCI_LINK_TERMINATE;
  qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;
  qtd->token = EHCI_QTD_ACTIVE | EHCI_QTD_CERR_3 | EHCI_QTD_PID_IN |
               EHCI_QTD_IOC | ((uint32_t)controller->report_length << 16);
  qtd->buffer[0] = (uint32_t)controller->report_buffer_phys;

  for (uint32_t frame = 0; frame < 1024; ++frame)
    controller->periodic_list[frame] = EHCI_LINK_TERMINATE;
  uint32_t link = (uint32_t)controller->interrupt_qh_phys | EHCI_LINK_QH;
  for (uint32_t frame = 0; frame < 1024; frame += frame_stride)
    controller->periodic_list[frame] = link;

  if (ehci_wait_mask(&controller->operational[1], EHCI_USBSTS_PERIODIC_STATUS,
                     0) < 0)
    return -1;
  controller->operational[0x14 / 4] =
      (uint32_t)controller->periodic_list_phys;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  controller->operational[0] |= EHCI_USBCMD_PERIODIC_ENABLE;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (ehci_wait_mask(&controller->operational[1],
                     EHCI_USBSTS_PERIODIC_STATUS,
                     EHCI_USBSTS_PERIODIC_STATUS) < 0) {
    controller->operational[0] &= ~EHCI_USBCMD_PERIODIC_ENABLE;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    (void)ehci_wait_mask(&controller->operational[1],
                         EHCI_USBSTS_PERIODIC_STATUS, 0);
    return -1;
  }

  controller->keyboard_configured = 1;
  keyboard_set_usb_active(true);
  serial_set_input_enabled(0);
  serial_print("USB EHCI: periodic keyboard schedule active\n");
  return 0;
}

static int ehci_configure_hid_device(ehci_controller_t *controller,
                                     ehci_usb_device_t *device) {
  uint8_t *descriptor = controller->control_buffer + 64;
  if (ehci_get_configuration(controller, device, descriptor, 9) < 0 ||
      descriptor[0] < 9 || descriptor[1] != USB_DESC_CONFIGURATION)
    return -1;
  uint16_t total = ehci_read_le16(descriptor + 2);
  if (total < 9 || total > PAGE_SIZE - 64 ||
      ehci_get_configuration(controller, device, descriptor, total) < 0)
    return -1;

  usb_hid_boot_interface_t hid = {0};
  if (usb_hid_find_boot_interface(descriptor, total, &hid) < 0 ||
      ehci_set_configuration(controller, device, hid.configuration_value) < 0 ||
      ehci_control_transfer(controller, device, 0x21, 11, 0,
                            hid.interface_number, 0) < 0)
    return -1;

  if (hid.protocol != 1) return -1;
  if (ehci_control_transfer(controller, device, 0x21, 9, 0x0200,
                            hid.interface_number, 1) == 0)
    ehci_log("USB EHCI: Caps Lock LED report sent");
  else
    ehci_log("USB EHCI: Caps Lock LED report failed");
  return ehci_start_periodic_keyboard(controller, device, &hid);
}

static int ehci_address_usb_device(ehci_controller_t *controller,
                                   ehci_usb_device_t *device,
                                   uint8_t address, uint16_t *vendor,
                                   uint16_t *product) {
  uint8_t descriptor[18] = {0};
  device->address = 0;
  device->max_packet = device->speed == 2 ? 64 : 8;
  if (ehci_get_device_descriptor(controller, device, descriptor, 8) < 0) {
    ehci_log("USB EHCI: initial device descriptor request failed");
    return -1;
  }
  if (descriptor[0] < 8 || descriptor[1] != USB_DESC_DEVICE) {
    ehci_log_initial_descriptor_bytes(controller, descriptor);
    ehci_log("USB EHCI: initial device descriptor completed but is invalid");
    return -1;
  }
  if (descriptor[7] != 8 && descriptor[7] != 16 && descriptor[7] != 32 &&
      descriptor[7] != 64)
    return -1;
  device->max_packet = descriptor[7];
  if (ehci_control_transfer(controller, device, 0x00, USB_REQ_SET_ADDRESS,
                            address, 0, 0) < 0)
    return -1;
  ehci_set_device_address(device, address);
  ehci_delay_ms(2);

  if (ehci_get_device_descriptor(controller, device, descriptor, 18) < 0 ||
      descriptor[0] < 18 || descriptor[1] != USB_DESC_DEVICE ||
      descriptor[7] != device->max_packet)
    return -1;
  *vendor = ehci_read_le16(descriptor + 8);
  *product = ehci_read_le16(descriptor + 10);
  device->class_code = descriptor[4];
  return 0;
}

static int ehci_read_hub_port_status(ehci_controller_t *controller,
                                     ehci_usb_device_t *hub,
                                     uint8_t port, uint32_t *status_out) {
  if (ehci_control_transfer(controller, hub, 0xa3, USB_REQ_GET_STATUS, 0,
                            port, 4) < 0)
    return -1;
  *status_out = (uint32_t)controller->control_buffer[64] |
                ((uint32_t)controller->control_buffer[65] << 8) |
                ((uint32_t)controller->control_buffer[66] << 16) |
                ((uint32_t)controller->control_buffer[67] << 24);
  return 0;
}

static int ehci_find_hub_configuration(const uint8_t *descriptor,
                                       uint16_t length,
                                       uint8_t *configuration) {
  if (!descriptor || !configuration || length < 9 || descriptor[0] < 9 ||
      descriptor[1] != USB_DESC_CONFIGURATION || descriptor[5] == 0)
    return -1;
  uint16_t total = ehci_read_le16(descriptor + 2);
  if (total > length || total < descriptor[0]) return -1;
  for (size_t offset = descriptor[0]; offset < total;) {
    if (total - offset < 2) return -1;
    uint8_t size = descriptor[offset];
    uint8_t type = descriptor[offset + 1];
    if (size < 2 || size > total - offset) return -1;
    if (type == 4) {
      if (size < 9) return -1;
      if (descriptor[offset + 3] == 0 && descriptor[offset + 5] ==
                                                USB_CLASS_HUB) {
        *configuration = descriptor[5];
        return 0;
      }
    }
    offset += size;
  }
  return -1;
}

static int ehci_enumerate_device(ehci_controller_t *controller,
                                ehci_usb_device_t *device, uint8_t address,
                                uint8_t depth, uint8_t *next_address);

static int ehci_enumerate_hub(ehci_controller_t *controller,
                              ehci_usb_device_t *hub, uint8_t depth,
                              uint8_t *next_address) {
  if (depth > 2) return -1;
  uint8_t descriptor[9] = {0};
  uint8_t *config = controller->control_buffer + 64;
  if (ehci_get_configuration(controller, hub, config, 9) < 0 ||
      config[0] < 9 || config[1] != USB_DESC_CONFIGURATION)
    return -1;
  uint16_t total = ehci_read_le16(config + 2);
  if (total < 9 || total > PAGE_SIZE - 64 ||
      ehci_get_configuration(controller, hub, config, total) < 0)
    return -1;
  uint8_t configuration = 0;
  if (ehci_find_hub_configuration(config, total, &configuration) < 0 ||
      ehci_set_configuration(controller, hub, configuration) < 0)
    return -1;

  /* USB 2.0 leaves a hub's response to class requests undefined until it is
   * configured. Fetch the hub descriptor only after SET_CONFIGURATION. */
  if (ehci_get_descriptor_retry(controller, hub, 0xA0, USB_DESC_HUB, 0,
                                0, 7) < 0)
    return -1;
  memcpy(descriptor, controller->control_buffer + 64, 7);
  if (descriptor[0] < 7 || descriptor[1] != USB_DESC_HUB ||
      descriptor[2] == 0 || descriptor[2] > EHCI_HUB_MAX_PORTS)
    return -1;
  uint8_t port_count = descriptor[2];

  serial_print("USB EHCI: hub address ");
  serial_print_hex(hub->address);
  serial_print(" ports ");
  serial_print_hex(port_count);
  serial_print("\n");

  for (uint8_t port = 1; port <= port_count; ++port) {
    if (ehci_control_transfer(controller, hub, 0x23, USB_REQ_SET_FEATURE,
                              USB_FEATURE_PORT_POWER, port, 0) < 0)
      return -1;
  }
  uint32_t power_delay = (uint32_t)descriptor[5] * 2u;
  ehci_delay_ms(power_delay ? power_delay : 20);

  for (uint8_t port = 1; port <= port_count; ++port) {
    uint32_t port_status;
    if (ehci_read_hub_port_status(controller, hub, port, &port_status) < 0)
      continue;
    uint16_t status = (uint16_t)port_status;
    uint16_t changes = (uint16_t)(port_status >> 16);
    if (changes & 1u)
      (void)ehci_control_transfer(controller, hub, 0x23,
                                  USB_REQ_CLEAR_FEATURE,
                                  USB_FEATURE_C_PORT_CONNECTION, port, 0);
    if (!(status & 1u)) continue;

    if (ehci_control_transfer(controller, hub, 0x23, USB_REQ_SET_FEATURE,
                              USB_FEATURE_PORT_RESET, port, 0) < 0)
      continue;
    ehci_delay_ms(50);
    int reset_done = 0;
    for (uint32_t attempt = 0; attempt < 10; ++attempt) {
      if (ehci_read_hub_port_status(controller, hub, port, &port_status) < 0)
        break;
      status = (uint16_t)port_status;
      changes = (uint16_t)(port_status >> 16);
      if (!(status & (1u << 4)) && (changes & (1u << 4))) {
        reset_done = 1;
        break;
      }
      ehci_delay_ms(10);
    }
    if (!reset_done) continue;
    (void)ehci_control_transfer(controller, hub, 0x23,
                                USB_REQ_CLEAR_FEATURE,
                                USB_FEATURE_C_PORT_RESET, port, 0);
    if (ehci_read_hub_port_status(controller, hub, port, &port_status) < 0)
      continue;
    status = (uint16_t)port_status;
    if (!(status & (1u << 1)) || *next_address == 0 || *next_address > 126)
      continue;

    ehci_usb_device_t child = {
        .address = 0,
        .speed = (status & (1u << 10)) ? 2 : ((status & (1u << 9)) ? 1 : 0),
        .max_packet = 0,
        .hub_address = hub->address,
        .hub_port = port,
    };
    uint8_t child_address = (*next_address)++;
    if (ehci_enumerate_device(controller, &child, child_address,
                              (uint8_t)(depth + 1), next_address) == 0)
      return 0;
  }
  return -1;
}

static int ehci_enumerate_device(ehci_controller_t *controller,
                                ehci_usb_device_t *device, uint8_t address,
                                uint8_t depth, uint8_t *next_address) {
  uint16_t vendor = 0, product = 0;
  if (ehci_address_usb_device(controller, device, address, &vendor, &product) <
      0)
    return -1;
  if ((vendor == EHCI_ROOT_HUB_VENDOR &&
       product == EHCI_ROOT_HUB_PRODUCT) || device->class_code == USB_CLASS_HUB)
    return ehci_enumerate_hub(controller, device, depth, next_address);

  if (ehci_configure_hid_device(controller, device) < 0) return -1;
  serial_print("USB EHCI: configured boot keyboard address ");
  serial_print_hex(device->address);
  serial_print(" VID:PID ");
  serial_print_hex(vendor);
  serial_print(":");
  serial_print_hex(product);
  serial_print("\n");
  return 0;
}

static void ehci_enumerate_root_ports(ehci_controller_t *controller) {
  volatile uint32_t *cap = (volatile uint32_t *)phys_to_virt(controller->bar);
  uint32_t hcsparams = cap[1];
  uint32_t debug_port = (hcsparams >> 20) & 0x0f;
  controller->port_count = hcsparams & 0x0f;
  uint8_t next_address = 1;
  for (uint32_t port = 0; port < controller->port_count; ++port) {
    if (debug_port && port + 1 == debug_port) continue;
    volatile uint32_t *portsc =
        &controller->operational[0x44 / 4 + port];
    uint32_t status = *portsc;
    if (status & EHCI_PORT_CONNECTED)
      controller->root_port_connected |= (uint16_t)(1u << port);
    else
      controller->root_port_connected &= (uint16_t)~(1u << port);
    if (!(status & EHCI_PORT_CONNECTED)) continue;
    char line[64];
    char *p = ehci_append_text(line, "USB EHCI: connected root port ");
    p = ehci_append_hex(p, port + 1, 1);
    p = ehci_append_text(p, " status=");
    p = ehci_append_hex(p, status, 8);
    *p = '\0';
    ehci_log(line);
    if (ehci_reset_root_port(controller, port) < 0) {
      ehci_log("USB EHCI: root port reset/enable failed");
      continue;
    }
    if (next_address > 126)
      continue;
    ehci_usb_device_t root = {
        .address = 0,
        .speed = 2,
        .max_packet = 64,
        .hub_address = 0,
        .hub_port = 0,
    };
    uint8_t address = next_address++;
    if (ehci_enumerate_device(controller, &root, address, 0,
                              &next_address) == 0)
      return;
    ehci_log("USB EHCI: root device enumeration failed");
  }
}

static volatile int ehci_poll_active = 0;

void usb_ehci_poll(void) {
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
  if (ehci_poll_active) {
    if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
    return;
  }
  ehci_poll_active = 1;

  for (int index = 0; index < ehci_controller_total; ++index) {
    ehci_controller_t *controller = &ehci_controllers[index];
    if (controller->started && !controller->keyboard_configured &&
        ++controller->root_scan_ticks >= 10) {
      controller->root_scan_ticks = 0;
      volatile uint32_t *cap = (volatile uint32_t *)phys_to_virt(controller->bar);
      uint32_t ports = cap[1] & 0x0f;
      uint32_t debug_port = (cap[1] >> 20) & 0x0f;
      for (uint32_t port = 0; port < ports; ++port) {
        if (debug_port && port + 1 == debug_port) continue;
        uint16_t bit = (uint16_t)(1u << port);
        uint32_t status = controller->operational[0x11 + port];
        if (!(status & EHCI_PORT_CONNECTED)) {
          controller->root_port_connected &= (uint16_t)~bit;
        } else if (!(controller->root_port_connected & bit)) {
          controller->root_port_connected |= bit;
          controller->root_scan_pending = 1;
        }
      }
    }
    if (!controller->keyboard_configured || !controller->interrupt_qtd ||
        !controller->report_buffer)
      continue;

    /* Clear any transient interrupt or host error status bits */
    uint32_t sts = controller->operational[1];
    if (sts & (EHCI_USBSTS_HOST_ERROR | (1u << 1) | (1u << 2))) {
      controller->operational[1] = sts;
    }

    uint32_t token = controller->interrupt_qtd->token;
    if (token & EHCI_QTD_ACTIVE) continue;

    if (!(token & (EHCI_QTD_HALTED | EHCI_QTD_BUFFER_ERROR |
                   EHCI_QTD_BABBLE | EHCI_QTD_TRANSACTION_ERROR))) {
      uint32_t remaining = (token >> 16) & 0x7fffu;
      if (remaining <= controller->report_length) {
        uint32_t received = controller->report_length - remaining;
        if (usb_hid_keyboard_report(controller->report_buffer, received) ==
            0) {
          if (!controller->report_count)
            serial_print("USB EHCI: keyboard report received\n");
          int activity = controller->report_buffer[0] != 0;
          for (uint32_t byte = 2; byte < 8 && !activity; ++byte)
            activity = controller->report_buffer[byte] != 0;
          if (activity && !controller->input_activity_reported) {
            serial_print("USB EHCI: keyboard input event observed\n");
            controller->input_activity_reported = 1;
          }
          controller->report_count++;
        }
      }
    }

    memset(controller->report_buffer, 0, controller->report_length);
    ehci_qtd_t *qtd = controller->interrupt_qtd;
    memset(qtd, 0, sizeof(*qtd));
    qtd->next_qtd = EHCI_LINK_TERMINATE;
    qtd->alternate_next_qtd = EHCI_LINK_TERMINATE;
    qtd->token = EHCI_QTD_ACTIVE | EHCI_QTD_CERR_3 | EHCI_QTD_PID_IN |
                 EHCI_QTD_IOC | ((uint32_t)controller->report_length << 16);
    qtd->buffer[0] = (uint32_t)controller->report_buffer_phys;

    /* Memory barrier ensuring qTD contents are committed before the QH points to it */
    __atomic_thread_fence(__ATOMIC_RELEASE);

    /* Update the Queue Head Next qTD pointer to trigger the next transfer on the active schedule */
    ehci_queue_head_t *qh = controller->interrupt_qh;
    if (token & EHCI_QTD_HALTED) {
      qh->token &= ~EHCI_QTD_HALTED;
    }
    qh->next_qtd = (uint32_t)controller->interrupt_qtd_phys;
    __atomic_thread_fence(__ATOMIC_RELEASE);
  }

  ehci_poll_active = 0;
  if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
}

/* Control transfers can wait for hardware; drain connection work from a
 * process-context syscall, while the timer only detects and queues it. */
void usb_ehci_service(void) {
  usb_ehci_poll();
  for (int index = 0; index < ehci_controller_total; ++index) {
    ehci_controller_t *controller = &ehci_controllers[index];
    if (!controller->root_scan_pending || controller->keyboard_configured)
      continue;
    controller->root_scan_pending = 0;
    ehci_enumerate_root_ports(controller);
    if (controller->keyboard_configured) {
      ehci_status = "usb ehci keyboard ready";
      ehci_log("USB EHCI: boot keyboard ready after reconnect");
      break;
    }
  }
}

void usb_ehci_probe(void) {
  ehci_controller_total = 0;
  ehci_started_total = 0;
  ehci_status = "usb ehci: no PCI EHCI controller found";

  int found = pci_usb_device_count(EHCI_INTERFACE);
  if (!found) return;
  if (found > EHCI_MAX_CONTROLLERS) found = EHCI_MAX_CONTROLLERS;

  ehci_status = "usb ehci: initialization failed";
  for (int i = 0; i < found; i++) {
    pci_device_t *pci = pci_get_usb_by_index(EHCI_INTERFACE, i);
    if (!pci) continue;
    ehci_controller_t *controller = &ehci_controllers[ehci_controller_total++];
    *controller = (ehci_controller_t){.pci = pci};

    if (ehci_read_bar0(pci, &controller->bar) < 0 ||
        ehci_enable_mmio(pci) < 0 || ehci_map_registers(controller) < 0) {
      ehci_log("USB EHCI: BAR/MMIO setup failed");
      continue;
    }
    if (ehci_start_controller(controller) < 0) continue;
    if (ehci_start_async_schedule(controller) < 0) {
      ehci_log("USB EHCI: asynchronous DMA schedule did not start");
      continue;
    }
    /* Intel's PCH datasheet specifies CONFIGFLAG as the last controller
     * configuration action; its 0->1 transition also clears PORTSC.Owner
     * and routes ports to EHCI. Do this after RUN and the async schedule are
     * active, before reading root-port state or issuing USB transactions. */
    controller->operational[0x10] |= EHCI_CONFIGURED;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    /* Give port routing and connection detection time to settle on hardware
     * before the one-time root-port scan. */
    ehci_delay_ms(100);
    ehci_started_total++;
    ehci_log("USB EHCI: asynchronous DMA schedule active; enumeration pending");
    ehci_log_runtime_state(controller);
    ehci_enumerate_root_ports(controller);
    if (controller->keyboard_configured) {
      ehci_status = "usb ehci keyboard ready";
      ehci_log("USB EHCI: boot keyboard ready");
      break;
    }
  }

  if (!usb_ehci_keyboard_ready() && ehci_started_total)
    ehci_status = "usb ehci active; no boot keyboard found";
}

int usb_ehci_controller_ready(void) { return ehci_started_total > 0; }

int usb_ehci_keyboard_ready(void) {
  for (int i = 0; i < ehci_controller_total; ++i)
    if (ehci_controllers[i].keyboard_configured) return 1;
  return 0;
}

const char *usb_ehci_boot_status(void) { return ehci_status; }
