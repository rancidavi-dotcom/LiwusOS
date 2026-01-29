# Scene Graph — LiwusOS Node System

## 1. Objective and Responsibilities

The scene graph is the **structural backbone** of the LiwusOS GUI. It is a rooted tree of `node_t` objects that defines the spatial, visual, and behavioral organization of every element on screen. The scene graph is responsible for:

- **Hierarchical composition**: Every GUI element is a child of some parent node. The root is always the Canvas (`NODE_CANVAS`).
- **Coordinate system propagation**: Each node's local position is concatenated with its ancestors to compute a world transform.
- **Draw traversal**: Depth-first recursion over visible nodes, calling each node's vtable draw function.
- **Hit-testing**: Walk children in reverse z-order to find the frontmost interactive node under a screen point.
- **Dirty-flag management**: Nodes track which of transform/layout/paint need recomputation.
- **Lifetime management**: `node_create()`/`node_destroy()` handle allocation/free of nodes and their descendants.

**Node count is arbitrary** (limited only by kernel heap). There is no fixed maximum node count beyond the per-parent `NODE_MAX_CHILDREN=64`.

## 2. Problems Solved

- **No special structures for different UI elements**: A window, a button, a text label, and the canvas background are all `node_t`. There is no `window_t`, `widget_t`, or `control_t` type. Polymorphism comes from the vtable.
- **No separate coordinate bookkeeping**: Each node stores its position once (local to parent). World position is derived by walking up the tree. There is no duplicate coordinate storage.
- **No ad-hoc render ordering**: Z-order is implicit in the children array order. The last child draws on top. The compositor simply recurses in array order.
- **No separate input routing**: Hit-testing is a tree walk. There is no separate "window manager" that routes clicks — `node_hit_test()` returns the deepest visible, interactive node containing the point.

## 3. Architecture

### 3.1 The `node_t` Structure

```
struct node {
    ┌─ Identity ────────────────────────────────┐
    │  uint32_t id;           // unique per tree │
    │  node_type_t type;      // NODE_CANVAS etc │
    │  char name[32];         // e.g. "ok_btn"   │
    ├─ Spatial Data ─────────────────────────────┤
    │  int local_x, local_y;  // parent space    │
    │  int width, height;     // parent space    │
    │  gui_transform_t world_transform; // cached │
    │  gui_rect_t screen_bounds;   // cached     │
    ├─ Hierarchy ────────────────────────────────┤
    │  node_t *parent;                           │
    │  node_t *children[64];                     │
    │  uint32_t child_count;                     │
    │  node_t *prev_sibling;   // doubly-linked  │
    │  node_t *next_sibling;                     │
    ├─ State ────────────────────────────────────┤
    │  bool visible;                             │
    │  bool interactive;      // receives events │
    │  uint32_t dirty;        // DIRTY_* bits    │
    │  float opacity;         // 0.0–1.0         │
    │  int z_order;           // higher = on top │
    ├─ Layout ───────────────────────────────────┤
    │  layout_type_t layout_type;  // ABSOLUTE.. │
    │  layout_align_t layout_align;              │
    │  int margin[4], padding[4];                │
    │  int flex_weight;                          │
    ├─ Behavior ─────────────────────────────────┤
    │  const node_vtable_t *vtable;              │
    │  void *userdata;        // subtype payload │
    └────────────────────────────────────────────┘
};
```

**Size**: ~160 bytes per node (excluding userdata).

### 3.2 Node Types

```c
typedef enum {
    NODE_GENERIC  = 0,   // Custom/user-defined
    NODE_CANVAS   = 1,   // Root of the scene graph
    NODE_GROUP    = 2,   // Non-drawing container
    NODE_WINDOW   = 3,   // Window with title bar
    NODE_PANEL    = 4,   // Colored container rect
    NODE_BUTTON   = 5,   // Clickable button
    NODE_LABEL    = 6,   // Single-line text
    NODE_IMAGE    = 7,   // Bitmap/icon (reserved)
    NODE_TERMINAL = 8,   // Terminal emulator
    NODE_OVERLAY  = 9,   // Always-on-top HUD
    NODE_DEBUG    = 10,  // Debug visualization
} node_type_t;
```

New types can be added freely. They must never be renumbered (stored in saved state). The type informs the vtable assignment but is not used by the scene graph core — all polymorphism goes through the vtable.

### 3.3 Vtable

```c
typedef struct {
    void  (*draw)(node_t *self, struct gui_renderer *r);
    bool  (*on_event)(node_t *self, const gui_event_t *event);
    void  (*layout)(node_t *self);
    void  (*destroy)(node_t *self);
} node_vtable_t;
```

- **draw**: Paint the node's content into the renderer. Receives screen-space coordinates after camera projection. Must set `self->screen_bounds`.
- **on_event**: Handle events dispatched to this node, or events in the capture/bubble phase. Return `true` to stop propagation.
- **layout**: Custom layout override (called by layout engine if non-NULL).
- **destroy**: Free subtype-specific resources (the `userdata` pointer).

All function pointers are optional. The system checks for NULL before calling.

### 3.4 Dirty Flags

```c
#define NODE_DIRTY_TRANSFORM  (1u << 0)  // world_transform needs rebuild
#define NODE_DIRTY_LAYOUT     (1u << 1)  // children need repositioning
#define NODE_DIRTY_PAINT      (1u << 2)  // visual content changed
#define NODE_DIRTY_ALL        (0x07u)
```

- `NODE_DIRTY_TRANSFORM` is set by `node_set_position()` and propagates to **all descendants** (see `node_mark_dirty()`).
- `NODE_DIRTY_LAYOUT` is set by `node_set_size()` and when children are added/removed.
- `NODE_DIRTY_PAINT` is set by widget code when content changes (text, color, etc.).

The compositor clears dirty flags after processing each phase:
- Transform pass clears `DIRTY_TRANSFORM`
- Layout pass clears `DIRTY_LAYOUT`
- Draw pass clears `DIRTY_PAINT`

## 4. Lifecycle

### 4.1 Initialization

```
scene_graph_init()
  │
  └─ kmalloc(scene_graph_t)
     ├─ root = NULL
     ├─ next_id = 1
     └─ node_count = 0
```

The scene graph singleton `g_scene` is created once during `gui_init()`. The root node is created separately and assigned:

```c
g_scene->root = node_create(NODE_CANVAS, "canvas");
```

### 4.2 Node Creation

```
node_create(type, name)
  │
  ├─ kmalloc(node_t)
  ├─ memset(0)
  ├─ id = g_scene->next_id++
  ├─ type = given, visible = true, interactive = true
  ├─ opacity = 1.0, dirty = NODE_DIRTY_ALL
  ├─ world_transform = identity
  ├─ name = strncpy (max 31 chars)
  └─ return node
```

Nodes are created detached (no parent). They must be explicitly added to the tree.

### 4.3 Parent/Child Attachment

```
node_add_child(parent, child)
  │
  ├─ Check: child_count < NODE_MAX_CHILDREN (64)
  ├─ Check: child->parent == NULL (not already attached)
  ├─ Set: child->parent = parent
  ├─ Sibling list: link after last child
  ├─ Array: children[child_count++] = child
  └─ Mark child dirty: TRANSFORM | LAYOUT
```

```
         parent                    parent
           │                          │
     ┌─────┼─────┐               ┌───┼───┬───┐
     │     │     │               │   │   │   │
     A     B     C     + D  →    A   B   C   D
                       ↑         ↑   ↑   ↑   ↑
                  prev_sibling ──┼───┼───┘   │
                  next_sibling ──┼───┼───────┘
                                prev of D = C
                                next of D = NULL
```

### 4.4 Destruction

```
node_destroy(node)
  │
  ├─ For each child (depth-first):
  │    └─ node_destroy(child)
  ├─ Call vtable->destroy(node) — frees userdata
  ├─ g_scene->node_count--
  └─ kfree(node)
```

Children are destroyed before the parent (depth-first). The `vtable->destroy` handler must free any heap memory in `userdata`. After destruction, all pointers to the node are invalid.

### 4.5 Removal (without destruction)

```
node_remove_child(parent, child)
  │
  ├─ Find child in parent->children[]
  ├─ Unlink from sibling list (prev/next)
  ├─ Compact children array (shift left)
  ├─ child->parent = NULL
  ├─ child->prev_sibling = NULL
  └─ child->next_sibling = NULL
```

The removed node continues to exist in memory. It can be re-attached to a different parent.

### 4.6 Full Lifecycle Diagram

```
scene_graph_init()
    │
    ▼
node_create(NODE_CANVAS, "root") ────┐
    │                                  │
    ▼                                  │
node_add_child(root, child)            │
    │                                  │
    ▼                                  │
node_set_position / set_size           │
    │                                  │
    ├── node_mark_dirty() ─────────────┤
    │                                  │
    ▼                                  ▼
compositor_frame() loop:
  ├─ node_update_transforms()  ← clears DIRTY_TRANSFORM
  ├─ layout_engine_compute()   ← clears DIRTY_LAYOUT
  └─ node_draw_recursive()     ← clears DIRTY_PAINT
         │
         ▼
    node_remove_child(parent, child)
         │
         ├─ re-attach elsewhere
         └─ node_destroy(child)
              │
              └─ kfree(node)

scene_graph_destroy()
  │
  └─ node_destroy(root)
     └─ kfree(g_scene)
```

## 5. Coordinate System and Transform Chain

### 5.1 Three Coordinate Spaces

```
  ┌──────────────┐     local_to_world     ┌──────────────┐
  │  Local Space │ ════════════════════▶  │  World Space │
  │  (parent)    │   world_transform      │  (canvas)    │
  └──────────────┘                        └──────┬───────┘
                                                 │
                                        camera_world_to_screen()
                                                 │
                                                 ▼
                                        ┌──────────────┐
                                        │ Screen Space │
                                        │ (pixels)     │
                                        └──────────────┘
```

- **Local space**: Position relative to parent. Stored in `local_x`, `local_y`. Children of the same parent use the same local space.
- **World space**: Position relative to the canvas root. Computed by multiplying all ancestor local transforms: `world_transform = local_T ∘ parent->world_transform`.
- **Screen space**: World space projected through the camera. `screen_x = (world_x - cam_x) * zoom / 1024`.

### 5.2 Transform Representation

A `gui_transform_t` is a 3×3 affine matrix stored in 16.16 fixed-point:

```
| a  c  tx |     a,d  = scale (TRANSFORM_SCALE = 65536 = 1.0)
| b  d  ty |     b,c  = shear (unused in current widgets)
| 0  0   1 |     tx,ty = translation (pixels)
```

Currently, all widget transforms are pure translations (identity with `tx=local_x, ty=local_y`). Scale/shear are reserved for future zoom effects on individual nodes.

### 5.3 Transform Propagation

```
node_update_transforms(node, parent_world)
  │
  ├─ if NOT dirty: skip
  ├─ local = transform_translation(node->local_x, node->local_y)
  ├─ node->world_transform = concat(local, parent_world)
  ├─ node->dirty &= ~DIRTY_TRANSFORM
  │
  └─ For each child:
       └─ node_update_transforms(child, node->world_transform)
```

```
Example tree with transforms:

Canvas (world = I)
│
├─ Window A (local_x=100, local_y=50)
│  │  world = T(100,50) ∘ I = T(100,50)
│  │
│  └─ Button A1 (local_x=10, local_y=20)
│       world = T(10,20) ∘ T(100,50) = T(110,70)
│
└─ Window B (local_x=400, local_y=300)
     world = T(400,300) ∘ I = T(400,300)
```

### 5.4 World → Screen Projection

Each widget's `draw()` function converts world coordinates to screen space:

```c
void widget_draw(node_t *self, gui_renderer_t *r) {
    camera_t *cam = g_compositor->camera;

    // Origin of this node in world space
    gui_pointi_t world_origin = transform_apply(self->world_transform, 0, 0);

    // Project to screen
    int sx = camera_world_to_screen_x(cam, world_origin.x);
    int sy = camera_world_to_screen_y(cam, world_origin.y);
    int sw = camera_scale(cam, self->width);
    int sh = camera_scale(cam, self->height);

    self->screen_bounds = rect_make(sx, sy, sw, sh);
    renderer_fill_rect(r, self->screen_bounds, color);
}
```

## 6. Draw Traversal

```
node_draw_recursive(node, renderer)
  │
  ├─ if !node || !node->visible: return
  │
  ├─ if node->vtable && node->vtable->draw:
  │    └─ node->vtable->draw(node, renderer)
  │
  ├─ For each child (in array order = z-order):
  │    └─ node_draw_recursive(child, renderer)
  │
  └─ node->dirty &= ~DIRTY_PAINT
```

```
Draw order for a typical scene:

1. Canvas (NODE_CANVAS)      ← background dot grid (drawn by compositor,
  │                             not by node_draw_recursive)
  │
  ├─ Window A (NODE_WINDOW)  ← title bar + border
  │   ├─ Panel (NODE_PANEL)  ← background rect
  │   ├─ Label (NODE_LABEL)  ← text glyphs
  │   └─ Button (NODE_BUTTON)← rect + centered text
  │
  └─ Window B (NODE_WINDOW)  ← drawn after A (on top)
      └─ ...

Result: children draw on top of parents,
        later siblings draw on top of earlier ones.
```

## 7. Hit-Testing

```
node_hit_test(root, screen_x, screen_y)
  │
  ├─ if !root || !root->visible: return NULL
  │
  ├─ For each child in REVERSE order (topmost first):
  │    └─ hit = node_hit_test(child, screen_x, screen_y)
  │       if hit: return hit (deepest wins)
  │
  ├─ if root->interactive AND
  │    rect_contains_point(root->screen_bounds, screen_x, screen_y):
  │    └─ return root
  │
  └─ return NULL
```

Key properties:
- **Reverse child iteration**: Later children (higher z-order) are tested first.
- **Depth-first**: The deepest descendant that contains the point wins.
- **Interactive filter**: Only nodes with `interactive=true` are returned.
- **Canvas exclusion**: Callers typically exclude `NODE_CANVAS` from hit results.

```
Hit-test example for click at (150, 80):

Canvas (screen_bounds = full screen)
└─ Window A (screen_bounds = 100,50 to 400,250)  ← contains (150,80)?
   ├─ Panel A1 (screen_bounds = 110,70 to 370,200) ← contains (150,80)? YES
   │   └─ Button A1a (screen_bounds = 130,90 to 210,126) ← contains (150,80)? NO
   └─ Label A2 (screen_bounds = 110,210 to 270,226) ← NO

Result: Panel A1 is the deepest node containing the point.
```

## 8. APIs

### 8.1 Public API

```c
// System lifecycle
void scene_graph_init(void);
void scene_graph_destroy(void);

// Node lifecycle
node_t *node_create(node_type_t type, const char *name);
void node_destroy(node_t *node);

// Hierarchy
bool    node_add_child(node_t *parent, node_t *child);
void    node_remove_child(node_t *parent, node_t *child);
node_t *node_find_by_name(node_t *root, const char *name);
node_t *node_find_by_id(node_t *root, uint32_t id);
node_t *node_hit_test(node_t *root, int screen_x, int screen_y);

// Spatial
void node_set_position(node_t *node, int x, int y);
void node_set_size(node_t *node, int w, int h);
void node_mark_dirty(node_t *node, uint32_t flags);

// Frame
void node_update_transforms(node_t *node, gui_transform_t parent_world);
void node_draw_recursive(node_t *node, struct gui_renderer *r);

// Globals
extern scene_graph_t *g_scene;
```

### 8.2 Node Vtable

```c
typedef struct {
    void  (*draw)(node_t *self, struct gui_renderer *r);
    bool  (*on_event)(node_t *self, const gui_event_t *event);
    void  (*layout)(node_t *self);
    void  (*destroy)(node_t *self);
} node_vtable_t;
```

### 8.3 Scene Graph Singletons

```c
typedef struct {
    node_t    *root;        // canvas root
    uint32_t   next_id;     // monotonically increasing node ID
    uint32_t   node_count;  // current live node count
} scene_graph_t;
```

## 9. Dependencies

| File | Depends On |
|---|---|
| `node.h` | `rect.h`, `transform.h`, `event_bus.h` |
| `node.c` | `node.h`, `kheap.h`, `string.h` |
| `camera.h` | `rect.h` |
| `transform.h` | `rect.h` |

The scene graph module depends on:
- **kheap**: `kmalloc()` / `kfree()` for node allocation
- **rect**: AABB types and predicates for hit-testing, screen boundaries
- **transform**: 3×3 affine matrix for the coordinate chain
- **event_bus**: Event type definitions used in `on_event` vtable

The scene graph does NOT depend on: renderer, compositor, input_manager, or any widget implementation. Widgets depend on the scene graph.

## 10. Limitations and Trade-offs

| Limitation | Rationale |
|---|---|
| `NODE_MAX_CHILDREN=64` | Fixed array avoids linked-list overhead; 64 is enough for any container |
| No rotation in transforms | `transform_concat` supports it; not used by widgets to keep math simple |
| `world_transform` is 24 bytes per node | Needed for every frame; caching avoids tree walk each frame |
| `screen_bounds` is computed per-frame | Necessary because camera changes every frame; widgets compute it in draw |
| No instancing | Each node is a unique allocation; no shared geometry |
| `float opacity` | Used in animation engine; clang generates soft-float for `-mno-sse` |

## 11. Performance and Memory

| Metric | Value |
|---|---|
| `sizeof(node_t)` | ~160 bytes |
| Userdata typical | 32–128 bytes per widget |
| Transform update | O(N) where N = dirty nodes |
| Draw traversal | O(N) where N = visible nodes |
| Hit-test | O(depth × children) worst case |
| Node creation | 1× `kmalloc(160)` + 1× userdata alloc |

## 12. Future Extensions

- **Node pooling**: Pre-allocate a slab of nodes to avoid per-frame allocation.
- **Dirty subtree tracking**: Track dirty status at each node to skip clean subtrees during transform/layout passes.
- **Spatial hash for hit-testing**: Accelerate hit-test on scenes with hundreds of nodes.
- **Instance sharing**: Allow multiple nodes to share a single `world_transform` source.
- **Serialization**: Save/restore scene graph state to disk (persistent canvas layout).

## 13. Usage Examples

### Creating a widget hierarchy
```c
node_t *root = node_create(NODE_CANVAS, "canvas_root");
g_scene->root = root;

node_t *win = window_node_create("my_win", 200, 150, 320, 240, "Example");
node_t *btn = button_create("close_btn", 0, 0, 80, 28, "Close");
node_t *lbl = label_create("status_lbl", 0, 0, "Ready", 0xFFF8FAFC);

win->layout_type = LAYOUT_VBOX;
win->padding[0] = 30;  // title bar
win->padding[1] = 10;
win->padding[2] = 10;
win->padding[3] = 10;

lbl->layout_align = ALIGN_CENTER;
btn->layout_align = ALIGN_CENTER;
btn->margin[2] = 8;  // bottom margin

node_add_child(win, lbl);
node_add_child(win, btn);
node_add_child(root, win);

layout_engine_compute(win);
```

### Finding a node
```c
node_t *btn = node_find_by_name(g_scene->root, "close_btn");
if (btn) node_set_position(btn, 10, 10);
```

### Hit-testing (called by FocusManager)
```c
node_t *hit = node_hit_test(g_scene->root, mouse_x, mouse_y);
if (hit && hit->type != NODE_CANVAS) {
    focus_manager_set_focus(fm, hit);
}
```

### Custom draw vtable
```c
static void custom_draw(node_t *self, gui_renderer_t *r) {
    camera_t *cam = g_compositor->camera;
    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int sx = camera_world_to_screen_x(cam, pt.x);
    int sy = camera_world_to_screen_y(cam, pt.y);
    self->screen_bounds = rect_make(sx, sy,
        camera_scale(cam, self->width),
        camera_scale(cam, self->height));
    renderer_fill_rect(r, self->screen_bounds, 0xFF475569);
    renderer_draw_rect(r, self->screen_bounds, 0xFF94A3B8, 2);
}

static const node_vtable_t custom_vtable = {
    .draw = custom_draw,
    .on_event = NULL,
    .layout = NULL,
    .destroy = NULL,
};
```

---

*Document v1.0 — reflects `src/kernel/gui/scene/node.{h,c}` as of LiwusOS build.*
