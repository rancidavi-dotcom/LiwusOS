#ifndef LGX_SYNC_H
#define LGX_SYNC_H

#include "lgx_types.h"

/**
 * @brief Estado de uma Fence
 */
typedef struct {
    bool signaled;
} lg_fence_create_info_t;

/* --- Funções de Fence (CPU-GPU) --- */

lg_result_t lg_create_fence(lg_device_t device, const lg_fence_create_info_t* create_info, lg_fence_t* fence);
void lg_destroy_fence(lg_device_t device, lg_fence_t fence);

lg_result_t lg_wait_for_fences(lg_device_t device, uint32_t fence_count, const lg_fence_t* fences, bool wait_all, uint64_t timeout);
lg_result_t lg_reset_fences(lg_device_t device, uint32_t fence_count, const lg_fence_t* fences);

/* --- Funções de Semáforo (GPU-GPU) --- */

lg_result_t lg_create_semaphore(lg_device_t device, lg_semaphore_t* semaphore);
void lg_destroy_semaphore(lg_device_t device, lg_semaphore_t semaphore);

#endif
