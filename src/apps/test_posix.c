#include <libliw.h>
#include <stdbool.h>
#include <stdint.h>

void test_posix() {
  print("POSIX Test Starting...\n");

  // 1. Test BRK (malloc/heap)
  void *initial_brk = (void *)syscall_brk(0);
  syscall_brk((uint32_t)initial_brk + 4096);
  void *new_brk = (void *)syscall_brk(0);

  if (new_brk > initial_brk) {
    print("BRK Test: PASS (Heap expanded)\n");
    // Test write to new heap
    *(int *)initial_brk = 0x12345678;
    if (*(int *)initial_brk == 0x12345678) {
      print("Heap Write Test: PASS\n");
    }
  }

  // 2. Test FORK and WAITPID
  print("Testing Fork...\n");
  int pid = syscall_fork();

  if (pid == 0) {
    // Child process
    print("Child: Sleeping for a bit...\n");
    for (volatile int i = 0; i < 1000000; i++)
      ;
    print("Child: Exiting with status 42\n");
    syscall_exit(42);
  } else if (pid > 0) {
    // Parent process
    print("Parent: Waiting for Child PID ");
    print_int(pid);
    print("...\n");

    int status = 0;
    int waited_pid = syscall_waitpid(pid, &status, 0);

    if (waited_pid == pid) {
      print("Parent: Child finished! Status: ");
      print_int(status);
      print("\n");

      if (status == 42) {
        print("POSIX FLOW TEST: SUCCESS!\n");
      }
    } else {
      print("Parent: Waitpid failed!\n");
    }
  } else {
    print("Fork FAILED\n");
  }

  syscall_exit(0);
}
