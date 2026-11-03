#include "usb_xhci_ring.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t trb_type(uint32_t control) {
  return (control & USB_XHCI_TRB_TYPE_MASK) >> USB_XHCI_TRB_TYPE_SHIFT;
}

int main(void) {
  usb_xhci_command_ring_t commands;
  _Alignas(64) usb_xhci_trb_segment_t command_segment;
  memset(&commands, 0xA5, sizeof(commands));
  const uint64_t command_ring_phys = 0x12340000;
  assert(usb_xhci_command_ring_init(&commands, &command_segment,
                                    command_ring_phys) == 0);
  assert(((uintptr_t)commands.segment & 63u) == 0);
  assert(commands.producer_cycle == 1);
  assert(trb_type(commands.segment->trbs[USB_XHCI_RING_TRBS - 1].control) ==
         USB_XHCI_TRB_TYPE_LINK);
  assert(commands.segment->trbs[USB_XHCI_RING_TRBS - 1].parameter ==
         command_ring_phys);
  assert(commands.segment->trbs[USB_XHCI_RING_TRBS - 1].control &
         USB_XHCI_TRB_TOGGLE_CYCLE);
  assert(usb_xhci_command_ring_init(&commands, &command_segment,
                                    command_ring_phys + 1) < 0);

  usb_xhci_trb_t command = {.parameter = 0x12345678, .status = 0x55,
                             .control = 0xCAFE0000};
  uint16_t slot = UINT16_MAX;
  for (uint16_t i = 0; i < USB_XHCI_RING_TRBS - 1; ++i) {
    assert(usb_xhci_command_ring_push(&commands, &command, &slot) == 0);
    assert(slot == i);
    assert(commands.segment->trbs[slot].parameter == command.parameter);
    assert(commands.segment->trbs[slot].control & USB_XHCI_TRB_CYCLE);
  }
  assert(commands.enqueue_index == 0 && commands.producer_cycle == 0);
  assert(commands.in_flight == USB_XHCI_RING_TRBS - 1);
  /* The Link TRB is consumed with the old phase, then TC changes the phase
   * for the next pass through slot zero. */
  assert(commands.segment->trbs[USB_XHCI_RING_TRBS - 1].control &
         USB_XHCI_TRB_CYCLE);
  assert(usb_xhci_command_ring_push(&commands, &command, NULL) < 0);
  usb_xhci_command_ring_complete(&commands);
  assert(usb_xhci_command_ring_push(&commands, &command, &slot) == 0);
  assert(slot == 0 && !(commands.segment->trbs[slot].control & USB_XHCI_TRB_CYCLE));
  for (uint16_t i = 1; i < USB_XHCI_RING_TRBS - 1; ++i) {
    usb_xhci_command_ring_complete(&commands);
    assert(usb_xhci_command_ring_push(&commands, &command, &slot) == 0);
    assert(slot == i && !(commands.segment->trbs[slot].control & USB_XHCI_TRB_CYCLE));
  }
  assert(commands.enqueue_index == 0 && commands.producer_cycle == 1);
  assert(!(commands.segment->trbs[USB_XHCI_RING_TRBS - 1].control &
           USB_XHCI_TRB_CYCLE));

  usb_xhci_event_ring_t events;
  _Alignas(64) usb_xhci_trb_segment_t event_segment;
  assert(usb_xhci_event_ring_init(&events, &event_segment) == 0);
  usb_xhci_trb_t observed;
  assert(!usb_xhci_event_ring_pop(&events, &observed));
  events.segment->trbs[0] = (usb_xhci_trb_t){.parameter = 0xABC, .status = 7,
                                   .control = 0x1200 | USB_XHCI_TRB_CYCLE};
  assert(usb_xhci_event_ring_pop(&events, &observed));
  assert(observed.parameter == 0xABC && observed.status == 7);
  assert(events.dequeue_index == 1 && events.consumer_cycle == 1);
  for (uint16_t i = 1; i < USB_XHCI_RING_TRBS; ++i)
    events.segment->trbs[i].control = USB_XHCI_TRB_CYCLE;
  for (uint16_t i = 1; i < USB_XHCI_RING_TRBS; ++i)
    assert(usb_xhci_event_ring_pop(&events, &observed));
  assert(events.dequeue_index == 0 && events.consumer_cycle == 0);
  assert(!usb_xhci_event_ring_pop(&events, &observed));
  events.segment->trbs[0].control = 0;
  events.segment->trbs[0].parameter = 0xDEF;
  assert(usb_xhci_event_ring_pop(&events, &observed));
  assert(observed.parameter == 0xDEF);

  puts("USB xHCI ring PASS: TRB layout, fullness, link wrap and cycle phase");
  return 0;
}
