#include "usb_ehci_schedule.h"

#include <stdio.h>

static int fail(const char *message) {
  fprintf(stderr, "USB EHCI schedule FAIL: %s\n", message);
  return 1;
}

int main(void) {
  uint32_t capabilities = 0;
  uint32_t stride = 0;

  if (ehci_periodic_schedule(0, 10, 5, 2,
                             &capabilities, &stride) != 0)
    return fail("full-speed split endpoint rejected");
  if ((capabilities & 0xffu) != 0x01u)
    return fail("full-speed start-split mask must schedule microframe 0");
  if (((capabilities >> 8) & 0xffu) != 0x1cu)
    return fail("complete-split mask must schedule microframes 2-4");
  if (((capabilities >> 16) & 0x7fu) != 5u ||
      ((capabilities >> 23) & 0x7fu) != 2u)
    return fail("transaction-translator hub/port fields are incorrect");
  if (stride != 10u) return fail("full-speed interval stride is incorrect");

  if (ehci_periodic_schedule(1, 1, 7, 3,
                             &capabilities, &stride) != 0 ||
      (capabilities & 0xffu) != 0x01u ||
      ((capabilities >> 8) & 0xffu) != 0x1cu)
    return fail("low-speed split schedule masks are incorrect");

  if (ehci_periodic_schedule(2, 1, 0, 0,
                             &capabilities, &stride) != 0 ||
      (capabilities & 0xffu) != 0xffu || stride != 1u)
    return fail("high-speed one-microframe interval is incorrect");
  if (ehci_periodic_schedule(2, 2, 0, 0,
                             &capabilities, &stride) != 0 ||
      (capabilities & 0xffu) != 0x55u)
    return fail("high-speed two-microframe mask is incorrect");
  if (ehci_periodic_schedule(2, 16, 0, 0,
                             &capabilities, &stride) != 0 ||
      (capabilities & 0xffu) != 0x01u || stride != 1024u)
    return fail("high-speed long interval was not bounded to frame-list size");

  if (ehci_periodic_schedule(2, 0, 0, 0,
                             &capabilities, &stride) == 0)
    return fail("zero bInterval must be rejected");
  if (ehci_periodic_schedule(2, 17, 0, 0,
                             &capabilities, &stride) == 0)
    return fail("invalid high-speed bInterval must be rejected");
  if (ehci_periodic_schedule(0, 1, 0, 1,
                             &capabilities, &stride) == 0)
    return fail("full-speed endpoint without a transaction translator accepted");
  if (ehci_periodic_schedule(3, 1, 1, 1,
                             &capabilities, &stride) == 0)
    return fail("reserved endpoint speed accepted");

  /* Verify 64-bit capable QH and qTD memory layout and alignments per EHCI 1.0 */
  if (sizeof(ehci_queue_head_t) != 96)
    return fail("ehci_queue_head_t size must be 96 bytes");
  if (sizeof(ehci_qtd_t) != 64)
    return fail("ehci_qtd_t size must be 64 bytes");
  if (offsetof(ehci_queue_head_t, buffer) != 28)
    return fail("QH buffer[0] must be at byte offset 28 (dword 7)");
  if (offsetof(ehci_queue_head_t, extended_buffer) != 48)
    return fail("QH extended_buffer[0] must be at byte offset 48 (dword 12)");
  if (offsetof(ehci_qtd_t, buffer) != 12)
    return fail("qTD buffer[0] must be at byte offset 12 (dword 3)");
  if (offsetof(ehci_qtd_t, extended_buffer) != 32)
    return fail("qTD extended_buffer[0] must be at byte offset 32 (dword 8)");

  puts("USB EHCI schedule PASS: split masks, TT fields, intervals and bounds");
  return 0;
}
