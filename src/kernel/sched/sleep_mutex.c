#include "sleep_mutex.h"
#include "string.h"

void sleep_mutex_init(sleep_mutex_t *mutex) {
  if (!mutex) return;
  memset(mutex, 0, sizeof(*mutex));
}

int sleep_mutex_lock(sleep_mutex_t *mutex) {
  if (!mutex) return -1;
  /* Boot-time filesystem setup is single-threaded and runs before GS-local
   * task state exists. In that phase this lock is intentionally a no-op. */
  if (!tasking_is_initialized()) return 0;
  if (!current_task) return -1;

  for (;;) {
    uint64_t flags = task_scheduler_lock();
    if (mutex->owner == current_task) {
      task_scheduler_unlock(flags);
      return -1; /* non-recursive: expose accidental lock-order cycles */
    }
    if (!mutex->owner) {
      mutex->owner = current_task;
      current_task->kernel_mutex_depth++;
      task_scheduler_unlock(flags);
      return 0;
    }
    if (wait_queue_block_locked(&mutex->waiters, 0, 0, false, NULL, NULL) < 0) {
      task_scheduler_unlock(flags);
      return -1;
    }
    task_scheduler_unlock(flags);
    switch_task();
  }
}

int sleep_mutex_unlock(sleep_mutex_t *mutex) {
  if (!mutex) return -1;
  if (!tasking_is_initialized()) return 0;
  if (!current_task) return -1;

  int terminate = 0;
  int exit_status = 0;
  uint64_t flags = task_scheduler_lock();
  if (mutex->owner != current_task || current_task->kernel_mutex_depth == 0) {
    task_scheduler_unlock(flags);
    return -1;
  }

  mutex->owner = NULL;
  current_task->kernel_mutex_depth--;
  wait_queue_wake_locked(&mutex->waiters, 0, 0, false, 1);
  if (current_task->kernel_mutex_depth == 0 && current_task->pending_kill) {
    terminate = 1;
    exit_status = current_task->pending_kill_status;
    current_task->pending_kill = 0;
  }
  task_scheduler_unlock(flags);

  /* A kill requested while the thread owned a filesystem transaction is
   * deferred until the protected operation has released the lock. */
  if (terminate) sys_exit_thread(exit_status);
  return 0;
}
