#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* Inicializa o subsistema gráfico LVGL e os drivers de Display e Mouse */
void lvgl_port_init(void);

/* Atualiza timers e executa tarefas de renderização do LVGL */
void lvgl_port_update(void);
int lvgl_port_set_resolution(uint16_t width, uint16_t height);

/* Retorna o ponteiro do dispositivo de entrada do mouse */
lv_indev_t *lvgl_get_mouse_indev(void);

#endif /* LVGL_PORT_H */
