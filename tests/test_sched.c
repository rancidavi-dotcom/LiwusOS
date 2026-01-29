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

#endif /* KERNEL_TEST */
