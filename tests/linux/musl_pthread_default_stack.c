#include <pthread.h>
#include <stdio.h>

static volatile int marker;

static void *worker(void *arg) {
  marker = (int)(long)arg;
  return (void *)0x1234;
}

int main(void) {
  pthread_t thread;
  void *result = 0;
  int rc = pthread_create(&thread, 0, worker, (void *)42);
  if (rc != 0) {
    printf("pthread_create failed: %d\n", rc);
    return 10;
  }
  rc = pthread_join(thread, &result);
  if (rc != 0) {
    printf("pthread_join failed: %d\n", rc);
    return 11;
  }
  if (marker != 42 || result != (void *)0x1234) {
    printf("thread result mismatch: marker=%d result=%p\n", marker, result);
    return 12;
  }
  puts("static musl default-stack pthread create/join PASS");
  return 0;
}
