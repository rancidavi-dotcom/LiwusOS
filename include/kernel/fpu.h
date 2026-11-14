#ifndef KERNEL_FPU_H
#define KERNEL_FPU_H

#include <stdint.h>

/* Shared x86 task-state format selected once by the bootstrap processor. */
extern uint32_t fpu_context_size;
extern uint64_t fpu_context_mask;
extern int fpu_xsave_enabled;

/* Enable the selected x87/SSE state (and AVX when XSAVE is available) locally. */
void init_fpu(void);

#endif
