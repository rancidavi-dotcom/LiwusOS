#ifndef LIWUS_USB_EHCI_SCHEDULE_H
#define LIWUS_USB_EHCI_SCHEDULE_H

#include <stdint.h>
#include <stddef.h>

#define EHCI_QH_MULT_ONE (1u << 30)

/* EHCI Queue Head and Transfer Descriptors with 64-bit addressing extensions
 * (EHCI 1.0 Table 3-17 and Table 3-19). Even when operating in low 4GB memory,
 * 64-bit capable host controllers (HCCPARAMS bit 0) fetch extended buffer
 * fields. Having explicit extended_buffer words ensures that contiguous array
 * elements (like qtd[1]) are never misinterpreted as high address bits. */
typedef struct __attribute__((packed, aligned(32))) {
  uint32_t horizontal_link;
  uint32_t endpoint_characteristics;
  uint32_t endpoint_capabilities;
  uint32_t current_qtd;
  uint32_t next_qtd;
  uint32_t alternate_next_qtd;
  uint32_t token;
  uint32_t buffer[5];
  uint32_t extended_buffer[5];
} ehci_queue_head_t;

_Static_assert(sizeof(ehci_queue_head_t) == 96, "EHCI QH must be 96 bytes");

typedef struct __attribute__((packed, aligned(32))) {
  uint32_t next_qtd;
  uint32_t alternate_next_qtd;
  uint32_t token;
  uint32_t buffer[5];
  uint32_t extended_buffer[5];
} ehci_qtd_t;

_Static_assert(sizeof(ehci_qtd_t) == 64, "EHCI qTD must be 64 bytes");

/* Construct the periodic QH endpoint-capability word and frame-list stride.
 * Speed uses the EHCI EPS encoding: 0=full, 1=low, 2=high. */
static inline int ehci_periodic_schedule(uint8_t speed, uint8_t interval,
                                         uint8_t hub_address,
                                         uint8_t hub_port,
                                         uint32_t *capabilities_out,
                                         uint32_t *frame_stride_out) {
  if (!capabilities_out || !frame_stride_out || !interval || speed > 2)
    return -1;

  uint32_t capabilities = EHCI_QH_MULT_ONE;
  uint32_t frame_stride = 1;
  if (speed == 2) {
    /* High-speed interrupt bInterval is an exponent in the range 1..16. */
    if (interval > 16) return -1;
    if (interval == 1)
      capabilities |= 0xffu;
    else if (interval == 2)
      capabilities |= 0x55u;
    else if (interval == 3)
      capabilities |= 0x11u;
    else {
      frame_stride = 1u << (interval - 4);
      if (frame_stride > 1024) frame_stride = 1024;
      capabilities |= 1u;
    }
  } else {
    if (!hub_address || !hub_port) return -1;
    frame_stride = interval;
    if (frame_stride > 1024) frame_stride = 1024;
    /* Start-split in microframe 0; complete-splits in microframes 2-4. */
    capabilities |= 1u | (0x1cu << 8);
    capabilities |= (uint32_t)hub_address << 16;
    capabilities |= (uint32_t)hub_port << 23;
  }

  *capabilities_out = capabilities;
  *frame_stride_out = frame_stride;
  return 0;
}

#endif
