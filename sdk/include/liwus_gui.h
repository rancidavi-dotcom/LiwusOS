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

/* Hierarchy and Spatial */
void canvas_add(Canvas canvas, Node child);
void node_add_child(Node parent, Node child);
void node_move(Node node, int x, int y);

/* Camera */
void camera_zoom(float zoom);

#endif /* LIWUS_GUI_H */
