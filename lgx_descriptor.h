#ifndef LGX_DESCRIPTOR_H
#define LGX_DESCRIPTOR_H

#include "lgx_types.h"

/**
 * @brief Tipos de Descriptors
 */
typedef enum {
    LGX_DESCRIPTOR_TYPE_UNIFORM_BUFFER = 0,
    LGX_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1,
    LGX_DESCRIPTOR_TYPE_STORAGE_BUFFER = 2
} lg_descriptor_type_t;

/**
 * @brief Binding de um descriptor no layout
 */
typedef struct {
    uint32_t binding;
    lg_descriptor_type_t descriptor_type;
    uint32_t descriptor_count;
} lg_descriptor_set_layout_binding_t;

typedef struct {
    uint32_t binding_count;
    const lg_descriptor_set_layout_binding_t* bindings;
} lg_descriptor_set_layout_create_info_t;

/**
 * @brief Informações para criação do Pipeline Layout
 */
typedef struct {
    uint32_t set_layout_count;
    const lg_descriptor_set_layout_t* set_layouts;
} lg_pipeline_layout_create_info_t;

/**
 * @brief Pool de Descriptors
 */
typedef struct {
    lg_descriptor_type_t type;
    uint32_t descriptor_count;
} lg_descriptor_pool_size_t;

typedef struct {
    uint32_t max_sets;
    uint32_t pool_size_count;
    const lg_descriptor_pool_size_t* pool_sizes;
} lg_descriptor_pool_create_info_t;

/**
 * @brief Alocação de Descriptor Sets
 */
typedef struct {
    lg_descriptor_pool_t descriptor_pool;
    uint32_t descriptor_set_count;
    const lg_descriptor_set_layout_t* set_layouts;
} lg_descriptor_set_allocate_info_t;

/**
 * @brief Escrita/Atualização de Descriptors
 */
typedef struct {
    lg_descriptor_set_t dst_set;
    uint32_t dst_binding;
    lg_descriptor_type_t descriptor_type;
    uint32_t descriptor_count;
    lg_buffer_t buffer; // Se for buffer
    lg_image_t image;   // Se for imagem
} lg_write_descriptor_set_t;

/* --- Funções de Layout --- */

lg_result_t lg_create_descriptor_set_layout(lg_device_t device, const lg_descriptor_set_layout_create_info_t* create_info, lg_descriptor_set_layout_t* layout);
lg_result_t lg_create_pipeline_layout(lg_device_t device, const lg_pipeline_layout_create_info_t* create_info, lg_pipeline_layout_t* layout);

/* --- Funções de Pool e Sets --- */

lg_result_t lg_create_descriptor_pool(lg_device_t device, const lg_descriptor_pool_create_info_t* create_info, lg_descriptor_pool_t* pool);
lg_result_t lg_allocate_descriptor_sets(lg_device_t device, const lg_descriptor_set_allocate_info_t* allocate_info, lg_descriptor_set_t* descriptor_sets);

/* --- Atualização --- */

void lg_update_descriptor_sets(lg_device_t device, uint32_t descriptor_write_count, const lg_write_descriptor_set_t* descriptor_writes);

#endif
