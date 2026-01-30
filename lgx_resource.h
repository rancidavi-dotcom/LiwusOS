#ifndef LGX_RESOURCE_H
#define LGX_RESOURCE_H

#include "lgx_types.h"

/**
 * @brief Informações para alocação de memória
 */
typedef struct {
    size_t size;
    uint32_t memory_type_index;
} lg_memory_allocate_info_t;

/**
 * @brief Informações para criação de buffer
 */
typedef struct {
    size_t size;
    lg_buffer_usage_flags_t usage;
} lg_buffer_create_info_t;

/**
 * @brief Requisitos de memória para um recurso
 */
typedef struct {
    size_t size;
    size_t alignment;
    uint32_t memory_type_bits;
} lg_memory_requirements_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    lg_format_t format;
    lg_image_usage_flags_t usage;
} lg_image_create_info_t;

/* --- Funções de Memória --- */

lg_result_t lg_allocate_memory(lg_device_t device, const lg_memory_allocate_info_t* alloc_info, lg_device_memory_t* memory);
void lg_free_memory(lg_device_t device, lg_device_memory_t memory);

lg_result_t lg_map_memory(lg_device_t device, lg_device_memory_t memory, size_t offset, size_t size, void** data);
void lg_unmap_memory(lg_device_t device, lg_device_memory_t memory);

/* --- Funções de Buffer --- */

lg_result_t lg_create_buffer(lg_device_t device, const lg_buffer_create_info_t* create_info, lg_buffer_t* buffer);
void lg_destroy_buffer(lg_device_t device, lg_buffer_t buffer);

void lg_get_buffer_memory_requirements(lg_device_t device, lg_buffer_t buffer, lg_memory_requirements_t* requirements);
lg_result_t lg_bind_buffer_memory(lg_device_t device, lg_buffer_t buffer, lg_device_memory_t memory, size_t offset);

/* --- Funções de Imagem --- */

lg_result_t lg_create_image(lg_device_t device, const lg_image_create_info_t* create_info, lg_image_t* image);
void lg_destroy_image(lg_device_t device, lg_image_t image);

void lg_get_image_memory_requirements(lg_device_t device, lg_image_t image, lg_memory_requirements_t* requirements);
lg_result_t lg_bind_image_memory(lg_device_t device, lg_image_t image, lg_device_memory_t memory, size_t offset);

#endif
