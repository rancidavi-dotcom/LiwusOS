.section .text
.global _start
.type _start, @function
_start:
  xor %rdi, %rdi
  mov $1, %rax
  int $0x80
1:
  jmp 1b
