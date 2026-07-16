/*
 * gui/widgets/image_node.h
 *
 * Image — Renders a raw ARGB pixel buffer inside the scene graph.
 *
 * Used by userspace apps (e.g. Doom) to push framebuffer content
 * into a LGX window via syscalls.
 */
#ifndef GUI_IMAGE_NODE_H
#define GUI_IMAGE_NODE_H

#include "../scene/node.h"

/*
 * Create an image node with a pixel buffer.
 * pixels: ARGB pixel data (width * height uint32_t values)
 * The buffer is COPIED into kernel-owned memory.
 * Returns NULL on failure.
 */
node_t *image_node_create(const char *name, int x, int y,
                           int width, int height,
                           const uint32_t *pixels);

/*
 * Update the pixel buffer of an existing image node.
 * count: number of pixels to update (width * height)
 * The buffer is COPIED into kernel-owned memory.
 * Returns 0 on success, -1 on failure.
 */
int image_node_update(node_t *node, const uint32_t *pixels, uint32_t count);

/*
 * Resize the image node and its pixel buffer.
 * Old pixel data is discarded.
 * Returns 0 on success, -1 on failure.
 */
int image_node_resize(node_t *node, int new_width, int new_height);

#endif /* GUI_IMAGE_NODE_H */
