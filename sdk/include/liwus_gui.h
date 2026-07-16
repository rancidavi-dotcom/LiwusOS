#ifndef LIWUS_GUI_H
#define LIWUS_GUI_H

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t Canvas;
typedef uint32_t Node;

/* Node Types (matching kernel node_type_t) */
#define NODE_GENERIC  0
#define NODE_CANVAS   1
#define NODE_GROUP    2
#define NODE_WINDOW   3
#define NODE_PANEL    4
#define NODE_BUTTON   5
#define NODE_LABEL    6
#define NODE_IMAGE    7
#define NODE_TERMINAL 8
#define NODE_OVERLAY  9
#define NODE_DEBUG    10

/* 
 * Create a new window canvas for the app. 
 * The system automatically assigns a red close button that will kill this process when clicked.
 */
Canvas canvas_create(int width, int height, const char* title);

/* Create generic nodes */
Node text_create(const char* text);
Node button_create(const char* text);
Node panel_create(void);

/* Create an image node with a raw ARGB pixel buffer inside a canvas.
 * pixels: width * height uint32_t values in 0xAARRGGBB format.
 * The pixel data is COPIED into kernel memory.
 * Returns the image node ID, or 0 on failure.
 */
Node image_create(Canvas parent, int width, int height, const uint32_t *pixels);

/* Update the pixel buffer of an existing image node.
 * count: number of pixels (width * height).
 * The pixel data is COPIED into kernel memory.
 * Returns 0 on success, -1 on failure.
 */
int image_update(Node image, const uint32_t *pixels, uint32_t count);

/* Hierarchy and Spatial */
void canvas_add(Canvas canvas, Node child);
void node_add_child(Node parent, Node child);
void node_move(Node node, int x, int y);

/* Camera */
void camera_zoom(float zoom);

#endif /* LIWUS_GUI_H */
