#include "system_bridge.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static system_state_t s_state;

void system_bridge_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.global_ram_total_mb = 1024;
}

static process_category_t categorize_process(const char* name) {
    if (strstr(name, "gui") || strstr(name, "hello")) return PROC_CAT_BROWSER; // Mock as comercial
    if (strstr(name, "calc") || strstr(name, "kilo") || strstr(name, "lde")) return PROC_CAT_TERMINAL; // Mock as industrial
    if (strstr(name, "doom")) return PROC_CAT_GAME;
    if (strstr(name, "demo")) return PROC_CAT_BROWSER; // Use BROWSER as a mock for demo
    return PROC_CAT_SYSTEM;
}

typedef enum {
  TASK_RUNNING,
  TASK_READY,
  TASK_SLEEPING,
  TASK_ZOMBIE
} task_state_t;

typedef struct {
  int id;
  int parent_id;
  task_state_t state;
  uint64_t heap_start;
  uint64_t heap_end;
  uint64_t cpu_ticks;
  uint64_t switch_count;
  bool user_mode;
  char name[32];
} k_task_info_t;

static inline uint64_t syscall2(uint64_t n, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    asm volatile(
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "int $0x80\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(n), "r"(a1), "r"(a2)
        : "rax", "rdi", "rsi", "memory");
    return ret;
}

void system_bridge_update(void) {
    s_state.global_cpu_usage = 10;
    s_state.global_ram_total_mb = 1024;
    s_state.global_ram_usage_mb = 256;
    
    k_task_info_t tasks[32];
    int count = syscall2(34, (uint64_t)tasks, 32);
    
    if (count > 0) {
        s_state.num_processes = 0;
        for (int i = 0; i < count && i < MAX_SYS_PROCESSES; i++) {
            // Ignore kernel/idle tasks if they don't have user_mode
            if (!tasks[i].user_mode && tasks[i].id == 0) continue; 
            
            int idx = s_state.num_processes++;
            s_state.processes[idx].pid = tasks[i].id;
            s_state.processes[idx].memory_mb = 16; // mock value for now
            s_state.processes[idx].cpu_usage = 5; // mock value for now
            strncpy(s_state.processes[idx].name, tasks[i].name, 31);
            s_state.processes[idx].name[31] = '\0';
            s_state.processes[idx].category = categorize_process(tasks[i].name);
        }
    }
}

const system_state_t* system_bridge_get_state(void) {
    return &s_state;
}
