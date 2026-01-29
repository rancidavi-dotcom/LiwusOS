/*
 * gui/gui_syscalls.c
 *
 * Implementation of GUI syscalls (120-126) bridging userspace apps to the
 * LGX compositor scene graph.
 *
 * These run inside the kernel's syscall handler (called from userspace via
 * int $0x80) but are invoked with the CALLING task's page directory active,
 * so user pointers must be dereferenced through the usercopy helpers when
 * the process is in Ring 3 (current_task->user_mode == true).  When invoked
 * from a Ring 0 kernel task (e.g. the compositor itself or an in-kernel app),
 * pointers are direct kernel addresses and are used as-is.
 */
#include "gui_syscalls.h"
#include "task.h"
#include "usercopy.h"
#include "kheap.h"
#include "string.h"

#include "scene/node.h"
#include "scene/camera.h"
#include "widgets/window_node.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/panel.h"
#include "widgets/image_node.h"
#include "core/event_bus.h"
#include "window/window_manager.h"
#include "render/compositor.h"

/* --------------------------------------------------------------------------
 * Per-process GUI bookkeeping
 *
 * We keep a small array on the task (task_t.gui) of node handles the task
 * created.  When the process exits (sys_exit_process), sys_gui_cleanup_task
 * destroys those nodes so windows don't leak.  This is a bounded table —
 * GUI apps that need more concurrent nodes are expected to reuse handles.
 * -------------------------------------------------------------------------- */

#define GUI_MAX_HANDLES_PER_TASK 128

typedef struct {
    uint32_t handles[GUI_MAX_HANDLES_PER_TASK];
    uint32_t count;
} gui_task_state_t;

/* Pointer to a floating task_t member we add below. */
gui_task_state_t *gui_state_for_current_task(void) {
    if (!current_task) return NULL;
    /* Lazy allocation on first use. */
    if (!current_task->gui) {
        current_task->gui = (gui_task_state_t *)kmalloc(sizeof(gui_task_state_t));
        if (current_task->gui) {
            memset(current_task->gui, 0, sizeof(gui_task_state_t));
        }
    }
    return current_task->gui;
}

static void gui_track_handle(uint32_t handle) {
    gui_task_state_t *s = gui_state_for_current_task();
    if (!s || !handle) return;
    if (s->count >= GUI_MAX_HANDLES_PER_TASK) return;
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->handles[i] == handle) return; /* already tracked */
    }
    s->handles[s->count++] = handle;
}

/* --------------------------------------------------------------------------
 * Node lookup helpers
 * -------------------------------------------------------------------------- */

/* Find a node by handle (== node_t.id) anywhere in the scene graph. */
static node_t *gui_find_node(uint32_t handle) {
    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root || handle == 0) return NULL;
    return node_find_by_id(g_scene->root, handle);
}

/* Safe string copy from either user or kernel space. */
static char *gui_strdup(const char *str) {
    if (!str) return NULL;

    char stack_buf[256];
    int len;
    if (current_task && current_task->user_mode) {
        if (validate_user_pointer(str, 1) < 0) return NULL;
        if (strncpy_from_user(stack_buf, str, 255) < 0) return NULL;
        stack_buf[255] = '\0';
        len = strlen(stack_buf);
    } else {
        /* Kernel context: direct access */
        len = strlen(str);
        if (len >= 256) len = 255;
        memcpy(stack_buf, str, len);
        stack_buf[len] = '\0';
    }

    char *out = (char *)kmalloc(len + 1);
    if (!out) return NULL;
    memcpy(out, stack_buf, len + 1);
    return out;
}

/* Copy a pixel buffer (uint32 words) from user or kernel space. */
static int gui_copy_pixels(uint32_t *dst, const uint32_t *src, size_t words) {
    if (!dst || !src) return -1;
    if (current_task && current_task->user_mode) {
        if (validate_user_pointer(src, words * 4) < 0) return -1;
        if (copy_from_user(dst, src, words * 4) < 0) return -1;
    } else {
        memcpy(dst, src, words * 4);
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Syscall 120 — canvas_create
 * -------------------------------------------------------------------------- */

uint64_t sys_gui_canvas_create(uint64_t width, uint64_t height, const char *title) {
    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return 0;
    if (width == 0 || width > 4096) return 0;
    if (height == 0 || height > 4096) return 0;

    char *title_copy = gui_strdup(title);
    if (!title_copy && title) {
        /* fall back to empty title */
        title_copy = (char *)kmalloc(1);
        if (title_copy) title_copy[0] = '\0';
    }
    if (!title_copy) return 0;

    node_t *win = window_node_create("canvas", 100, 100,
                                     (int)width, (int)height,
                                     title_copy ? title_copy : "");
    kfree(title_copy);
    if (!win) return 0;

    /* Bind this window to the calling process, so the close button can kill it */
    if (current_task) {
        window_node_set_pid(win, current_task->id);
    }

    if (!node_add_child(g_scene->root, win)) {
        node_destroy(win);
        return 0;
    }

    uint32_t handle = win->id;
    gui_track_handle(handle);

    /* Bring to front so the new app is visible */
    extern void window_manager_bring_to_front(node_t *node);
    window_manager_bring_to_front(win);

    extern compositor_t *g_compositor;
    if (g_compositor) compositor_invalidate_full(g_compositor);

    return handle;
}

/* --------------------------------------------------------------------------
 * Syscall 121 — node_create (label / button / panel)
 * -------------------------------------------------------------------------- */

uint64_t sys_gui_node_create(uint64_t type, const char *text) {
    char *text_copy = gui_strdup(text);

    node_t *n = NULL;
    switch (type) {
        case NODE_LABEL: {
            n = label_create("label", 0, 0, text_copy ? text_copy : "", 0xFFE6E8EB);
            break;
        }
        case NODE_BUTTON: {
            n = button_create("button", 0, 0, 120, 28, text_copy ? text_copy : "Button");
            break;
        }
        case NODE_PANEL: {
            n = panel_create("panel", 0, 0, 200, 120, 0xFF252A33);
            break;
        }
        default:
            kfree(text_copy);
            return 0;
    }

    if (text_copy) kfree(text_copy);
    if (!n) return 0;

    uint32_t handle = n->id;
    gui_track_handle(handle);
    return handle;
}

/* --------------------------------------------------------------------------
 * Syscall 122 — canvas_add / node_add_child
 * -------------------------------------------------------------------------- */

uint64_t sys_gui_node_add_child(uint64_t parent_handle, uint64_t child_handle) {
    node_t *parent = gui_find_node((uint32_t)parent_handle);
    node_t *child  = gui_find_node((uint32_t)child_handle);
    if (!parent || !child) return (uint64_t)-1;

    /* If the child already has a parent, detach it first so we can move it. */
    if (child->parent) {
        node_remove_child(child->parent, child);
    }

    if (!node_add_child(parent, child)) return (uint64_t)-1;

    extern compositor_t *g_compositor;
    if (g_compositor) compositor_invalidate_full(g_compositor);
    return 0;
}

/* --------------------------------------------------------------------------
 * Syscall 123 — node_move
 * -------------------------------------------------------------------------- */

uint64_t sys_gui_node_move(uint64_t node_handle, uint64_t x, uint64_t y) {
    node_t *n = gui_find_node((uint32_t)node_handle);
    if (!n) return (uint64_t)-1;

    node_set_position(n, (int)x, (int)y);

    extern compositor_t *g_compositor;
    if (g_compositor) compositor_invalidate_full(g_compositor);
    return 0;
}

/* --------------------------------------------------------------------------
 * Syscall 124 — camera_zoom
 * -------------------------------------------------------------------------- */

uint64_t sys_gui_camera_zoom(uint64_t izoom) {
    extern compositor_t *g_compositor;
    if (!g_compositor || !g_compositor->camera) return (uint64_t)-1;

    /* sdk sends zoom*1000; camera wants fixed-point at CAMERA_ZOOM_SCALE (1024) */
    int zoom_fp = (int)izoom;
    /* scale 1000-based to 1024-based: zoom_fp * 1024 / 1000 */
    zoom_fp = (int)((int64_t)zoom_fp * CAMERA_ZOOM_SCALE / 1000);

    camera_t *cam = g_compositor->camera;
    camera_zoom_at(cam, zoom_fp, cam->screen_w / 2, cam->screen_h / 2);
    compositor_invalidate_full(g_compositor);
    return 0;
}

/* --------------------------------------------------------------------------
 * Syscall 125 — image_create
 * -------------------------------------------------------------------------- */

uint64_t sys_gui_image_create(uint64_t canvas_handle, uint64_t width,
                              uint64_t height, uint32_t *buffer) {
    node_t *canvas = gui_find_node((uint32_t)canvas_handle);
    if (!canvas) return 0;
    if (width == 0 || height == 0 || width > 2048 || height > 2048) return 0;

    size_t words = (size_t)width * (size_t)height;
    uint32_t *pixels = (uint32_t *)kmalloc(words * 4);
    if (!pixels) return 0;

    if (buffer) {
        if (gui_copy_pixels(pixels, buffer, words) < 0) {
            kfree(pixels);
            return 0;
        }
    } else {
        memset(pixels, 0, words * 4);
    }

    node_t *img = image_node_create("image", (int)width, (int)height, pixels);
    kfree(pixels); /* image_node_create copies into its own buffer */
    if (!img) return 0;

    /* Attach to the canvas window */
    if (!node_add_child(canvas, img)) {
        node_destroy(img);
        return 0;
    }

    uint32_t handle = img->id;
    gui_track_handle(handle);

    extern compositor_t *g_compositor;
    if (g_compositor) compositor_invalidate_full(g_compositor);
    return handle;
}

/* --------------------------------------------------------------------------
 * Syscall 126 — image_update
 * -------------------------------------------------------------------------- */

/* Copy pixels from a user/kernel buffer directly into an image node's buffer,
 * then mark it dirty.  Bounds-checked against the node's capacity. */
static int image_state_copy_safe(node_t *n, const uint32_t *buffer, int buffer_size) {
    extern int image_node_pixel_capacity(node_t *node);
    int capacity = image_node_pixel_capacity(n);
    if (capacity <= 0 || buffer_size <= 0) return -1;
    if (buffer_size > capacity) buffer_size = capacity;

    /* image_node stores its buffer in node->userdata (image_state_t) */
    typedef struct { uint32_t *buffer; int width; int height; } is_t;
    is_t *st = (is_t *)n->userdata;
    if (!st || !st->buffer) return -1;

    if (gui_copy_pixels(st->buffer, buffer, (size_t)buffer_size) < 0) return -1;
    node_mark_dirty(n, NODE_DIRTY_PAINT);
    return 0;
}

uint64_t sys_gui_image_update(uint64_t image_handle, uint32_t *buffer,
                              uint64_t buffer_size) {
    node_t *n = gui_find_node((uint32_t)image_handle);
    if (!n || n->type != NODE_IMAGE) return (uint64_t)-1;

    /* The image node already holds width*height pixels; the caller passes
     * buffer_size as the number of uint32.  We relay up to that many. */
    if (!buffer || buffer_size == 0) return (uint64_t)-1;

    image_state_copy_safe(n, buffer, (int)buffer_size);

    extern compositor_t *g_compositor;
    if (g_compositor) compositor_invalidate_full(g_compositor);
    return 0;
}

/* --------------------------------------------------------------------------
 * Cleanup — called on process exit
 * -------------------------------------------------------------------------- */

void sys_gui_cleanup_task(void) {
    gui_task_state_t *s = (current_task) ? current_task->gui : NULL;
    if (!s) return;

    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) { kfree(s); current_task->gui = NULL; return; }

    for (uint32_t i = 0; i < s->count; i++) {
        node_t *n = node_find_by_id(g_scene->root, s->handles[i]);
        if (n && n->parent) {
            node_remove_child(n->parent, n);
            node_destroy(n);
        }
    }

    kfree(s);
    current_task->gui = NULL;

    extern compositor_t *g_compositor;
    if (g_compositor) compositor_invalidate_full(g_compositor);
}
