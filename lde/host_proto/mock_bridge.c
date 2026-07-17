#include "mock_bridge.h"
#include <stdlib.h>
#include <string.h>

static mock_system_state_t s_state;

void mock_bridge_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.global_ram_total_mb = 4096;
    s_state.global_ram_usage_mb = 512;
    s_state.global_cpu_usage = 10;

    s_state.num_processes = 3;
    
    s_state.processes[0].pid = 1;
    strcpy(s_state.processes[0].name, "init");
    s_state.processes[0].memory_mb = 4;
    s_state.processes[0].cpu_usage = 0;
    s_state.processes[0].category = PROC_CAT_SYSTEM;

    s_state.processes[1].pid = 2;
    strcpy(s_state.processes[1].name, "window_server");
    s_state.processes[1].memory_mb = 64;
    s_state.processes[1].cpu_usage = 5;
    s_state.processes[1].category = PROC_CAT_SYSTEM;

    s_state.processes[2].pid = 3;
    strcpy(s_state.processes[2].name, "terminal");
    s_state.processes[2].memory_mb = 32;
    s_state.processes[2].cpu_usage = 2;
    s_state.processes[2].category = PROC_CAT_TERMINAL;
}

void mock_bridge_update(void) {
    int cpu_delta = (rand() % 21) - 10;
    s_state.global_cpu_usage += cpu_delta;
    if (s_state.global_cpu_usage < 0) s_state.global_cpu_usage = 0;
    if (s_state.global_cpu_usage > 100) s_state.global_cpu_usage = 100;
    
    int ram_delta = (rand() % 65) - 32;
    s_state.global_ram_usage_mb += ram_delta;
    if (s_state.global_ram_usage_mb < 128) s_state.global_ram_usage_mb = 128;
    if (s_state.global_ram_usage_mb > s_state.global_ram_total_mb) s_state.global_ram_usage_mb = s_state.global_ram_total_mb;
    
    if (rand() % 100 < 8 && s_state.num_processes < MAX_MOCK_PROCESSES) {
        int idx = s_state.num_processes++;
        s_state.processes[idx].pid = 100 + (rand() % 900);
        
        int r = rand() % 4;
        if (r == 0) {
            strcpy(s_state.processes[idx].name, "browser");
            s_state.processes[idx].category = PROC_CAT_BROWSER;
        } else if (r == 1) {
            strcpy(s_state.processes[idx].name, "game");
            s_state.processes[idx].category = PROC_CAT_GAME;
        } else if (r == 2) {
            strcpy(s_state.processes[idx].name, "bash");
            s_state.processes[idx].category = PROC_CAT_TERMINAL;
        } else {
            strcpy(s_state.processes[idx].name, "driver");
            s_state.processes[idx].category = PROC_CAT_SYSTEM;
        }
        
        s_state.processes[idx].memory_mb = 16 + (rand() % 128);
        s_state.processes[idx].cpu_usage = rand() % 30;
    } else if (rand() % 100 < 5 && s_state.num_processes > 3) {
        int idx = 3 + (rand() % (s_state.num_processes - 3));
        s_state.processes[idx] = s_state.processes[s_state.num_processes - 1];
        s_state.num_processes--;
    }
}

const mock_system_state_t* mock_bridge_get_state(void) {
    return &s_state;
}
