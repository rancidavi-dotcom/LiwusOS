/*
 * gui/scene/node.h
 *
 * The universal unit of the LiwusOS Scene Graph.
 *
 * Everything visible on screen — windows, buttons, text labels, images,
 * the canvas background, overlays — is a node_t.
 *
 * Hierarchy:
 *
 *   canvas_root (node_t)
 *     └─ group (node_t)
 *          ├─ window (node_t, subtype NODE_WINDOW)
 *          │     └─ button (node_t, subtype NODE_BUTTON)
 *          └─ overlay (node_t)
 *
 * Coordinate spaces:
 *   - local_pos : position relative to the parent node
 *   - world_transform : cached product of all ancestor transforms + own
 *     (recomputed lazily via the DIRTY_TRANSFORM flag)
 *
 * Lifecycle:
 *   node_create() → node_add_child() → ... → node_destroy()
 *
 * Rendering:
 *   The compositor calls node_draw() recursively on the tree.
 *   Each concrete subtype fills node_t.vtable.draw with its own painter.
 *
 * Events:
 *   The compositor walks the tree Capture→Target→Bubble.
 *   node_t.vtable.on_event is the handler; return true to stop propagation.
 */
#ifndef GUI_NODE_H
#define GUI_NODE_H

#include <stdint.h>
#include <stdbool.h>
#include "../math/rect.h"
#include "../math/transform.h"
#include "../core/event_bus.h"

/* --------------------------------------------------------------------------
 * Node type identifiers — extend freely, never renumber.
 * -------------------------------------------------------------------------- */

typedef enum {
    NODE_GENERIC  = 0,
    NODE_CANVAS   = 1,
    NODE_GROUP    = 2,
    NODE_WINDOW   = 3,
    NODE_PANEL    = 4,
    NODE_BUTTON   = 5,
    NODE_LABEL    = 6,
    NODE_IMAGE    = 7,
    NODE_TERMINAL = 8,
    NODE_OVERLAY  = 9,
    NODE_DEBUG    = 10,
} node_type_t;

typedef enum {
    LAYOUT_ABSOLUTE = 0,
    LAYOUT_VBOX     = 1,
    LAYOUT_HBOX     = 2,
} layout_type_t;

typedef enum {
    ALIGN_START  = 0,
    ALIGN_CENTER = 1,
    ALIGN_END    = 2,
    ALIGN_STRETCH= 3,
} layout_align_t;

/* --------------------------------------------------------------------------
 * Forward declaration
 * -------------------------------------------------------------------------- */

typedef struct node node_t;
struct gui_renderer;   /* defined in render/renderer.h */

/* --------------------------------------------------------------------------
 * Virtual dispatch table (vtable) — one per node type.
 *
 * All function pointers are optional; the system checks for NULL before
 * calling.
 * -------------------------------------------------------------------------- */

typedef struct {
    /* Paint this node's content into the back-buffer via the renderer.
     * Called only when node is visible and not culled. */
    void  (*draw)(node_t *self, struct gui_renderer *r);

    /* Handle an event that targets or passes through this node.
     * Return true to stop propagation. */
    bool  (*on_event)(node_t *self, const gui_event_t *event);

    /* Called by the layout engine to position children.
     * Implementations should set child->local_bounds. */
    void  (*layout)(node_t *self);

    /* Called once when node_destroy() is invoked — free subtype data. */
    void  (*destroy)(node_t *self);
} node_vtable_t;

/* --------------------------------------------------------------------------
 * Dirty flags — bit-field
 * -------------------------------------------------------------------------- */

#define NODE_DIRTY_TRANSFORM  (1u << 0)  /* world_transform needs recompute  */
#define NODE_DIRTY_LAYOUT     (1u << 1)  /* layout pass needed               */
#define NODE_DIRTY_PAINT      (1u << 2)  /* content changed, repaint needed  */
#define NODE_DIRTY_ALL        (0x07u)

/* --------------------------------------------------------------------------
 * Core node_t structure
 * -------------------------------------------------------------------------- */

#define NODE_MAX_CHILDREN 64
#define NODE_NAME_LEN     32

struct node {
    /* Identity */
    uint32_t          id;           /* unique across the entire scene graph */
    node_type_t       type;
    char              name[NODE_NAME_LEN];

    /* --- Spatial data --------------------------------------------------- */

    /* Position and size in LOCAL (parent) coordinate space. */
    int               local_x, local_y;
    int               width, height;

    /* Cached transform from root to this node's origin.
     * Rebuilt when NODE_DIRTY_TRANSFORM is set. */
    gui_transform_t   world_transform;

    /* Bounding box in SCREEN space (after camera projection).
     * Set by compositor during traversal; used for dirty-rect culling. */
    gui_rect_t        screen_bounds;

    /* --- Hierarchy ------------------------------------------------------- */

    node_t           *parent;
    node_t           *children[NODE_MAX_CHILDREN];
    uint32_t          child_count;

    /* Doubly-linked sibling list for fast removal */
    node_t           *prev_sibling;
    node_t           *next_sibling;

    /* --- State ----------------------------------------------------------- */

    bool              visible;
    bool              interactive;  /* receives input events */
    uint32_t          dirty;        /* bit-mask of NODE_DIRTY_* */
    float             opacity;      /* 0.0 – 1.0 */
    int               z_order;      /* higher = drawn later (on top) */

    /* --- Layout ---------------------------------------------------------- */
    layout_type_t     layout_type;
    layout_align_t    layout_align;
    int               margin[4];    /* top, right, bottom, left */
    int               padding[4];   /* top, right, bottom, left */
    int               flex_weight;  /* 0 = absolute/fixed, >0 = flex */

    /* --- Virtual dispatch ------------------------------------------------ */
    const node_vtable_t *vtable;

    /* Subtype-specific payload — allocated by subtype constructors. */
    void             *userdata;
};

/* --------------------------------------------------------------------------
 * Scene-graph global state (singleton per system)
 * -------------------------------------------------------------------------- */

typedef struct {
    node_t    *root;          /* canvas root                        */
    uint32_t   next_id;       /* monotonically increasing node ID   */
    uint32_t   node_count;
} scene_graph_t;

extern scene_graph_t *g_scene;   /* set by canvas_create()          */

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/* Initialize the scene graph singleton. */
void scene_graph_init(void);
void scene_graph_destroy(void);

/* Allocate a blank node (all fields zeroed, id assigned). */
node_t *node_create(node_type_t type, const char *name);

/* Release node and all its descendants. Calls vtable->destroy if set. */
void node_destroy(node_t *node);

/* Parent/child management */
bool    node_add_child(node_t *parent, node_t *child);
void    node_remove_child(node_t *parent, node_t *child);
node_t *node_find_by_name(node_t *root, const char *name);
node_t *node_find_by_id(node_t *root, uint32_t id);
node_t *node_hit_test(node_t *root, int screen_x, int screen_y);

/* Transform helpers */
void node_set_position(node_t *node, int x, int y);
void node_set_size(node_t *node, int w, int h);
void node_mark_dirty(node_t *node, uint32_t flags);

/* Recompute world_transform for a node and all dirty descendants. */
void node_update_transforms(node_t *node, gui_transform_t parent_world);

/* Recursive draw — called by compositor each frame. */
void node_draw_recursive(node_t *node, struct gui_renderer *r);

#endif /* GUI_NODE_H */
