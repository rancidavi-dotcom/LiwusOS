#include "arch/sys_arch.h"
#include "timer.h"

u32_t sys_now(void) {
    /* timer roda a 100 Hz -> 1 tick = 10 ms */
    return timer_ticks * 10;
}
