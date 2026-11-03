#ifndef SLEEP_MUTEX_H
#define SLEEP_MUTEX_H

#include "task.h"

/* Sleepable ownership lock for thread-context code that may wait for I/O.
 * Never acquire from an interrupt handler or while holding a spinlock. */
typedef struct {
  thread_t *owner;
  wait_queue_t waiters;
} sleep_mutex_t;

#define SLEEP_MUTEX_INITIALIZER { NULL, { NULL } }

void sleep_mutex_init(sleep_mutex_t *mutex);
int sleep_mutex_lock(sleep_mutex_t *mutex);
int sleep_mutex_unlock(sleep_mutex_t *mutex);

#endif
