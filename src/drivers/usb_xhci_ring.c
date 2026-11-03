#include "usb_xhci_ring.h"

#include <string.h>

_Static_assert(sizeof(usb_xhci_trb_t) == 16, "xHCI TRBs are 16 bytes");
_Static_assert(sizeof(usb_xhci_trb_segment_t) == 4096,
               "a 256-entry xHCI ring segment occupies one page");

static uint32_t trb_type(uint32_t control, uint8_t type) {
  return (control & ~USB_XHCI_TRB_TYPE_MASK) |
         ((uint32_t)type << USB_XHCI_TRB_TYPE_SHIFT);
}

int usb_xhci_command_ring_init(usb_xhci_command_ring_t *ring,
                               usb_xhci_trb_segment_t *segment,
                               uint64_t ring_physical_address) {
  if (!ring || !segment || ((uintptr_t)segment & 0x3f) ||
      (ring_physical_address & 0x3f))
    return -1;
  memset(segment, 0, sizeof(*segment));
  memset(ring, 0, sizeof(*ring));
  ring->segment = segment;
  ring->physical_address = ring_physical_address;
  ring->producer_cycle = 1;
  /* The final TRB loops back to slot zero and toggles the producer cycle. */
  segment->trbs[USB_XHCI_RING_TRBS - 1].parameter = ring_physical_address;
  segment->trbs[USB_XHCI_RING_TRBS - 1].control =
      trb_type(USB_XHCI_TRB_CYCLE | USB_XHCI_TRB_TOGGLE_CYCLE,
               USB_XHCI_TRB_TYPE_LINK);
  return 0;
}

int usb_xhci_command_ring_push(usb_xhci_command_ring_t *ring,
                               const usb_xhci_trb_t *trb,
                               uint16_t *slot_out) {
  if (!ring || !trb || ring->enqueue_index >= USB_XHCI_RING_TRBS - 1 ||
      ring->in_flight >= USB_XHCI_RING_TRBS - 1)
    return -1;

  uint16_t slot = ring->enqueue_index;
  usb_xhci_trb_t value = *trb;
  value.control = (value.control & ~USB_XHCI_TRB_CYCLE) |
                  (ring->producer_cycle ? USB_XHCI_TRB_CYCLE : 0);
  ring->segment->trbs[slot] = value;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  ring->enqueue_index++;
  ring->in_flight++;
  if (ring->enqueue_index == USB_XHCI_RING_TRBS - 1) {
    usb_xhci_trb_t *link =
        &ring->segment->trbs[USB_XHCI_RING_TRBS - 1];
    link->control = trb_type(USB_XHCI_TRB_TOGGLE_CYCLE |
                                 (ring->producer_cycle ? USB_XHCI_TRB_CYCLE : 0),
                             USB_XHCI_TRB_TYPE_LINK);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ring->enqueue_index = 0;
    ring->producer_cycle ^= 1u;
  }
  if (slot_out) *slot_out = slot;
  return 0;
}

void usb_xhci_command_ring_complete(usb_xhci_command_ring_t *ring) {
  if (ring && ring->in_flight) ring->in_flight--;
}

int usb_xhci_event_ring_init(usb_xhci_event_ring_t *ring,
                             usb_xhci_trb_segment_t *segment) {
  if (!ring || !segment || ((uintptr_t)segment & 0x3f)) return -1;
  memset(segment, 0, sizeof(*segment));
  memset(ring, 0, sizeof(*ring));
  ring->segment = segment;
  ring->consumer_cycle = 1;
  return 0;
}

bool usb_xhci_event_ring_pop(usb_xhci_event_ring_t *ring,
                             usb_xhci_trb_t *event_out) {
  if (!ring || !ring->segment || !event_out ||
      ring->dequeue_index >= USB_XHCI_RING_TRBS)
    return false;
  usb_xhci_trb_t *event = &ring->segment->trbs[ring->dequeue_index];
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  if ((event->control & USB_XHCI_TRB_CYCLE) != ring->consumer_cycle)
    return false;
  *event_out = *event;
  ring->dequeue_index++;
  if (ring->dequeue_index == USB_XHCI_RING_TRBS) {
    ring->dequeue_index = 0;
    ring->consumer_cycle ^= 1u;
  }
  return true;
}
