#ifndef MOCK_BRIDGE_H
#define MOCK_BRIDGE_H

#include <stdint.h>

#define MAX_MOCK_PROCESSES 128

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
} mock_process_t;

typedef struct {
    int global_cpu_usage; // 0-100
    int global_ram_usage_mb;
    int global_ram_total_mb;
    
    int num_processes;
    mock_process_t processes[MAX_MOCK_PROCESSES];
} mock_system_state_t;

// Initialize the mock bridge
void mock_bridge_init(void);

// Advance the simulation (should be called 1-2 times per second)
void mock_bridge_update(void);

// Get the current system state
const mock_system_state_t* mock_bridge_get_state(void);

#endif
