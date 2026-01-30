#ifndef LGX_PIPELINE_H
#define LGX_PIPELINE_H

#include "lgx_types.h"

/**
 * @brief Topologia das primitivas
 */
typedef enum {
    LGX_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 0,
    LGX_PRIMITIVE_TOPOLOGY_POINT_LIST = 1,
    LGX_PRIMITIVE_TOPOLOGY_LINE_LIST = 2
} lg_primitive_topology_t;

/**
 * @brief Configuração de Entrada de Vértices
 */
typedef struct {
    uint32_t binding;
    uint32_t stride;
} lg_vertex_input_binding_description_t;

typedef struct {
    uint32_t location;
    uint32_t binding;
    lg_format_t format;
    uint32_t offset;
} lg_vertex_input_attribute_description_t;

typedef struct {
    uint32_t vertex_binding_description_count;
    const lg_vertex_input_binding_description_t* vertex_binding_descriptions;
    uint32_t vertex_attribute_description_count;
    const lg_vertex_input_attribute_description_t* vertex_attribute_descriptions;
} lg_pipeline_vertex_input_state_create_info_t;

/**
 * @brief Configuração do Rasterizador
 */
typedef struct {
    lg_primitive_topology_t topology;
    bool depth_test_enable;
} lg_pipeline_rasterization_state_create_info_t;

/**
 * @brief Informações para criação do Pipeline Gráfico
 */
typedef struct {
    lg_pipeline_vertex_input_state_create_info_t vertex_input_state;
    lg_pipeline_rasterization_state_create_info_t rasterization_state;
    // No futuro: Shader modules, Blend state, etc.
} lg_graphics_pipeline_create_info_t;

/* --- Funções de Pipeline --- */

lg_result_t lg_create_graphics_pipelines(lg_device_t device, uint32_t create_info_count, const lg_graphics_pipeline_create_info_t* create_infos, lg_pipeline_t* pipelines);
void lg_destroy_pipeline(lg_device_t device, lg_pipeline_t pipeline);

#endif
