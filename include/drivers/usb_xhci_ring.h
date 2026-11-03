#ifndef LIWUS_USB_XHCI_RING_H
#define LIWUS_USB_XHCI_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_XHCI_RING_TRBS 256u
#define USB_XHCI_TRB_TYPE_SHIFT 10u
#define USB_XHCI_TRB_TYPE_MASK (0x3fu << USB_XHCI_TRB_TYPE_SHIFT)
#define USB_XHCI_TRB_CYCLE (1u << 0)
#define USB_XHCI_TRB_TOGGLE_CYCLE (1u << 1)
#define USB_XHCI_TRB_TYPE_LINK 6u

typedef struct __attribute__((aligned(16))) {
  uint64_t parameter;
  uint32_t status;
  uint32_t control;
} usb_xhci_trb_t;

typedef struct __attribute__((aligned(64))) {
  usb_xhci_trb_t trbs[USB_XHCI_RING_TRBS];
} usb_xhci_trb_segment_t;

/* Command rings reserve their last entry for the link TRB. Event rings have
 * no link TRB and are consumed according to the xHCI cycle bit. The backing
 * arrays must be physically contiguous, 64-byte aligned DMA memory before
 * their physical address is programmed into controller registers. */
typedef struct __attribute__((aligned(64))) {
  usb_xhci_trb_segment_t *segment;
  uint64_t physical_address;
  uint16_t enqueue_index;
  uint16_t in_flight;
  uint8_t producer_cycle;
} usb_xhci_command_ring_t;

typedef struct __attribute__((aligned(64))) {
  usb_xhci_trb_segment_t *segment;
  uint16_t dequeue_index;
  uint8_t consumer_cycle;
} usb_xhci_event_ring_t;

int usb_xhci_command_ring_init(usb_xhci_command_ring_t *ring,
                               usb_xhci_trb_segment_t *segment,
                               uint64_t ring_physical_address);
int usb_xhci_command_ring_push(usb_xhci_command_ring_t *ring,
                               const usb_xhci_trb_t *trb,
                               uint16_t *slot_out);
void usb_xhci_command_ring_complete(usb_xhci_command_ring_t *ring);
int usb_xhci_event_ring_init(usb_xhci_event_ring_t *ring,
                             usb_xhci_trb_segment_t *segment);
bool usb_xhci_event_ring_pop(usb_xhci_event_ring_t *ring,
                             usb_xhci_trb_t *event_out);

#endif
