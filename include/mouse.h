#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

void init_mouse();
void mouse_handler();
int32_t get_mouse_x();
int32_t get_mouse_y();
bool is_left_clicked();

#endif