#include "usb_xhci.h"

#include "kernel_layout.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "vmm.h"
#include "usb_xhci_ring.h"

#define PCI_BAR0_OFFSET 0x10
#define PCI_COMMAND_OFFSET 0x04
#define PCI_COMMAND_MEMORY 0x0002
#define PCI_COMMAND_BUS_MASTER 0x0004
#define XHCI_USBCMD_RS (1u << 0)
#define XHCI_USBCMD_HCRST (1u << 1)
#define XHCI_USBSTS_HCH (1u << 0)
#define XHCI_USBSTS_HSE (1u << 2)
#define XHCI_USBSTS_CNR (1u << 11)
#define XHCI_USBSTS_HCE (1u << 12)
#define XHCI_HCCPARAMS1_AC64 (1u << 0)
#define XHCI_RUNTIME_INTR0 0x20
#define XHCI_MAX_SCRATCHPADS 1023u
#define XHCI_DMA_PAGE_LIMIT (4u + XHCI_MAX_SCRATCHPADS)
#define XHCI_COMMAND_COMPLETION_EVENT 33u
#define XHCI_COMMAND_NOOP 23u
#define XHCI_COMMAND_ENABLE_SLOT 9u
#define XHCI_COMPLETION_SUCCESS 1u
#define XHCI_EVENT_HANDLER_BUSY (1u << 3)
#define XHCI_EXT_CAP_LEGACY 1u
#define XHCI_EXT_CAP_BIOS_OWNED (1u << 16)
#define XHCI_EXT_CAP_OS_OWNED (1u << 24)
#define XHCI_CONTROLLER_WAIT_SPINS 5000000u
#define XHCI_PORTSC_CONNECTED (1u << 0)
#define XHCI_PORTSC_ENABLED (1u << 1)
#define XHCI_PORTSC_RESET (1u << 4)
#define XHCI_PORTSC_RESET_CHANGE (1u << 21)

typedef struct __attribute__((packed)) {
  uint64_t ring_segment_base;
  uint32_t ring_segment_size;
  uint32_t reserved;
} xhci_erst_entry_t;

static void *xhci_dma_pages[XHCI_DMA_PAGE_LIMIT];
static size_t xhci_dma_page_count;
static uint64_t xhci_scratchpad_array[XHCI_MAX_SCRATCHPADS]
    __attribute__((aligned(64)));
static usb_xhci_command_ring_t xhci_command_ring;
static usb_xhci_event_ring_t xhci_event_ring;
static uint64_t xhci_command_ring_phys;
static uint64_t xhci_event_ring_phys;
static uint8_t xhci_port_slot[256];
static uint8_t xhci_port_speed[256];
static uint16_t xhci_port_count;
static volatile uint32_t *xhci_operational_regs;
static volatile uint32_t *xhci_doorbell_regs;
static volatile uint32_t *xhci_runtime_regs;
static bool xhci_controller_started;

static int map_xhci_register_page(uint64_t physical_page);
static int map_xhci_register(uint64_t physical_address);

static void xhci_write64(volatile uint32_t *registers, uint32_t offset,
                         uint64_t value) {
  registers[offset / 4] = (uint32_t)value;
  registers[offset / 4 + 1] = (uint32_t)(value >> 32);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static uint64_t xhci_read64(volatile uint32_t *registers, uint32_t offset) {
  uint32_t low = registers[offset / 4];
  uint32_t high = registers[offset / 4 + 1];
  return low | ((uint64_t)high << 32);
}

static int xhci_wait_mask(volatile uint32_t *reg, uint32_t mask,
                          uint32_t expected) {
  for (uint32_t i = 0; i < XHCI_CONTROLLER_WAIT_SPINS; ++i) {
    if ((*reg & mask) == expected) return 0;
    __asm__ volatile("pause" ::: "memory");
  }
  return -1;
}

static void *xhci_alloc_dma_page(bool ac64) {
  if (xhci_dma_page_count >= XHCI_DMA_PAGE_LIMIT) return NULL;
  void *page = ac64 ? pmm_alloc_block()
                    : pmm_alloc_block_below(0x100000000ULL);
  if (!page) return NULL;
  uint64_t physical = virt_to_phys(page);
  if ((physical & (PAGE_SIZE - 1)) ||
      (!ac64 && physical >= 0x100000000ULL)) {
    pmm_free_block(page);
    return NULL;
  }
  memset(page, 0, PAGE_SIZE);
  xhci_dma_pages[xhci_dma_page_count++] = page;
  return page;
}

static void xhci_free_dma_pages(void) {
  while (xhci_dma_page_count)
    pmm_free_block(xhci_dma_pages[--xhci_dma_page_count]);
  memset(xhci_scratchpad_array, 0, sizeof(xhci_scratchpad_array));
  xhci_command_ring.segment = NULL;
  xhci_event_ring.segment = NULL;
}

static int xhci_legacy_handoff(uint64_t bar, uint32_t hccparams1) {
  uint32_t offset = ((hccparams1 >> 16) & 0xffffu) * 4u;
  for (uint32_t visited = 0; offset && visited < 64; ++visited) {
    if (offset < 0x20 || offset > 0x100000 ||
        map_xhci_register(bar + offset) < 0 ||
        map_xhci_register(bar + offset + sizeof(uint32_t)) < 0)
      return -1;
    volatile uint32_t *header =
        (volatile uint32_t *)phys_to_virt(bar + offset);
    uint32_t capability = *header;
    uint8_t cap_id = (uint8_t)capability;
    uint8_t next = (uint8_t)(capability >> 8);
    if (cap_id == XHCI_EXT_CAP_LEGACY) {
      volatile uint32_t *control = header + 1;
      uint32_t value = *control;
      if (value & XHCI_EXT_CAP_BIOS_OWNED) {
        *control = value | XHCI_EXT_CAP_OS_OWNED;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (xhci_wait_mask(control, XHCI_EXT_CAP_BIOS_OWNED, 0) < 0)
          return -1;
      }
      return 0;
    }
    if (!next) return 0;
    uint32_t next_offset = offset + (uint32_t)next * 4;
    if (next_offset <= offset) return -1;
    offset = next_offset;
  }
  return offset ? -1 : 0;
}

static int xhci_stop_controller(void) {
  uint32_t command = xhci_operational_regs[0];
  if (command & XHCI_USBCMD_RS) {
    xhci_operational_regs[0] = command & ~XHCI_USBCMD_RS;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
  }
  return xhci_wait_mask(&xhci_operational_regs[1], XHCI_USBSTS_HCH,
                        XHCI_USBSTS_HCH);
}

static int xhci_controller_reset(void) {
  if (xhci_stop_controller() < 0) return -1;
  xhci_operational_regs[0] |= XHCI_USBCMD_HCRST;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (xhci_wait_mask(&xhci_operational_regs[0], XHCI_USBCMD_HCRST, 0) < 0)
    return -1;
  return xhci_wait_mask(&xhci_operational_regs[1], XHCI_USBSTS_CNR, 0);
}

static void xhci_update_erdp(void) {
  uint64_t dequeue = xhci_event_ring_phys +
                     (uint64_t)xhci_event_ring.dequeue_index *
                         sizeof(usb_xhci_trb_t);
  xhci_write64(xhci_runtime_regs, XHCI_RUNTIME_INTR0 + 0x18,
               dequeue | XHCI_EVENT_HANDLER_BUSY);
}

static int xhci_wait_command(uint64_t command_phys,
                             usb_xhci_trb_t *completion) {
  for (uint32_t spin = 0; spin < XHCI_CONTROLLER_WAIT_SPINS; ++spin) {
    usb_xhci_trb_t event;
    if (usb_xhci_event_ring_pop(&xhci_event_ring, &event)) {
      xhci_update_erdp();
      uint32_t type = (event.control & USB_XHCI_TRB_TYPE_MASK) >>
                      USB_XHCI_TRB_TYPE_SHIFT;
      if (type == XHCI_COMMAND_COMPLETION_EVENT &&
          event.parameter == command_phys) {
        usb_xhci_command_ring_complete(&xhci_command_ring);
        *completion = event;
        return 0;
      }
    }
    if (xhci_operational_regs[1] & (XHCI_USBSTS_HSE | XHCI_USBSTS_HCE))
      return -1;
    __asm__ volatile("pause" ::: "memory");
  }
  return -1;
}

static int xhci_submit_noop(void) {
  usb_xhci_trb_t command = {
      .parameter = 0,
      .status = 0,
      .control = XHCI_COMMAND_NOOP << USB_XHCI_TRB_TYPE_SHIFT};
  uint16_t slot;
  if (usb_xhci_command_ring_push(&xhci_command_ring, &command, &slot) < 0)
    return -1;
  uint64_t command_phys = xhci_command_ring_phys +
                          (uint64_t)slot * sizeof(usb_xhci_trb_t);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  xhci_doorbell_regs[0] = 0;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  usb_xhci_trb_t completion;
  if (xhci_wait_command(command_phys, &completion) < 0) {
    serial_print("USB: command timeout status=");
    serial_print_hex(xhci_operational_regs[1]);
    serial_print(" crcr=");
    serial_print_hex(xhci_read64(xhci_operational_regs, 0x18));
    serial_print(" erstsz=");
    serial_print_hex(xhci_runtime_regs[XHCI_RUNTIME_INTR0 / 4 + 2]);
    serial_print(" erdp=");
    serial_print_hex(xhci_read64(xhci_runtime_regs,
                                 XHCI_RUNTIME_INTR0 + 0x18));
    serial_print(" event0=");
    serial_print_hex(xhci_event_ring.segment->trbs[0].control);
    serial_print("\n");
    return -1;
  }
  if ((completion.status >> 24) != XHCI_COMPLETION_SUCCESS) {
    serial_print("USB: command completion code=");
    serial_print_hex(completion.status >> 24);
    serial_print("\n");
    return -1;
  }
  return 0;
}

static int xhci_submit_command(usb_xhci_trb_t *command,
                               usb_xhci_trb_t *completion) {
  uint16_t slot;
  if (!command || !completion ||
      usb_xhci_command_ring_push(&xhci_command_ring, command, &slot) < 0)
    return -1;
  uint64_t command_phys = xhci_command_ring_phys +
                          (uint64_t)slot * sizeof(usb_xhci_trb_t);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  xhci_doorbell_regs[0] = 0;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (xhci_wait_command(command_phys, completion) < 0) return -1;
  return (completion->status >> 24) == XHCI_COMPLETION_SUCCESS ? 0 : -1;
}

static int xhci_reset_root_port(uint8_t port_id) {
  uint64_t portsc_phys = (uint64_t)(uintptr_t)xhci_operational_regs +
                         0x400 + (uint64_t)(port_id - 1) * 0x10;
  volatile uint32_t *portsc = (volatile uint32_t *)portsc_phys;
  uint32_t status = *portsc;
  if (!(status & XHCI_PORTSC_CONNECTED)) return -1;
  if (!(status & XHCI_PORTSC_ENABLED)) {
    /* PORTSC change bits are W1C; write only PR so unrelated changes remain. */
    *portsc = XHCI_PORTSC_RESET;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (uint32_t i = 0; i < XHCI_CONTROLLER_WAIT_SPINS; ++i) {
      status = *portsc;
      if ((status & XHCI_PORTSC_RESET_CHANGE) &&
          !(status & XHCI_PORTSC_RESET)) {
        *portsc = XHCI_PORTSC_RESET_CHANGE;
        return (status & XHCI_PORTSC_ENABLED) ? 0 : -1;
      }
      __asm__ volatile("pause" ::: "memory");
    }
    return -1;
  }
  return 0;
}

static uint8_t xhci_enable_slot(void) {
  usb_xhci_trb_t command = {
      .parameter = 0,
      .status = 0,
      .control = XHCI_COMMAND_ENABLE_SLOT << USB_XHCI_TRB_TYPE_SHIFT};
  usb_xhci_trb_t completion;
  if (xhci_submit_command(&command, &completion) < 0) return 0;
  return (uint8_t)(completion.control >> 24);
}

static uint32_t xhci_enumerate_connected_slots(uint16_t ports) {
  uint32_t enabled = 0;
  if (ports > sizeof(xhci_port_slot)) ports = sizeof(xhci_port_slot);
  xhci_port_count = ports;
  memset(xhci_port_slot, 0, sizeof(xhci_port_slot));
  memset(xhci_port_speed, 0, sizeof(xhci_port_speed));
  for (uint16_t index = 0; index < ports; ++index) {
    uint8_t port_id = (uint8_t)(index + 1);
    volatile uint32_t *portsc =
        (volatile uint32_t *)((uintptr_t)xhci_operational_regs + 0x400 +
                              (uint64_t)index * 0x10);
    uint32_t status = *portsc;
    if (!(status & XHCI_PORTSC_CONNECTED)) continue;
    if (xhci_reset_root_port(port_id) < 0) {
      serial_print("USB: xHCI root port reset failed: ");
      serial_print_hex(port_id);
      serial_print("\n");
      continue;
    }
    status = *portsc;
    uint8_t slot = xhci_enable_slot();
    if (!slot) {
      serial_print("USB: xHCI Enable Slot failed for port ");
      serial_print_hex(port_id);
      serial_print("\n");
      continue;
    }
    xhci_port_slot[index] = slot;
    xhci_port_speed[index] = (uint8_t)((status >> 10) & 0x0f);
    enabled++;
  }
  return enabled;
}

static int map_xhci_register_page(uint64_t physical_page) {
  if ((physical_page & (PAGE_SIZE - 1)) ||
      physical_page >= DIRECT_MAP_SIZE ||
      DIRECT_MAP_SIZE - physical_page < PAGE_SIZE)
    return -1;
  uint64_t virtual_page = (uint64_t)phys_to_virt(physical_page);
  return vmm_map_page_in(kernel_directory, (void *)physical_page,
                         (void *)virtual_page,
                         PTE_P | PTE_W | PTE_PCD | PTE_NX);
}

static int map_xhci_register(uint64_t physical_address) {
  uint64_t page = physical_address & ~((uint64_t)PAGE_SIZE - 1);
  return map_xhci_register_page(page);
}

static uint64_t xhci_bar_base(const pci_device_t *device) {
  uint32_t low = pci_read_config(device->bus, device->device,
                                 device->function, PCI_BAR0_OFFSET);
  if (low == 0 || low == 0xFFFFFFFF || (low & 1)) return 0;

  uint32_t bar_type = (low >> 1) & 0x3;
  uint64_t base = low & ~0xFULL;
  if (bar_type == 0x2) {
    uint32_t high = pci_read_config(device->bus, device->device,
                                    device->function, PCI_BAR0_OFFSET + 4);
    base |= (uint64_t)high << 32;
  } else if (bar_type != 0) {
    return 0;
  }
  return base;
}

static int xhci_enable_bus_master(const pci_device_t *device) {
  uint32_t command_status = pci_read_config(device->bus, device->device,
                                            device->function,
                                            PCI_COMMAND_OFFSET);
  uint16_t command = (uint16_t)command_status | PCI_COMMAND_MEMORY |
                     PCI_COMMAND_BUS_MASTER;
  pci_write_config(device->bus, device->device, device->function,
                   PCI_COMMAND_OFFSET, (uint32_t)command);
  uint32_t verify = pci_read_config(device->bus, device->device,
                                    device->function, PCI_COMMAND_OFFSET);
  return ((uint16_t)verify &
          (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) ==
                 (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)
             ? 0
             : -1;
}

static void xhci_disable_bus_master(const pci_device_t *device) {
  uint32_t command_status = pci_read_config(device->bus, device->device,
                                            device->function,
                                            PCI_COMMAND_OFFSET);
  uint16_t command = (uint16_t)command_status &
                     (uint16_t)~PCI_COMMAND_BUS_MASTER;
  pci_write_config(device->bus, device->device, device->function,
                   PCI_COMMAND_OFFSET, (uint32_t)command);
}

static int xhci_initialize_controller(const pci_device_t *device, uint64_t bar,
                                      uint8_t cap_length,
                                      uint32_t hcsparams1,
                                      uint32_t hcsparams2,
                                      uint32_t hccparams1,
                                      uint32_t doorbell_offset,
                                      uint32_t runtime_offset) {
  uint32_t max_slots = hcsparams1 & 0xffu;
  uint32_t max_interrupters = (hcsparams1 >> 8) & 0x7ffu;
  uint32_t scratchpads = ((hcsparams2 >> 21) & 0x1fu) << 5 |
                         ((hcsparams2 >> 27) & 0x1fu);
  bool ac64 = (hccparams1 & XHCI_HCCPARAMS1_AC64) != 0;

  doorbell_offset &= ~0x3u;
  runtime_offset &= ~0x1fu;
  if (!max_slots || !max_interrupters || doorbell_offset < cap_length ||
      runtime_offset < cap_length || doorbell_offset > 0x100000 ||
      runtime_offset > 0x100000 ||
      scratchpads > XHCI_MAX_SCRATCHPADS ||
      map_xhci_register(bar + cap_length) < 0 ||
      map_xhci_register(bar + doorbell_offset) < 0 ||
      map_xhci_register(bar + runtime_offset + XHCI_RUNTIME_INTR0 + 0x3c) < 0)
    return -1;

  xhci_operational_regs =
      (volatile uint32_t *)phys_to_virt(bar + cap_length);
  xhci_doorbell_regs =
      (volatile uint32_t *)phys_to_virt(bar + doorbell_offset);
  xhci_runtime_regs = (volatile uint32_t *)phys_to_virt(bar + runtime_offset);
  if (xhci_legacy_handoff(bar, hccparams1) < 0) {
    serial_print("USB: xHCI BIOS ownership handoff failed\n");
    return -1;
  }
  if (xhci_controller_reset() < 0) {
    serial_print("USB: xHCI halt/reset timed out\n");
    return -1;
  }

  usb_xhci_trb_segment_t *command_segment =
      (usb_xhci_trb_segment_t *)xhci_alloc_dma_page(ac64);
  usb_xhci_trb_segment_t *event_segment =
      (usb_xhci_trb_segment_t *)xhci_alloc_dma_page(ac64);
  uint64_t *device_context_array = (uint64_t *)xhci_alloc_dma_page(ac64);
  xhci_erst_entry_t *erst = (xhci_erst_entry_t *)xhci_alloc_dma_page(ac64);
  if (!command_segment || !event_segment || !device_context_array || !erst) {
    serial_print("USB: xHCI DMA ring allocation failed\n");
    xhci_free_dma_pages();
    return -1;
  }

  uint64_t command_phys = virt_to_phys(command_segment);
  xhci_event_ring_phys = virt_to_phys(event_segment);
  uint64_t dcbaa_phys = virt_to_phys(device_context_array);
  uint64_t erst_phys = virt_to_phys(erst);
  uint64_t scratchpad_array_phys = virt_to_phys(xhci_scratchpad_array);
  if ((!ac64 && (command_phys >= 0x100000000ULL ||
                 xhci_event_ring_phys >= 0x100000000ULL ||
                 dcbaa_phys >= 0x100000000ULL ||
                 erst_phys >= 0x100000000ULL ||
                 scratchpad_array_phys >= 0x100000000ULL)) ||
      usb_xhci_command_ring_init(&xhci_command_ring, command_segment,
                                 command_phys) < 0 ||
      usb_xhci_event_ring_init(&xhci_event_ring, event_segment) < 0) {
    serial_print("USB: xHCI DMA address/alignment validation failed\n");
    xhci_free_dma_pages();
    return -1;
  }
  xhci_command_ring_phys = command_phys;
  erst[0].ring_segment_base = xhci_event_ring_phys;
  erst[0].ring_segment_size = USB_XHCI_RING_TRBS;

  for (uint32_t i = 0; i < scratchpads; ++i) {
    void *scratchpad = xhci_alloc_dma_page(ac64);
    if (!scratchpad) {
      serial_print("USB: xHCI scratchpad allocation failed\n");
      xhci_free_dma_pages();
      return -1;
    }
    uint64_t physical = virt_to_phys(scratchpad);
    if (!ac64 && physical >= 0x100000000ULL) {
      serial_print("USB: xHCI scratchpad exceeds 32-bit DMA range\n");
      xhci_free_dma_pages();
      return -1;
    }
    xhci_scratchpad_array[i] = physical;
  }
  if (scratchpads)
    device_context_array[0] = scratchpad_array_phys;

  /* All referenced memory is now allocated, cleared, aligned, and range
   * checked. The controller is halted; enable DMA before programming ERSTBA,
   * whose implementation may immediately fetch the table. */
  if (xhci_enable_bus_master(device) < 0) {
    serial_print("USB: could not enable xHCI PCI bus mastering\n");
    xhci_free_dma_pages();
    return -1;
  }

  /* The controller sees only the physical aliases of these DMA structures. */
  xhci_write64(xhci_operational_regs, 0x30, dcbaa_phys);
  xhci_write64(xhci_operational_regs, 0x18, command_phys | 1u);
  xhci_operational_regs[0x38 / 4] = max_slots;

  volatile uint32_t *interrupter =
      xhci_runtime_regs + XHCI_RUNTIME_INTR0 / 4;
  interrupter[0] = 1u; /* Clear IP; keep IE disabled for polling. */
  interrupter[2] = 1u; /* One ERST entry. */
  xhci_write64(interrupter, 0x10, erst_phys);
  xhci_write64(interrupter, 0x18, xhci_event_ring_phys);

  /* Clear any stale W1C status before run; interrupts remain disabled. */
  xhci_operational_regs[1] = (1u << 2) | (1u << 3) | (1u << 4);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);

  uint32_t command = xhci_operational_regs[0];
  command &= ~((1u << 2) | (1u << 3) | XHCI_USBCMD_HCRST);
  xhci_operational_regs[0] = command | XHCI_USBCMD_RS;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (xhci_wait_mask(&xhci_operational_regs[1], XHCI_USBSTS_HCH, 0) < 0 ||
      xhci_wait_mask(&xhci_operational_regs[1], XHCI_USBSTS_CNR, 0) < 0) {
    serial_print("USB: xHCI start timed out; stopping controller\n");
    if (xhci_stop_controller() == 0) {
      xhci_disable_bus_master(device);
      xhci_free_dma_pages();
    } else {
      xhci_controller_started = true; /* Keep DMA alive if halt is uncertain. */
      serial_print("USB: controller still owns DMA; retaining buffers\n");
    }
    return -1;
  }
  xhci_controller_started = true;

  if (xhci_submit_noop() < 0) {
    serial_print("USB: xHCI command-ring NOOP failed\n");
    if (xhci_stop_controller() == 0) {
      xhci_controller_started = false;
      xhci_disable_bus_master(device);
      xhci_free_dma_pages();
    } else {
      serial_print("USB: controller still owns DMA; retaining buffers\n");
    }
    return -1;
  }

  uint32_t enabled_slots = xhci_enumerate_connected_slots(
      (uint16_t)(hcsparams1 >> 24));
  serial_print("\nUSB: xHCI controller running; command-ring NOOP PASS; slots=");
  serial_print_hex(max_slots);
  serial_print(" enabled-slots=");
  serial_print_hex(enabled_slots);
  serial_print(" scratchpads=");
  serial_print_hex(scratchpads);
  serial_print(" polling mode; HID enumeration pending\n");
  return 0;
}

void usb_xhci_probe(void) {
  pci_device_t *device = pci_get_usb(0x30);
  if (!device) {
    serial_print("USB: no PCI xHCI controller found; USB support inactive\n");
    return;
  }

  uint64_t bar = xhci_bar_base(device);
  if (!bar || bar >= DIRECT_MAP_SIZE) {
    serial_print("USB: xHCI has no usable memory BAR0\n");
    return;
  }

  /* Make MMIO visible but do not add the bus-master bit here. Preserve the
   * firmware's current command state until xHCI ownership is handed off. */
  uint32_t command_status = pci_read_config(device->bus, device->device,
                                            device->function,
                                            PCI_COMMAND_OFFSET);
  uint16_t command = (uint16_t)command_status | PCI_COMMAND_MEMORY;
  pci_write_config(device->bus, device->device, device->function,
                   PCI_COMMAND_OFFSET,
                   (uint32_t)command);

  uint64_t page = bar & ~((uint64_t)PAGE_SIZE - 1);
  if (map_xhci_register_page(page) < 0) {
    serial_print("USB: could not map xHCI capability registers\n");
    return;
  }

  volatile uint8_t *registers = (volatile uint8_t *)phys_to_virt(bar);
  /* Read the capability header as a 32-bit MMIO access. Some controllers
   * reject sub-dword reads even though the fields themselves are byte-sized. */
  uint32_t capability_header = *(volatile uint32_t *)registers;
  uint8_t cap_length = (uint8_t)capability_header;
  uint16_t version = (uint16_t)(capability_header >> 16);
  uint32_t structural = *(volatile uint32_t *)(registers + 4);
  uint32_t hcsparams2 = *(volatile uint32_t *)(registers + 8);
  uint32_t hccparams1 = *(volatile uint32_t *)(registers + 0x10);
  uint32_t doorbell_offset = *(volatile uint32_t *)(registers + 0x14);
  uint32_t runtime_offset = *(volatile uint32_t *)(registers + 0x18);
  uint32_t max_slots = structural & 0xffu;
  uint8_t ports = (uint8_t)(structural >> 24);

  if (cap_length < 0x20 || cap_length > 0x80 || version == 0 ||
      version == 0xFFFF || ports == 0 || max_slots == 0) {
    serial_print("USB: invalid xHCI capabilities caplen=");
    serial_print_hex(cap_length);
    serial_print(" version=");
    serial_print_hex(version);
    serial_print(" structural=");
    serial_print_hex(structural);
    serial_print("\n");
    return;
  }

  serial_print("USB: xHCI capability probe OK, PCI ");
  serial_print_hex(((uint64_t)device->bus << 16) |
                   ((uint64_t)device->device << 8) | device->function);
  serial_print(" BAR0=");
  serial_print_hex(bar);
  serial_print(" version=");
  serial_print_hex(version);
  serial_print(" ports=");
  serial_print_hex(ports);

  /* Root-port connection status is safe to inspect before claiming the host
   * controller. This is discovery only: do not write PORTSC or enable DMA. */
  uint64_t operational = bar + cap_length;
  uint32_t connected = 0;
  for (uint16_t port = 0; port < ports; ++port) {
    uint64_t portsc_phys = operational + 0x400 + (uint64_t)port * 0x10;
    if (map_xhci_register(portsc_phys) < 0) continue;
    volatile uint32_t *portsc =
        (volatile uint32_t *)phys_to_virt(portsc_phys);
    if (*portsc & 1u) {
      connected++;
      serial_print("\nUSB: xHCI root port connected: ");
      serial_print_hex((uint64_t)port + 1);
      serial_print(" speed=");
      serial_print_hex((*portsc >> 10) & 0x0f);
    }
  }
  serial_print(" connected=");
  serial_print_hex(connected);
  if (xhci_initialize_controller(device, bar, cap_length, structural,
                                  hcsparams2, hccparams1, doorbell_offset,
                                  runtime_offset) < 0) {
    serial_print("USB: xHCI controller initialization failed; PS/2 remains active\n");
    return;
  }
}
