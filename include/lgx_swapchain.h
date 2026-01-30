#ifndef LGX_SWAPCHAIN_H
#define LGX_SWAPCHAIN_H

#include "lgx_types.h"

/**
 * @brief Informações para criação da Swapchain
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    lg_format_t format;
} lg_swapchain_create_info_t;

/**
 * @brief Cria uma swapchain para o display atual
 */
lg_result_t lg_create_swapchain(lg_device_t device, const lg_swapchain_create_info_t* create_info, lg_swapchain_t* swapchain);
void lg_destroy_swapchain(lg_device_t device, lg_swapchain_t swapchain);

/**
 * @brief Obtém as imagens da swapchain (backbuffers)
 */
lg_result_t lg_get_swapchain_images(lg_device_t device, lg_swapchain_t swapchain, uint32_t* count, lg_image_t* images);
lg_image_t lg_get_swapchain_image(lg_swapchain_t swapchain, uint32_t index);

/**
 * @brief Apresenta a imagem na tela
 */
lg_result_t lg_queue_present(lg_queue_t queue, lg_swapchain_t swapchain, uint32_t image_index);

#endif
