.section .text
.global _start
.extern main

_start:
    # Stack layout from sys_execve:
    # [ESP]   = argc
    # [ESP+4] = argv
    
    # Can we just call main? 
    # main expects:
    # push argv
    # push argc
    # call main
    
    # Since they are already on stack in reverse order?
    # sys_execve pushed:
    #   push argv_ptr
    #   push argc
    # ESP points to argc.
    
    # Convention: args are expected at ESP+4, ESP+8
    # So if we just 'call main', the return address is pushed.
    # Stack becomes: [RetAddr] [argc] [argv]
    # This matches `int main(int argc, char** argv)`!
    
    call main
    
    # Exit with return value
    call main
    
    # Exit with return value
    movl %eax, %ebx
    movl $1, %eax  # SYS_EXIT
    int $0x80
