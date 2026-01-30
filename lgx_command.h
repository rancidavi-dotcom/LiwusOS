#ifndef LGX_COMMAND_H
#define LGX_COMMAND_H

#include "lgx_types.h"

/**
 * @brief Informações para criação do Command Pool
 */
typedef struct {
    uint32_t queue_family_index;
} lg_command_pool_create_info_t;

/**
 * @brief Informações para alocação de Command Buffers
 */
typedef struct {
    lg_command_pool_t command_pool;
    uint32_t count;
} lg_command_buffer_allocate_info_t;

/**
 * @brief Início da gravação de comandos
 */
typedef struct {
    bool one_time_submit_bit;
} lg_command_buffer_begin_info_t;

/**
 * @brief Submissão para a Fila
 */
typedef struct {
    uint32_t command_buffer_count;
    lg_command_buffer_t* command_buffers;
} lg_submit_info_t;

/* --- Funções de Fila e Pool --- */

void lg_get_device_queue(lg_device_t device, uint32_t queue_family_index, uint32_t queue_index, lg_queue_t* queue);

lg_result_t lg_create_command_pool(lg_device_t device, const lg_command_pool_create_info_t* create_info, lg_command_pool_t* command_pool);
void lg_destroy_command_pool(lg_device_t device, lg_command_pool_t command_pool);

lg_result_t lg_allocate_command_buffers(lg_device_t device, const lg_command_buffer_allocate_info_t* allocate_info, lg_command_buffer_t* command_buffers);

/* --- Gravação de Comandos --- */

lg_result_t lg_begin_command_buffer(lg_command_buffer_t command_buffer, const lg_command_buffer_begin_info_t* begin_info);
lg_result_t lg_end_command_buffer(lg_command_buffer_t command_buffer);

// Comandos específicos
void lg_cmd_copy_buffer(lg_command_buffer_t command_buffer, lg_buffer_t src, lg_buffer_t dst, size_t size);
void lg_cmd_fill_buffer(lg_command_buffer_t command_buffer, lg_buffer_t dst, uint32_t data);
void lg_cmd_copy_buffer_to_image(lg_command_buffer_t command_buffer, lg_buffer_t src, lg_image_t dst, int32_t dst_x, int32_t dst_y, uint32_t src_w, uint32_t src_h);
void lg_cmd_copy_image(lg_command_buffer_t command_buffer, lg_image_t src, lg_image_t dst, int32_t dst_x, int32_t dst_y);
void lg_cmd_blit_image(lg_command_buffer_t command_buffer, lg_image_t src, lg_image_t dst, int32_t dst_x, int32_t dst_y);

// Comandos de Renderização
void lg_cmd_bind_pipeline(lg_command_buffer_t command_buffer, lg_pipeline_t pipeline);
void lg_cmd_bind_descriptor_sets(lg_command_buffer_t command_buffer, lg_pipeline_layout_t layout, uint32_t first_set, uint32_t descriptor_set_count, const lg_descriptor_set_t* descriptor_sets);
void lg_cmd_bind_vertex_buffers(lg_command_buffer_t command_buffer, uint32_t first_binding, uint32_t binding_count, const lg_buffer_t* buffers, const size_t* offsets);
void lg_cmd_draw(lg_command_buffer_t command_buffer, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

// Comandos de Constantes
void lg_cmd_push_constants(lg_command_buffer_t command_buffer, lg_pipeline_t pipeline, uint32_t offset, uint32_t size, const void* values);

/* --- Execução --- */

lg_result_t lg_queue_submit(lg_queue_t queue, uint32_t submit_count, const lg_submit_info_t* submits);
lg_result_t lg_queue_wait_idle(lg_queue_t queue);

#endif
