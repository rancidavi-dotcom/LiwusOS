/*
 * gui/layout/layout_engine.h
 *
 * Implements a simple Flexbox-like layout engine.
 */
#ifndef GUI_LAYOUT_ENGINE_H
#define GUI_LAYOUT_ENGINE_H

#include "../scene/node.h"

/* Recomputes the layout for the given node and all its children.
 * Call this when a node's layout properties or children change.
 */
void layout_engine_compute(node_t *node);

#endif
