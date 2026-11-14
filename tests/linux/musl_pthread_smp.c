#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define THREAD_COUNT 4
#define ITERATIONS 1000

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned counter;

static void *worker(void *unused) {
  (void)unused;
  char name[16] = {0};
  if (pthread_setname_np(pthread_self(), "liwus-worker") != 0 ||
      pthread_getname_np(pthread_self(), name, sizeof(name)) != 0 ||
      strcmp(name, "liwus-worker") != 0)
    return (void *)3;
  for (unsigned i = 0; i < ITERATIONS; ++i) {
    if (pthread_mutex_lock(&lock) != 0) return (void *)1;
    ++counter;
    if (pthread_mutex_unlock(&lock) != 0) return (void *)2;
  }
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
  for (unsigned i = 0; i < THREAD_COUNT; ++i) {
    void *result = 0;
    int rc = pthread_join(threads[i], &result);
    if (rc != 0 || result != 0) {
      printf("pthread_join[%u] failed: rc=%d result=%p\n", i, rc, result);
      return 11;
    }
  }
  if (counter != THREAD_COUNT * ITERATIONS) {
    printf("mutex counter mismatch: %u\n", counter);
    return 12;
  }
  puts("static musl 4-thread mutex/join PASS");
  return 0;
}
