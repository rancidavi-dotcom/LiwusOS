/*
 * gui/core/app_registry.h
 */
#ifndef GUI_APP_REGISTRY_H
#define GUI_APP_REGISTRY_H

#include <stdint.h>

typedef struct {
    const char *name;
    const char *icon;
    void (*start)(void);
} app_descriptor_t;

void app_registry_init(void);
void app_registry_add(const char *name, const char *icon, void (*start)(void));
uint32_t app_registry_get_count(void);
const app_descriptor_t *app_registry_get(uint32_t index);
void app_registry_show_launcher(void);
void app_registry_toggle_launcher(void);

#endif /* GUI_APP_REGISTRY_H */
