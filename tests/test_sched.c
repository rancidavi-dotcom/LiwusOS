#ifdef KERNEL_TEST
#include "framework.h"
#include "task.h"
#include "timer.h"
#include "serial.h"
#include "string.h"

/* Workers de CPU: ficam girando enquanto sched_run != 0. Ao final do teste
 * entram num sleep longo (nao consomem CPU) para nao interferir nos testes
 * seguintes. */
static volatile int sched_run = 0;
static volatile uint64_t sched_loops[3];
static wait_queue_t sched_waitq;
static wait_queue_t sched_wait_gate;
static volatile int sched_wait_done;
static volatile int sched_wait_round_done;
static volatile int sched_wait_monitor_done;
#define WAITQ_WORKERS 8
#define WAITQ_ROUNDS 4

#define SCHED_WORKER(idx)                        \
  do {                                           \
    while (sched_run) {                          \
      sched_loops[idx]++;                        \
      asm volatile("" ::: "memory");             \
    }                                            \
    for (;;)                                     \
      task_sleep_ms(1000);                       \
  } while (0)

static void sched_worker0(void) { SCHED_WORKER(0); }
static void sched_worker1(void) { SCHED_WORKER(1); }
static void sched_worker2(void) { SCHED_WORKER(2); }

typedef struct {
  uint64_t cpu_ticks;
  int priority;
  uint8_t state;
} sched_info_t;

static void sched_info_of(int pid, sched_info_t *out) {
  task_info_t info[48];
  int n = task_snapshot(info, 48);
  out->cpu_ticks = 0;
  out->priority = -1;
  out->state = 0;
  for (int i = 0; i < n; i++) {
    if (info[i].id == pid) {
      out->cpu_ticks = info[i].cpu_ticks;
      out->priority = info[i].priority;
      out->state = (uint8_t)info[i].state;
      return;
    }
  }
}

/* Verifica que a prioridade vira fatia de CPU: prio 4 deve acumular
 * aproximadamente 4x os ticks da prio 1 no mesmo intervalo. */
int test_sched_priority(void) {
  /* Arma o loop ANTES de criar: a tarefa pode ser escalada no mesmo tick da
   * criacao, e se sched_run ainda fosse 0 ela dormiria sem trabalhar. */
  sched_loops[0] = sched_loops[1] = sched_loops[2] = 0;
  sched_run = 1;

  int a = create_task_named_prio(sched_worker0, "tprio1", 1);
  int b = create_task_named_prio(sched_worker1, "tprio2", 2);
  int c = create_task_named_prio(sched_worker2, "tprio4", 4);
  if (a < 0 || b < 0 || c < 0)
    FAIL("sched_priority", "create_task_named_prio falhou");

  task_sleep_ms(500); /* ~50 ticks compartilhados entre os workers */
  sched_run = 0;
  task_sleep_ms(20); /* deixa os workers sairem do loop */

  sched_info_t ia, ib, ic;
  sched_info_of(a, &ia);
  sched_info_of(b, &ib);
  sched_info_of(c, &ic);

  char msg[96];
  serial_print("  [sched] cpu_ticks prio1=");
  itoa((int)ia.cpu_ticks, msg, 10); serial_print(msg);
  serial_print(" prio2=");
  itoa((int)ib.cpu_ticks, msg, 10); serial_print(msg);
  serial_print(" prio4=");
  itoa((int)ic.cpu_ticks, msg, 10); serial_print(msg);
  serial_print(" | loops ");
  itoa((int)sched_loops[0], msg, 10); serial_print(msg);
  serial_print(" ");
  itoa((int)sched_loops[1], msg, 10); serial_print(msg);
  serial_print(" ");
  itoa((int)sched_loops[2], msg, 10); serial_print(msg);
  serial_print(" | prio_final ");
  itoa(ia.priority, msg, 10); serial_print(msg);
  serial_print(" ");
  itoa(ib.priority, msg, 10); serial_print(msg);
  serial_print(" ");
  itoa(ic.priority, msg, 10); serial_print(msg);
  serial_print(" | st ");
  itoa(ia.state, msg, 10); serial_print(msg);
  serial_print(" ");
  itoa(ib.state, msg, 10); serial_print(msg);
  serial_print(" ");
  itoa(ic.state, msg, 10); serial_print(msg);
  serial_print("\n");

  ASSERT(ic.cpu_ticks > ib.cpu_ticks && ib.cpu_ticks > ia.cpu_ticks,
         "prioridade maior deveria acumular mais ticks");
  ASSERT(ic.cpu_ticks >= 2 * ia.cpu_ticks,
         "prio4 deveria rodar bem mais que prio1");

  PASS("sched_priority");
}

/* Verifica que task_sleep_ms realmente bloqueia pelo tempo pedido. */
int test_sched_sleep(void) {
  uint32_t start = timer_ticks;
  task_sleep_ms(200); /* 20 ticks a 100 Hz */
  uint32_t elapsed = timer_ticks - start;

  char msg[48];
  serial_print("  [sched] sleep 200ms => ");
  itoa((int)elapsed, msg, 10); serial_print(msg);
  serial_print(" ticks\n");

  ASSERT(elapsed >= 18, "sleep acordou cedo demais");
  ASSERT(elapsed <= 40, "sleep demorou demais para acordar");

  PASS("sched_sleep");
}

static void sched_waitq_worker(void) {
  for (int round = 0; round < WAITQ_ROUNDS; round++) {
    uint64_t flags = task_scheduler_lock();
    int rc = wait_queue_block_locked(&sched_waitq, 0, 0, false, NULL, NULL);
    task_scheduler_unlock(flags);
    if (rc == 0) switch_task();
    __sync_fetch_and_add(&sched_wait_round_done, 1);
    if (round + 1 < WAITQ_ROUNDS) {
      flags = task_scheduler_lock();
      rc = wait_queue_block_locked(&sched_wait_gate, 0, 0, false, NULL, NULL);
      task_scheduler_unlock(flags);
      if (rc == 0) switch_task();
    }
  }
  __sync_fetch_and_add(&sched_wait_done, 1);
  for (;;) task_sleep_ms(1000);
}

static void sched_wait_monitor(void) {
  for (volatile uint64_t work = 0; work < 10000; work++)
    asm volatile("pause" ::: "memory");
  sched_wait_monitor_done = 1;
  for (;;) task_sleep_ms(1000);
}

static int waitq_workers_ready(uint32_t expected) {
  for (uint32_t tick = 0; tick < 100; tick++) {
    if (wait_queue_count(&sched_waitq) == expected) return 1;
    task_sleep_ms(10);
  }
  return 0;
}

static uint64_t task_ticks_for(int id) {
  task_info_t info[64];
  int count = task_snapshot(info, 64);
  for (int i = 0; i < count; i++)
    if (info[i].id == id) return info[i].cpu_ticks;
  return UINT64_MAX;
}

int test_sched_wait_queue(void) {
  wait_queue_init(&sched_waitq);
  wait_queue_init(&sched_wait_gate);
  sched_wait_done = 0;
  sched_wait_round_done = 0;
  sched_wait_monitor_done = 0;
  int ids[WAITQ_WORKERS];
  for (int i = 0; i < WAITQ_WORKERS; i++) {
    ids[i] = create_task_named(sched_waitq_worker, "waitq-worker");
    if (ids[i] < 0) FAIL("sched_wait_queue", "worker creation failed");
  }
  if (!waitq_workers_ready(WAITQ_WORKERS))
    FAIL("sched_wait_queue", "workers did not all block");

  for (int round = 0; round < WAITQ_ROUNDS; round++) {
    if (round == 0) {
      uint64_t ticks[WAITQ_WORKERS];
      for (int i = 0; i < WAITQ_WORKERS; i++) ticks[i] = task_ticks_for(ids[i]);
      if (create_task_named(sched_wait_monitor, "waitq-monitor") < 0)
        FAIL("sched_wait_queue", "monitor creation failed");
      for (int attempt = 0; attempt < 16 && !sched_wait_monitor_done;
           attempt++)
        switch_task();
      ASSERT(sched_wait_monitor_done,
             "scheduler monitor did not finish its bounded work");
      for (int i = 0; i < WAITQ_WORKERS; i++)
        ASSERT(task_ticks_for(ids[i]) == ticks[i],
               "blocked waiters consumed scheduler ticks");
    }
    if (!waitq_workers_ready(WAITQ_WORKERS))
      FAIL("sched_wait_queue", "workers failed to requeue");
    if (round == 0) {
      ASSERT(wait_queue_wake(&sched_waitq, 3) == 3,
             "wake-one-count did not wake exactly three waiters");
      ASSERT(wait_queue_wake(&sched_waitq, UINT32_MAX) == WAITQ_WORKERS - 3,
             "wake-all did not wake remaining waiters");
    } else {
      ASSERT(wait_queue_wake(&sched_waitq, UINT32_MAX) == WAITQ_WORKERS,
             "wake-all did not wake all workers in stress round");
    }
    for (uint32_t tick = 0; tick < 100 &&
         sched_wait_round_done != WAITQ_WORKERS; tick++)
      task_sleep_ms(10);
    ASSERT(sched_wait_round_done == WAITQ_WORKERS,
           "workers failed bounded round completion");
    if (round + 1 < WAITQ_ROUNDS) {
      sched_wait_round_done = 0;
      ASSERT(wait_queue_wake(&sched_wait_gate, UINT32_MAX) == WAITQ_WORKERS,
             "round gate did not release all workers");
    }
  }

  for (uint32_t tick = 0; tick < 100 && sched_wait_done != WAITQ_WORKERS;
       tick++)
    task_sleep_ms(10);
  ASSERT(sched_wait_done == WAITQ_WORKERS,
         "not all waiters resumed before bounded deadline");
  ASSERT(wait_queue_wake(&sched_waitq, 1) == 0,
         "wake on empty queue should return zero");
  PASS("sched_wait_queue");
}

#endif /* KERNEL_TEST */
