#ifdef KERNEL_TEST
#include "framework.h"
#include "idt.h"
#include "kstack.h"
#include "task.h"
#include "vmm.h"

volatile uint64_t test_kstack_guard_fault_addr;
extern void test_kstack_guard_write(void *address);
extern void test_kstack_guard_fault_handler(void);

static void idt_set_test_handler(idt_entry_t *entry, uint64_t handler) {
  entry->base_low = (uint16_t)handler;
  entry->base_mid = (uint16_t)(handler >> 16);
  entry->base_high = (uint32_t)(handler >> 32);
  entry->sel = 0x08;
  entry->flags = 0x8e;
  entry->reserved = 0;
}

int test_kernel_stack_guard(void) {
  uint64_t irq_stack_base = read_gs_qword(40);
  uint64_t irq_stack_top = read_gs_qword(32);
  if (!irq_stack_base || irq_stack_top <= irq_stack_base ||
      (current_task && current_task->kernel_stack_base < irq_stack_top &&
       current_task->kernel_stack > irq_stack_base))
    FAIL("kernel_stack_guard", "per-CPU IRQ stack is missing or aliases task stack");

  page_directory_t *existing_mm = vmm_create_directory();
  if (!existing_mm)
    FAIL("kernel_stack_guard", "could not create address-space mapping target");

  size_t size = 0;
  uint64_t base = (uint64_t)kernel_stack_alloc(KERNEL_STACK_MIN_SIZE, &size);
  if (!base) {
    vmm_free_directory(existing_mm);
    FAIL("kernel_stack_guard", "guarded stack allocation failed");
  }
  if (size < KERNEL_STACK_MIN_SIZE || size > KERNEL_STACK_MAX_SIZE ||
      (base & (PAGE_SIZE - 1))) {
    kernel_stack_free((void *)base, size);
    vmm_free_directory(existing_mm);
    FAIL("kernel_stack_guard", "stack size or alignment is invalid");
  }

  uint64_t low_guard = base - PAGE_SIZE;
  uint64_t high_guard = base + size;
  if (vmm_page_present_in(kernel_directory, low_guard) ||
      vmm_page_present_in(kernel_directory, high_guard) ||
      !vmm_page_present_in(kernel_directory, base) ||
      vmm_page_present_in(existing_mm, low_guard) ||
      vmm_page_present_in(existing_mm, high_guard) ||
      !vmm_page_present_in(existing_mm, base) ||
      !kernel_stack_is_guard(low_guard) ||
      !kernel_stack_is_guard(high_guard)) {
    kernel_stack_free((void *)base, size);
    vmm_free_directory(existing_mm);
    FAIL("kernel_stack_guard", "guard presence/ownership is incorrect");
  }

  idt_entry_t old_page_fault = idt_entries[14];
  uint64_t irq_flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(irq_flags) :: "memory");
  test_kstack_guard_fault_addr = 0;
  idt_set_test_handler(&idt_entries[14],
                       (uint64_t)test_kstack_guard_fault_handler);
  test_kstack_guard_write((void *)(low_guard + 64));
  idt_entries[14] = old_page_fault;
  if (irq_flags & (1ULL << 9)) asm volatile("sti" ::: "memory");

  if ((test_kstack_guard_fault_addr & ~(uint64_t)(PAGE_SIZE - 1)) !=
      low_guard) {
    kernel_stack_free((void *)base, size);
    vmm_free_directory(existing_mm);
    FAIL("kernel_stack_guard", "guard access did not raise #PF at guard page");
  }
  kernel_stack_free((void *)base, size);
  if (vmm_page_present_in(kernel_directory, base) ||
      vmm_page_present_in(existing_mm, base)) {
    vmm_free_directory(existing_mm);
    FAIL("kernel_stack_guard", "freed stack mapping remains present");
  }
  vmm_free_directory(existing_mm);
  PASS("kernel stack guard faults on IST1 and stack mappings are released");
}
#endif
