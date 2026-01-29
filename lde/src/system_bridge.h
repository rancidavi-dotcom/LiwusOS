#ifndef SYSTEM_BRIDGE_H
#define SYSTEM_BRIDGE_H

#include <stdint.h>

#define MAX_SYS_PROCESSES 128

typedef enum {
    PROC_CAT_TERMINAL = 0, // Industrial
    PROC_CAT_BROWSER = 1,  // Comercial
    PROC_CAT_GAME = 2,     // Parque
    PROC_CAT_SYSTEM = 3,   // Infraestrutura
    PROC_CAT_UNKNOWN = 4
} process_category_t;

typedef struct {
    int pid;
    char name[32];
    int memory_mb;
    int cpu_usage; // 0-100
    process_category_t category;
} sys_process_t;

typedef struct {
    int global_cpu_usage; // 0-100
    int global_ram_usage_mb;
    int global_ram_total_mb;
    
    int num_processes;
    sys_process_t processes[MAX_SYS_PROCESSES];
} system_state_t;

void system_bridge_init(void);
void system_bridge_update(void);
const system_state_t* system_bridge_get_state(void);

#endif
