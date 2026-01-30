#ifndef LGX_TYPES_H
#define LGX_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Resultados de operações LGX
 */
typedef enum {
    LGX_SUCCESS = 0,
    LGX_ERROR_OUT_OF_MEMORY = -1,
    LGX_ERROR_DEVICE_LOST = -2,
    LGX_ERROR_INITIALIZATION_FAILED = -3,
    LGX_ERROR_INVALID_FORMAT = -4,
    LGX_ERROR_NOT_SUPPORTED = -5
} lg_result_t;

/**
 * @brief Formatos de pixels suportados
 */
typedef enum {
    LGX_FORMAT_UNDEFINED = 0,
    LGX_FORMAT_R8G8B8A8_UNORM,
    LGX_FORMAT_B8G8R8A8_UNORM,
    LGX_FORMAT_D24_UNORM_S8_UINT,
    LGX_FORMAT_R16G16B16A16_SFLOAT
} lg_format_t;

/**
 * @brief Flags de propriedade de memória
 */
typedef enum {
    LGX_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 0x01, // VRAM (Rápida para GPU)
    LGX_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x02, // CPU consegue escrever
    LGX_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x04 // Sincronização automática
} lg_memory_property_flags_t;

/**
 * @brief Tipos de uso de Buffer
 */
typedef enum {
    LGX_BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x01,
    LGX_BUFFER_USAGE_INDEX_BUFFER_BIT = 0x02,
    LGX_BUFFER_USAGE_UNIFORM_BUFFER_BIT = 0x04,
    LGX_BUFFER_USAGE_TRANSFER_SRC_BIT = 0x08,
    LGX_BUFFER_USAGE_TRANSFER_DST_BIT = 0x10
} lg_buffer_usage_flags_t;

/**
 * @brief Tipos de uso de Imagem
 */
typedef enum {
    LGX_IMAGE_USAGE_SAMPLED_BIT = 0x01,
    LGX_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x02,
    LGX_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x04,
    LGX_IMAGE_USAGE_TRANSFER_SRC_BIT = 0x08,
    LGX_IMAGE_USAGE_TRANSFER_DST_BIT = 0x10
} lg_image_usage_flags_t;

/**
 * @brief Opaque handles para objetos LGX
 */
typedef struct lg_instance_s* lg_instance_t;
typedef struct lg_physical_device_s* lg_physical_device_t;
typedef struct lg_device_s* lg_device_t;
typedef struct lg_device_memory_s* lg_device_memory_t;
typedef struct lg_queue_s* lg_queue_t;
typedef struct lg_command_pool_s* lg_command_pool_t;
typedef struct lg_buffer_s* lg_buffer_t;
typedef struct lg_image_s* lg_image_t;
typedef struct lg_swapchain_s* lg_swapchain_t;
typedef struct lg_render_pass_s* lg_render_pass_t;
typedef struct lg_framebuffer_s* lg_framebuffer_t;
typedef struct lg_descriptor_set_layout_s* lg_descriptor_set_layout_t;
typedef struct lg_pipeline_layout_s* lg_pipeline_layout_t;
typedef struct lg_descriptor_pool_s* lg_descriptor_pool_t;
typedef struct lg_descriptor_set_s* lg_descriptor_set_t;
typedef struct lg_shader_module_s* lg_shader_module_t;
typedef struct lg_pipeline_s* lg_pipeline_t;
typedef struct lg_compute_pipeline_s* lg_compute_pipeline_t;
typedef struct lg_fence_s* lg_fence_t;
typedef struct lg_semaphore_s* lg_semaphore_t;
typedef struct lg_command_buffer_s* lg_command_buffer_t;

#endif
