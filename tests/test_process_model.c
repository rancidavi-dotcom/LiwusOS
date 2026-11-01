#ifdef KERNEL_TEST
#include "framework.h"
#include "task.h"

int test_process_thread_ownership(void) {
  thread_t *running = current_task;
  if (!running || !running->process || running->process->leader != running ||
      running->process->threads != running ||
      running->process->thread_count != 1 ||
      running->process->references != 2 ||
      running->process->mm != current_directory)
    FAIL("process_thread_ownership",
         "running thread is not attached to its resource-owning process");

  thread_t probe_thread = {0};
  process_t *probe = task_process_create(0x7ffffffe,
                                        running->process,
                                        kernel_directory);
  if (!probe)
    FAIL("process_thread_ownership", "could not allocate process object");
  task_process_attach(probe, &probe_thread);
  if (probe_thread.process != probe || probe->leader != &probe_thread ||
      probe->threads != &probe_thread || probe->thread_count != 1 ||
      probe->references != 2 || probe->parent != running->process ||
      probe->pid == running->process->pid) {
    task_process_detach(probe, &probe_thread);
    task_process_discard(probe);
    FAIL("process_thread_ownership", "process/thread ownership invariant failed");
  }

  task_process_detach(probe, &probe_thread);
  if (probe_thread.process || probe->threads || probe->leader ||
      probe->thread_count || probe->references != 1) {
    task_process_discard(probe);
    FAIL("process_thread_ownership", "detaching thread did not release reference");
  }
  task_process_discard(probe);
  PASS("process resources are separate; thread attach/detach balances ownership");
}
#endif
