#include "image_node.h"
#include "kheap.h"
#include "string.h"
#include "../math/rect.h"
#include "../render/compositor.h"

typedef struct {
    uint32_t *buffer;
    int width;
    int height;
} image_state_t;

static void image_render(node_t *node, struct gui_renderer *r) {
    if (!node || !r || !node->userdata) return;
    
    image_state_t *state = (image_state_t *)node->userdata;
    if (!state->buffer) return;

    // Use world_transform to get screen position
    int abs_x = (int)node->world_transform.tx;
    int abs_y = (int)node->world_transform.ty;

    extern compositor_t *g_compositor;
    if (g_compositor) {
        extern void compositor_draw_image(compositor_t *comp, int x, int y, int width, int height, const uint32_t *buffer);
        compositor_draw_image(g_compositor, abs_x, abs_y, state->width, state->height, state->buffer);
    }
}

static void image_destroy(node_t *node) {
    if (node->userdata) {
        image_state_t *state = (image_state_t *)node->userdata;
        if (state->buffer) {
            kfree(state->buffer);
        }
        kfree(state);
    }
}

static const node_vtable_t image_vtable = {
    .draw = image_render,
    .on_event = NULL,
    .layout = NULL,
    .destroy = image_destroy
};

node_t* image_node_create(const char *name, int width, int height, uint32_t *buffer) {
    node_t *node = node_create(NODE_IMAGE, name);
    if (!node) return NULL;

    image_state_t *state = kmalloc(sizeof(image_state_t));
    if (!state) {
        kfree(node);
        return NULL;
    }

    state->width = width;
    state->height = height;
    state->buffer = kmalloc(width * height * 4);
    if (buffer) {
        memcpy(state->buffer, buffer, width * height * 4);
    } else {
        memset(state->buffer, 0, width * height * 4);
    }

    node->userdata = state;
    node->width = width;
    node->height = height;
    node->vtable = &image_vtable;

    return node;
}

void image_node_update(node_t *node, uint32_t *buffer, int buffer_size) {
    if (!node || node->type != NODE_IMAGE || !node->userdata || !buffer) return;
    image_state_t *state = (image_state_t *)node->userdata;
    
    if (buffer_size > state->width * state->height) {
        buffer_size = state->width * state->height;
    }
    
    memcpy(state->buffer, buffer, buffer_size * 4);
    node_mark_dirty(node, NODE_DIRTY_PAINT);
}
