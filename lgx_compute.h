#ifndef LGX_COMPUTE_H
#define LGX_COMPUTE_H

#include "lgx_types.h"
#include "lgx_shader.h"

/**
 * @brief Informações para criação do Compute Pipeline
 */
typedef struct {
    lg_pipeline_shader_stage_create_info_t stage;
    lg_pipeline_layout_t layout;
} lg_compute_pipeline_create_info_t;

/* --- Funções de Criação --- */

lg_result_t lg_create_compute_pipelines(lg_device_t device, uint32_t create_info_count, const lg_compute_pipeline_create_info_t* create_infos, lg_compute_pipeline_t* pipelines);

/* --- Comandos de Execução --- */

/**
 * @brief Lança a execução de um shader de computação
 * @param group_count_x Número de grupos de trabalho em X
 * @param group_count_y Número de grupos de trabalho em Y
 * @param group_count_z Número de grupos de trabalho em Z
 */
void lg_cmd_dispatch(lg_command_buffer_t command_buffer, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

#endif
