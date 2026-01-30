#ifndef LGX_SHADER_H
#define LGX_SHADER_H

#include "lgx_types.h"

/**
 * @brief Tipos de estágios de shader
 */
typedef enum {
    LGX_SHADER_STAGE_VERTEX_BIT = 0x01,
    LGX_SHADER_STAGE_FRAGMENT_BIT = 0x10,
    LGX_SHADER_STAGE_COMPUTE_BIT = 0x20
} lg_shader_stage_flags_t;

/**
 * @brief Informações para criação do Shader Module
 */
typedef struct {
    size_t code_size;
    const uint32_t* p_code; // Bytecode LGSL (futuro) ou ponteiro para função (Software)
} lg_shader_module_create_info_t;

/**
 * @brief Configuração de um estágio no pipeline
 */
typedef struct {
    lg_shader_stage_flags_t stage;
    lg_shader_module_t module;
    const char* p_name; // Entry point (ex: "main")
} lg_pipeline_shader_stage_create_info_t;

/* --- Funções --- */

lg_result_t lg_create_shader_module(lg_device_t device, const lg_shader_module_create_info_t* create_info, lg_shader_module_t* shader_module);
void lg_destroy_shader_module(lg_device_t device, lg_shader_module_t shader_module);

#endif
