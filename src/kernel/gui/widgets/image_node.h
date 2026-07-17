#ifndef IMAGE_NODE_H
#define IMAGE_NODE_H

#include "../scene/node.h"
#include <stdint.h>

node_t* image_node_create(const char *name, int width, int height, uint32_t *buffer);
void image_node_update(node_t *node, uint32_t *buffer, int buffer_size);

#endif
