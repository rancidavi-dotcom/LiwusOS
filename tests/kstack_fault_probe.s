.text
.global test_kstack_guard_write
.global test_kstack_guard_fault_handler
.extern test_kstack_guard_fault_addr

test_kstack_guard_write:
  movb $0, (%rdi)
.Lguard_write_recovered:
  ret

/* The test replaces #PF's handler temporarily. The gate retains IST1, so the
 * probe confirms the guard fault can be handled off the damaged task stack. */
test_kstack_guard_fault_handler:
  mov %cr2, %rax
  mov %rax, test_kstack_guard_fault_addr(%rip)
  leaq .Lguard_write_recovered(%rip), %rax
  mov %rax, 8(%rsp)              /* hardware RIP follows the error code */
  add $8, %rsp                   /* discard page-fault error code */
  iretq
