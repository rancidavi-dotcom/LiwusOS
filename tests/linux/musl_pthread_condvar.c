#include <pthread.h>
#include <stdio.h>

#define THREAD_COUNT 4

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static unsigned ready;
static unsigned completed;
static int release_workers;

static void *worker(void *unused) {
  (void)unused;
  if (pthread_mutex_lock(&lock) != 0) return (void *)1;
  ++ready;
  pthread_cond_broadcast(&changed);
  while (!release_workers) {
    if (pthread_cond_wait(&changed, &lock) != 0) {
      pthread_mutex_unlock(&lock);
      return (void *)2;
    }
  }
  ++completed;
  pthread_mutex_unlock(&lock);
  return 0;
}

int main(void) {
  pthread_t threads[THREAD_COUNT];
  for (unsigned i = 0; i < THREAD_COUNT; ++i) {
    int rc = pthread_create(&threads[i], 0, worker, 0);
    if (rc != 0) {
      printf("pthread_create[%u] failed: %d\n", i, rc);
      return 10;
    }
  }

  if (pthread_mutex_lock(&lock) != 0) return 11;
  while (ready != THREAD_COUNT) {
    if (pthread_cond_wait(&changed, &lock) != 0) return 12;
  }
  release_workers = 1;
  pthread_cond_broadcast(&changed);
  pthread_mutex_unlock(&lock);

  for (unsigned i = 0; i < THREAD_COUNT; ++i) {
    void *result = 0;
    int rc = pthread_join(threads[i], &result);
    if (rc != 0 || result != 0) {
      printf("pthread_join[%u] failed: rc=%d result=%p\n", i, rc, result);
      return 13;
    }
  }
  if (ready != THREAD_COUNT || completed != THREAD_COUNT) {
    printf("condition-variable count mismatch: ready=%u completed=%u\n",
           ready, completed);
    return 14;
  }
  puts("static musl 4-thread condition-variable broadcast/join PASS");
  return 0;
}
