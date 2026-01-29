/*
 * gui/layout/layout_engine.c
 */
#include "layout_engine.h"

static void layout_vbox(node_t *node) {
    if (!node || node->child_count == 0) return;

    int current_y = node->padding[0]; /* top padding */
    int available_w = node->width - node->padding[1] - node->padding[3];
    int available_h = node->height - node->padding[0] - node->padding[2];
    
    /* Pre-pass: calculate total flex weight and fixed height used */
    int total_flex = 0;
    int fixed_h = 0;
    for (uint32_t i = 0; i < node->child_count; i++) {
        node_t *child = node->children[i];
        if (!child || !child->visible) continue;

        if (child->flex_weight > 0) {
            total_flex += child->flex_weight;
        } else {
            fixed_h += child->height + child->margin[0] + child->margin[2];
        }
    }

    int remaining_h = available_h - fixed_h;
    if (remaining_h < 0) remaining_h = 0;

    for (uint32_t i = 0; i < node->child_count; i++) {
        node_t *child = node->children[i];
        if (!child || !child->visible) continue;

        /* Calculate height */
        int ch = child->height;
        if (child->flex_weight > 0 && total_flex > 0) {
            ch = (remaining_h * child->flex_weight) / total_flex;
            ch -= (child->margin[0] + child->margin[2]);
            if (ch < 0) ch = 0;
            child->height = ch;
        }

        /* Calculate width and X based on alignment */
        int cw = child->width;
        int cx = node->padding[3] + child->margin[3];

        if (child->layout_align == ALIGN_STRETCH) {
            cw = available_w - child->margin[1] - child->margin[3];
            if (cw < 0) cw = 0;
            child->width = cw;
        } else if (child->layout_align == ALIGN_CENTER) {
            cx = node->padding[3] + (available_w - cw) / 2;
        } else if (child->layout_align == ALIGN_END) {
            cx = node->width - node->padding[1] - child->margin[1] - cw;
        }

        /* Set Y */
        int cy = current_y + child->margin[0];
        
        node_set_position(child, cx, cy);

        current_y = cy + ch + child->margin[2];

        /* Recurse */
        layout_engine_compute(child);
    }
}

static void layout_hbox(node_t *node) {
    if (!node || node->child_count == 0) return;

    int current_x = node->padding[3]; /* left padding */
    int available_w = node->width - node->padding[1] - node->padding[3];
    int available_h = node->height - node->padding[0] - node->padding[2];
    
    /* Pre-pass */
    int total_flex = 0;
    int fixed_w = 0;
    for (uint32_t i = 0; i < node->child_count; i++) {
        node_t *child = node->children[i];
        if (!child || !child->visible) continue;

        if (child->flex_weight > 0) {
            total_flex += child->flex_weight;
        } else {
            fixed_w += child->width + child->margin[1] + child->margin[3];
        }
    }

    int remaining_w = available_w - fixed_w;
    if (remaining_w < 0) remaining_w = 0;

    for (uint32_t i = 0; i < node->child_count; i++) {
        node_t *child = node->children[i];
        if (!child || !child->visible) continue;

        /* Calculate width */
        int cw = child->width;
        if (child->flex_weight > 0 && total_flex > 0) {
            cw = (remaining_w * child->flex_weight) / total_flex;
            cw -= (child->margin[1] + child->margin[3]);
            if (cw < 0) cw = 0;
            child->width = cw;
        }

        /* Calculate height and Y based on alignment */
        int ch = child->height;
        int cy = node->padding[0] + child->margin[0];

        if (child->layout_align == ALIGN_STRETCH) {
            ch = available_h - child->margin[0] - child->margin[2];
            if (ch < 0) ch = 0;
            child->height = ch;
        } else if (child->layout_align == ALIGN_CENTER) {
            cy = node->padding[0] + (available_h - ch) / 2;
        } else if (child->layout_align == ALIGN_END) {
            cy = node->height - node->padding[2] - child->margin[2] - ch;
        }

        /* Set X */
        int cx = current_x + child->margin[3];
        
        node_set_position(child, cx, cy);

        current_x = cx + cw + child->margin[1];

        /* Recurse */
        layout_engine_compute(child);
    }
}

void layout_engine_compute(node_t *node) {
    if (!node || !node->visible) return;

    /* If the node subtype has a custom layout, it can override this behavior (e.g. text wrapping).
     * But for now, we use the engine for general container layouts. */
    if (node->vtable && node->vtable->layout) {
        node->vtable->layout(node);
    }

    if (node->layout_type == LAYOUT_VBOX) {
        layout_vbox(node);
    } else if (node->layout_type == LAYOUT_HBOX) {
        layout_hbox(node);
    } else {
        /* Absolute layout: just recurse to children */
        for (uint32_t i = 0; i < node->child_count; i++) {
            layout_engine_compute(node->children[i]);
        }
    }
    
    /* Clear dirty flag */
    node->dirty &= ~NODE_DIRTY_LAYOUT;
}
