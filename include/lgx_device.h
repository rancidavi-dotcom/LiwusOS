#ifndef LGX_DEVICE_H
#define LGX_DEVICE_H

#include "lgx_types.h"

/**
 * @brief Informações para criação da instância
 */
typedef struct {
    const char* app_name;
    uint32_t app_version;
} lg_instance_create_info_t;

/**
 * @brief Propriedades do dispositivo físico
 */
typedef struct {
    char name[256];
    uint32_t vendor_id;
    uint32_t device_id;
    bool is_software_renderer;
} lg_physical_device_props_t;

/**
 * @brief Cria uma nova instância do LGX
 */
lg_result_t lg_create_instance(const lg_instance_create_info_t* create_info, lg_instance_t* instance);

/**
 * @brief Destrói a instância
 */
void lg_destroy_instance(lg_instance_t instance);

/**
 * @brief Enumera os dispositivos físicos disponíveis
 */
lg_result_t lg_enumerate_physical_devices(lg_instance_t instance, uint32_t* count, lg_physical_device_t* devices);

/**
 * @brief Obtém propriedades de um dispositivo físico
 */
void lg_get_physical_device_props(lg_physical_device_t physical_device, lg_physical_device_props_t* props);

/**
 * @brief Cria um dispositivo lógico a partir de um físico
 */
lg_result_t lg_create_device(lg_physical_device_t physical_device, lg_device_t* device);

/**
 * @brief Destrói o dispositivo lógico
 */
void lg_destroy_device(lg_device_t device);

#endif
