#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

void init_mouse();
void mouse_handler();
void mouse_handle_event(int x_rel, int y_rel, int buttons);
int32_t get_mouse_x();
int32_t get_mouse_y();
bool is_left_clicked();
bool is_right_clicked();

void mouse_set_sensitivity(uint8_t sensitivity);
uint8_t mouse_get_sensitivity(void);
void mouse_set_acceleration(bool enable);
bool mouse_get_acceleration(void);

#endif