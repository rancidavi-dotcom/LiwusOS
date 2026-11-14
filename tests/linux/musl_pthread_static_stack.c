#include <pthread.h>
#include <stdio.h>

static volatile int marker;
static __attribute__((aligned(16))) unsigned char thread_stack[65536];

static void *worker(void *arg) {
  marker = (int)(long)arg;
  return (void *)0x1234;
}

int main(void) {
  pthread_t thread;
  pthread_attr_t attr;
  void *result = 0;
  pthread_attr_init(&attr);
  if (pthread_attr_setstack(&attr, thread_stack, sizeof(thread_stack)) != 0)
    return 10;
  int rc = pthread_create(&thread, &attr, worker, (void *)42);
  if (rc != 0) {
    printf("pthread_create failed: %d\n", rc);
    return 11;
  }
  rc = pthread_join(thread, &result);
  if (rc != 0) {
    printf("pthread_join failed: %d\n", rc);
    return 12;
  }
  if (marker != 42 || result != (void *)0x1234) {
    printf("thread result mismatch: marker=%d result=%p\n", marker, result);
    return 13;
  }
  puts("static musl pthread create/join with caller stack PASS");
  return 0;
}
