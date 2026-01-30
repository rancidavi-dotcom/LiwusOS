#ifndef LGX_RENDERPASS_H
#define LGX_RENDERPASS_H

#include "lgx_types.h"

/**
 * @brief Operações de carga e armazenamento
 */
typedef enum {
    LGX_ATTACHMENT_LOAD_OP_LOAD = 0,
    LGX_ATTACHMENT_LOAD_OP_CLEAR = 1,
    LGX_ATTACHMENT_LOAD_OP_DONT_CARE = 2
} lg_attachment_load_op_t;

typedef enum {
    LGX_ATTACHMENT_STORE_OP_STORE = 0,
    LGX_ATTACHMENT_STORE_OP_DONT_CARE = 1
} lg_attachment_store_op_t;

/**
 * @brief Descrição de um anexo (Cor ou Profundidade)
 */
typedef struct {
    lg_format_t format;
    lg_attachment_load_op_t load_op;
    lg_attachment_store_op_t store_op;
} lg_attachment_description_t;

/**
 * @brief Informações para criação do Render Pass
 */
typedef struct {
    uint32_t attachment_count;
    const lg_attachment_description_t* attachments;
} lg_render_pass_create_info_t;

/**
 * @brief Informações para criação do Framebuffer
 */
typedef struct {
    lg_render_pass_t render_pass;
    uint32_t attachment_count;
    const lg_image_t* attachments;
    uint32_t width;
    uint32_t height;
} lg_framebuffer_create_info_t;

/**
 * @brief Início do Render Pass no Command Buffer
 */
typedef struct {
    lg_render_pass_t render_pass;
    lg_framebuffer_t framebuffer;
    uint32_t clear_color;
    float clear_depth;
} lg_render_pass_begin_info_t;

/* --- Funções --- */

lg_result_t lg_create_render_pass(lg_device_t device, const lg_render_pass_create_info_t* create_info, lg_render_pass_t* render_pass);
lg_result_t lg_create_framebuffer(lg_device_t device, const lg_framebuffer_create_info_t* create_info, lg_framebuffer_t* framebuffer);

void lg_cmd_begin_render_pass(lg_command_buffer_t command_buffer, const lg_render_pass_begin_info_t* begin_info);
void lg_cmd_end_render_pass(lg_command_buffer_t command_buffer);

#endif
