# API Reference — LiwusOS GUI Subsystem

## Objective

Provide a complete, authoritative reference for every function, type, constant, and syscall in the LiwusOS GUI subsystem, covering kernel-internal modules (scene graph, camera, renderer, compositor, input, event bus, theme, animation, focus, window, asset management, layout) and the user-space syscall interface.

---

## Problems Solved

- Single source of truth for the 60+ functions across 15+ modules
- Documents the transition from the legacy LGX framebuffer API (syscalls 10–13 via `int $0x80`) to the modern Scene Graph SDK (syscalls 120–124 via `syscall` instruction)
- Clarifies ownership, threading, and calling context for all APIs

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   gui_main.c / gui_init()               │
│  (orchestrates all module init in dependency order)     │
└────┬───────┬───────┬───────┬───────┬───────┬───────┬───┘
     │       │       │       │       │       │       │
     ▼       ▼       ▼       ▼       ▼       ▼       ▼
  Scene   Camera  Renderer EventBus Input  Theme  Anim
  Graph                    Layout   Mgr   Engine Engine
                              │
                              ▼
                         Compositor
                              │
                              ▼
                       gui_compositor_task()
                    (kernel task, infinite loop)
```

---

## Module: Scene Graph

### Types

```c
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
    ALIGN_STRETCH = 3,
} layout_align_t;
```

### node_t Structure

```c
#define NODE_MAX_CHILDREN 64
#define NODE_NAME_LEN     32

struct node {
    uint32_t          id;             // unique across scene graph
    node_type_t       type;
    char              name[NODE_NAME_LEN];
    int               local_x, local_y;
    int               width, height;
    gui_transform_t   world_transform;
    gui_rect_t        screen_bounds;
    node_t           *parent;
    node_t           *children[NODE_MAX_CHILDREN];
    uint32_t          child_count;
    node_t           *prev_sibling, *next_sibling;
    bool              visible;
    bool              interactive;
    uint32_t          dirty;          /* NODE_DIRTY_* bits */
    float             opacity;        /* 0.0–1.0 */
    int               z_order;
    layout_type_t     layout_type;
    layout_align_t    layout_align;
    int               margin[4];      /* top, right, bottom, left */
    int               padding[4];     /* top, right, bottom, left */
    int               flex_weight;
    const node_vtable_t *vtable;
    void             *userdata;
};
```

### Dirty Flags

```c
#define NODE_DIRTY_TRANSFORM  (1u << 0)
#define NODE_DIRTY_LAYOUT     (1u << 1)
#define NODE_DIRTY_PAINT      (1u << 2)
#define NODE_DIRTY_ALL        (0x07u)
```

### scene_graph_t

```c
typedef struct {
    node_t    *root;          /* canvas root */
    uint32_t   next_id;       /* monotonically increasing node ID */
    uint32_t   node_count;
} scene_graph_t;
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `scene_graph_init` | `void scene_graph_init(void)` | Allocates the global `g_scene` singleton, sets `next_id=1`, `node_count=0` |
| `scene_graph_destroy` | `void scene_graph_destroy(void)` | Recursively destroys `g_scene->root`, frees the singleton, sets `g_scene=NULL` |
| `node_create` | `node_t *node_create(node_type_t type, const char *name)` | Allocates a zeroed node on the kernel heap, assigns `id` from `g_scene->next_id++`, sets `visible=true`, `interactive=true`, `opacity=1.0f`, `dirty=NODE_DIRTY_ALL`, `world_transform=identity`. Copies `name` (max 31 chars). Returns NULL on OOM. |
| `node_destroy` | `void node_destroy(node_t *node)` | Depth-first destroys all children, calls `vtable->destroy`, decrements `node_count`, calls `kfree(node)` |
| `node_add_child` | `bool node_add_child(node_t *parent, node_t *child)` | Appends `child` to `parent->children[]`, links siblings, marks `child` dirty for TRANSFORM\|LAYOUT. Returns false if `parent` full (`>=64`) or `child` already has a parent. |
| `node_remove_child` | `void node_remove_child(node_t *parent, node_t *child)` | Patches sibling list, compacts children array, clears `child->parent` |
| `node_find_by_name` | `node_t *node_find_by_name(node_t *root, const char *name)` | Depth-first search by `name` |
| `node_find_by_id` | `node_t *node_find_by_id(node_t *root, uint32_t id)` | Depth-first search by `id` |
| `node_hit_test` | `node_t *node_hit_test(node_t *root, int screen_x, int screen_y)` | Reverse-order child iteration (topmost first), returns deepest `interactive` node whose `screen_bounds` contains the point |
| `node_set_position` | `void node_set_position(node_t *node, int x, int y)` | Sets `local_x`,`local_y`, marks `NODE_DIRTY_TRANSFORM` (no-op if unchanged) |
| `node_set_size` | `void node_set_size(node_t *node, int w, int h)` | Sets `width`,`height`, marks `NODE_DIRTY_LAYOUT\|NODE_DIRTY_PAINT` (no-op if unchanged) |
| `node_mark_dirty` | `void node_mark_dirty(node_t *node, uint32_t flags)` | ORs flags into `node->dirty`; if `NODE_DIRTY_TRANSFORM`, propagates to all children recursively |
| `node_update_transforms` | `void node_update_transforms(node_t *node, gui_transform_t parent_world)` | If node dirty, computes `world_transform = translate(local) ∘ parent_world`, clears dirty bit, recurses |
| `node_draw_recursive` | `void node_draw_recursive(node_t *node, struct gui_renderer *r)` | If visible, calls `vtable->draw`, recurses children in ascending order, clears `NODE_DIRTY_PAINT` |

### node_vtable_t

```c
typedef struct {
    void  (*draw)(node_t *self, struct gui_renderer *r);
    bool  (*on_event)(node_t *self, const gui_event_t *event);
    void  (*layout)(node_t *self);
    void  (*destroy)(node_t *self);
} node_vtable_t;
```

All fn pointers optional (checked for NULL before call). `on_event` returns `true` to stop propagation.

### Dependencies

- Depends on: `rect.h`, `transform.h`, `event_bus.h`, `kheap.h`, `string.h`
- No dependency on renderer, camera, compositor, or widgets

---

## Module: Camera

### Types

```c
#define CAMERA_ZOOM_SCALE  1024   /* 1.0 = 1024 */
#define CAMERA_POS_SCALE   256    /* 1 pixel = 256 */
#define CAMERA_ZOOM_MIN_FP (CAMERA_ZOOM_SCALE / 10)  /* 0.1× */
#define CAMERA_ZOOM_MAX_FP (CAMERA_ZOOM_SCALE * 8)   /* 8.0× */
#define CAMERA_ZOOM_DEF_FP (CAMERA_ZOOM_SCALE)       /* 1.0× */
#define CAMERA_FRICTION_NUM  870
#define CAMERA_FRICTION_DEN  1024
#define CAMERA_ZOOM_STEP_FP  82

typedef struct {
    int32_t  pos_x_fp;      /* world x << 8 */
    int32_t  pos_y_fp;      /* world y << 8 */
    int32_t  zoom_fp;       /* zoom × 1024 */
    int32_t  vel_x_fp;      /* velocity (same scale as pos) */
    int32_t  vel_y_fp;
    int      screen_w;
    int      screen_h;
    bool     dirty;
} camera_t;
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `camera_create` | `camera_t *camera_create(int screen_w, int screen_h)` | Allocates camera, sets `zoom_fp=CAMERA_ZOOM_DEF_FP`, `dirty=true` |
| `camera_destroy` | `void camera_destroy(camera_t *cam)` | Calls `kfree(cam)` |
| `camera_pan` | `void camera_pan(camera_t *cam, int dx, int dy)` | Adds `dx*256` to pos_x_fp, same for y; sets velocity for inertia |
| `camera_center_on` | `void camera_center_on(camera_t *cam, int world_x, int world_y)` | Computes `pos_fp` so `world_x,world_y` maps to screen center |
| `camera_zoom_at` | `void camera_zoom_at(camera_t *cam, int new_zoom_fp, int pivot_sx, int pivot_sy)` | Clamps zoom to [0.1×,8×], finds world point under `pivot_sx,pivot_sy` before zoom, repositions so same world point maps to same screen pixel after zoom |
| `camera_reset` | `void camera_reset(camera_t *cam)` | Zeroes pos, sets zoom to 1.0×, zeroes velocity |
| `camera_fit` | `void camera_fit(camera_t *cam, const gui_rect_t *rects, uint32_t count)` | Computes bounding box of all rects, sets zoom to fit within screen, centers |
| `camera_update` | `void camera_update(camera_t *cam)` | Decays velocity by 870/1024 per frame, applies to position; stops if velocity < 0.25 world px |

### Coordinate Conversion (inline helpers)

| Function | Description |
|---|---|
| `camera_world_to_screen_x(c, wx)` | `(wx*256 - pos_x_fp) * zoom_fp / (1024*256)` |
| `camera_world_to_screen_y(c, wy)` | Same for Y |
| `camera_screen_to_world_x(c, sx)` | `(sx*1024*256 / zoom_fp + pos_x_fp) / 256` |
| `camera_screen_to_world_y(c, sy)` | Same for Y |
| `camera_world_rect_to_screen(c, wr)` | Transforms all 4 corners, returns AABB |
| `camera_viewport_in_world(c)` | Returns the visible world rect |
| `camera_scale(c, world_dim)` | `world_dim * zoom_fp / 1024` |

### Dependencies

- Depends on: `rect.h`, `kheap.h`

---

## Module: Renderer

### Types

```c
typedef struct {
    const uint8_t *bitmap;  /* 16 rows × 1 byte each */
    int cell_w, cell_h;
} glyph_t;
```

### renderer_ops_t (Backend Vtable)

```c
typedef struct {
    void (*fill_rect)(gui_renderer_t *r, gui_rect_t rect, uint32_t color);
    void (*draw_rect)(gui_renderer_t *r, gui_rect_t rect, uint32_t color, int thickness);
    void (*blit)(gui_renderer_t *r, int dest_x, int dest_y, const uint32_t *src, int src_w, int src_h, int src_pitch, int src_x, int src_y, int copy_w, int copy_h);
    void (*blit_scaled)(gui_renderer_t *r, int dest_x, int dest_y, const uint32_t *src, int src_w, int src_h, float scale_x, float scale_y);
    void (*draw_glyph)(gui_renderer_t *r, int x, int y, uint32_t fg, uint32_t bg, const glyph_t *g);
    void (*set_clip)(gui_renderer_t *r, gui_rect_t clip);
    void (*set_opacity)(gui_renderer_t *r, float opacity);
    void (*present)(gui_renderer_t *r);
    void (*destroy)(gui_renderer_t *r);
} renderer_ops_t;
```

### gui_renderer_t

```c
struct gui_renderer {
    const renderer_ops_t *ops;
    void                 *backend;   /* backend-private state */
    gui_rect_t            clip;
    float                 opacity;
    int                   screen_w;
    int                   screen_h;
};
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `renderer_create` | `gui_renderer_t *renderer_create(const renderer_ops_t *ops, void *backend_state, int screen_w, int screen_h)` | Allocates renderer object, sets clip to full screen |
| `renderer_destroy` | `void renderer_destroy(gui_renderer_t *r)` | Calls `ops->destroy(r->backend)`, frees renderer |
| `renderer_fill_rect` | `void renderer_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color)` | Fills solid rect. Color 0xAARRGGBB. Null-safe dispatch to ops. |
| `renderer_draw_rect` | `void renderer_draw_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color, int thickness)` | Draws 1px border |
| `renderer_blit` | `void renderer_blit(gui_renderer_t *r, int dest_x, int dest_y, const uint32_t *src, int src_w, int src_h, int src_pitch, int src_x, int src_y, int copy_w, int copy_h)` | Alpha-blend blit |
| `renderer_draw_glyph` | `void renderer_draw_glyph(gui_renderer_t *r, int x, int y, uint32_t fg, uint32_t bg, const glyph_t *g)` | Bitmap font glyph |
| `renderer_set_clip` | `void renderer_set_clip(gui_renderer_t *r, gui_rect_t clip)` | Sets `r->clip` and `ops->set_clip`. Pass `rect_zero()` to clear. |
| `renderer_present` | `void renderer_present(gui_renderer_t *r)` | Flips back-buffer to screen |

### Framebuffer Backend (fb_renderer)

| Function | Signature | Description |
|---|---|---|
| `fb_renderer_create` | `gui_renderer_t *fb_renderer_create(void)` | Reads `vga_fb_addr`, `vga_fb_width`, `vga_fb_height`, `vga_fb_pitch` globals; allocates back-buffer (kmalloc); returns renderer with `fb_ops` |
| `fb_renderer_backbuf` | `uint32_t *fb_renderer_backbuf(gui_renderer_t *r)` | Returns direct pointer to the software back-buffer |

### fb_ops Implementations

- `fb_fill_rect`: Clips to screen and user clip rect, alpha-blends per pixel if `color.a < 0xFF`
- `fb_draw_rect`: Draws 4 filled strips for top/bottom/left/right borders
- `fb_blit`: Clips dest rect, alpha-blends each pixel; supports source pitch (not necessarily == width)
- `fb_blit_scaled`: Nearest-neighbour scaling
- `fb_draw_glyph`: Bitmask font, 1 byte per row, 8px wide
- `fb_set_clip`: Stores in `r->clip`
- `fb_present`: memcpy from back-buffer to VRAM (`vga_fb_addr`)
- `fb_destroy`: Frees back-buffer and fb_state

### Dependencies

- Depends on: `rect.h`, `kheap.h`, VGA globals (vga.c)

---

## Module: Compositor

### Types

```c
#define COMPOSITOR_MAX_DIRTY_RECTS 64

typedef enum {
    CURSOR_ARROW    = 0,
    CURSOR_HAND     = 1,
    CURSOR_IBEAM    = 2,
    CURSOR_RESIZE_NS = 3,
    CURSOR_RESIZE_EW = 4,
} gui_cursor_t;

typedef struct {
    gui_renderer_t  *renderer;
    camera_t        *camera;
    gui_event_bus_t *bus;
    input_manager_t *input;
    node_t          *scene_root;
    gui_rect_t       dirty_rects[COMPOSITOR_MAX_DIRTY_RECTS];
    uint32_t         dirty_count;
    bool             full_redraw;
    int              cursor_x, cursor_y;
    bool             cursor_saved;
    uint32_t         cursor_save_buf[16*16];
    gui_cursor_t     cursor_type;
    uint64_t         frame_number;
} compositor_t;
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `compositor_create` | `compositor_t *compositor_create(gui_renderer_t *renderer, camera_t *camera, gui_event_bus_t *bus, input_manager_t *input, node_t *scene_root)` | Allocates compositor, sets `full_redraw=true`, cursor at (-1,-1), sets `g_compositor` global |
| `compositor_destroy` | `void compositor_destroy(compositor_t *c)` | Clears `g_compositor`, calls `kfree(c)` |
| `compositor_frame` | `void compositor_frame(compositor_t *c)` | Main per-frame loop: poll input, dispatch events, camera inertia, animation tick, transform pass, restore cursor area, draw background (dot grid on slate-900), draw all nodes, draw cursor, `renderer_present`, increment `frame_number`, `switch_task()` |
| `compositor_invalidate` | `void compositor_invalidate(compositor_t *c, const gui_rect_t *rect)` | Sets `full_redraw=true` (dirty-rect tracking reserved for Phase 4) |
| `compositor_invalidate_full` | `void compositor_invalidate_full(compositor_t *c)` | Sets `full_redraw=true` |
| `compositor_set_cursor` | `void compositor_set_cursor(compositor_t *c, gui_cursor_t type)` | Changes cursor sprite type |

### Global

```c
extern compositor_t *g_compositor;  /* set by compositor_create, cleared by destroy */
```

### Cursor Sprites

Hardcoded 16×16 1-bit bitmaps for arrow and hand. Compositor saves pixels under cursor, then restores before repaint each frame, to avoid ghosting.

### Dependencies

- Depends on: `node.h`, `camera.h`, `renderer.h`, `input_manager.h`, `event_bus.h`, `fb_renderer.h`, `theme_engine.h`, `animation_engine.h`, `task.h`

---

## Module: Event Bus

### Types

```c
typedef enum {
    GUI_EVENT_NONE        = 0,
    GUI_EVENT_MOUSE_MOVE  = 1,
    GUI_EVENT_MOUSE_DOWN  = 2,
    GUI_EVENT_MOUSE_UP    = 3,
    GUI_EVENT_MOUSE_SCROLL = 4,
    GUI_EVENT_KEY_DOWN    = 5,
    GUI_EVENT_KEY_UP      = 6,
    GUI_EVENT_KEY_CHAR    = 7,
    GUI_EVENT_MOUSE_ENTER = 8,
    GUI_EVENT_MOUSE_LEAVE = 9,
    GUI_EVENT_WIN_FOCUS   = 10,
    GUI_EVENT_WIN_BLUR    = 11,
    GUI_EVENT_WIN_CLOSE   = 12,
    GUI_EVENT_WIN_MOVE    = 13,
    GUI_EVENT_WIN_RESIZE  = 14,
    GUI_EVENT_CANVAS_PAN  = 20,
    GUI_EVENT_CANVAS_ZOOM = 21,
    GUI_EVENT_NODE_DIRTY  = 30,
    GUI_EVENT_NODE_ADDED  = 31,
    GUI_EVENT_NODE_REMOVED = 32,
    GUI_EVENT_FRAME_BEGIN = 40,
    GUI_EVENT_FRAME_END   = 41,
} gui_event_type_t;

typedef enum {
    GUI_PRIORITY_CRITICAL = 0,
    GUI_PRIORITY_HIGH     = 1,
    GUI_PRIORITY_NORMAL   = 2,
    GUI_PRIORITY_LOW      = 3,
} gui_event_priority_t;

typedef struct {
    int x, y;      /* screen coordinates */
    int dx, dy;    /* delta (move/scroll) */
    uint8_t button; /* 0=none, 1=left, 2=right, 3=middle */
} gui_mouse_payload_t;

typedef struct {
    uint8_t  scancode;
    uint32_t keycode;
    uint32_t unicode;
    uint8_t  modifiers; /* bit0=Shift bit1=Ctrl bit2=Alt bit3=Super */
} gui_key_payload_t;

typedef struct {
    uint64_t a, b, c, d;
} gui_generic_payload_t;

typedef struct {
    gui_event_type_t     type;
    gui_event_priority_t priority;
    uint32_t            target_id;
    bool                propagating;
    union {
        gui_mouse_payload_t   mouse;
        gui_key_payload_t       key;
        gui_generic_payload_t    generic;
    };
} gui_event_t;
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `event_bus_create` | `gui_event_bus_t *event_bus_create(void)` | Allocates bus, sets `next_sub_id=1` |
| `event_bus_destroy` | `void event_bus_destroy(gui_event_bus_t *bus)` | Calls `kfree(bus)` |
| `event_bus_subscribe` | `gui_subscription_id_t event_bus_subscribe(gui_event_bus_t *bus, gui_event_type_t type, gui_event_handler_t handler, void *userdata)` | Registers handler for `type` (or `GUI_EVENT_NONE` for all). Returns subscription ID (0 on failure). Max 64 subscribers. |
| `event_bus_unsubscribe` | `void event_bus_unsubscribe(gui_event_bus_t *bus, gui_subscription_id_t id)` | Marks slot inactive |
| `event_bus_post` | `bool event_bus_post(gui_event_bus_t *bus, const gui_event_t *event)` | Writes to ring buffer. Returns false on overflow (capacity 256). Safe from IRQ context. |
| `event_bus_dispatch` | `uint32_t event_bus_dispatch(gui_event_bus_t *bus)` | Drains ring to local snapshot, insertion-sorts by priority, dispatches to matching subscribers. Returns count. |
| `event_stop_propagation` | `void event_stop_propagation(gui_event_bus_t *bus)` | Sets `stop_propagation=true`; valid only inside handler callback |

### Convenience Posting Helpers (inline)

| Function | Description |
|---|---|
| `event_post_mouse_move(bus, x, y, dx, dy)` | Posts `GUI_EVENT_MOUSE_MOVE` at HIGH priority |
| `event_post_mouse_button(bus, x, y, button, pressed)` | Posts `GUI_EVENT_MOUSE_DOWN` or `GUI_EVENT_MOUSE_UP` |
| `event_post_key(bus, scancode, pressed)` | Posts `GUI_EVENT_KEY_DOWN` or `GUI_EVENT_KEY_UP` |
| `event_post_node_dirty(bus, node_id)` | Posts `GUI_EVENT_NODE_DIRTY` at NORMAL priority |

### Dependencies

- Depends on: `kheap.h`, `string.h`
- Zero dependency on node, camera, or renderer

---

## Module: Input Manager

### Types

```c
#define MOD_SHIFT  (1u << 0)
#define MOD_CTRL   (1u << 1)
#define MOD_ALT    (1u << 2)
#define MOD_SUPER  (1u << 3)
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `input_manager_create` | `input_manager_t *input_manager_create(gui_event_bus_t *bus)` | Allocates input manager, stores bus reference |
| `input_manager_destroy` | `void input_manager_destroy(input_manager_t *im)` | Calls `kfree(im)` |
| `input_manager_poll` | `void input_manager_poll(input_manager_t *im)` | Reads `get_mouse_x/y()`, `is_left_clicked/right_clicked()`, `keyboard_is_pressed()`; diffs against previous frame; posts `GUI_EVENT_MOUSE_MOVE`, `GUI_EVENT_MOUSE_DOWN/UP`, `GUI_EVENT_KEY_DOWN/UP` to bus. Treats Left-Ctrl as additional left-click. |
| `input_mouse_x` | `int input_mouse_x(const input_manager_t *im)` | Returns current mouse X (screen space) |
| `input_mouse_y` | `int input_mouse_y(const input_manager_t *im)` | Returns current mouse Y |
| `input_mouse_button` | `bool input_mouse_button(const input_manager_t *im, uint8_t button)` | Returns whether button (1=L,2=R,3=M) is currently held |
| `input_key_held` | `bool input_key_held(const input_manager_t *im, uint8_t scancode)` | Returns whether scancode is currently pressed |
| `input_modifiers` | `uint8_t input_modifiers(const input_manager_t *im)` | Returns modifier bitmask |

### Dependencies

- Depends on: `event_bus.h`, `camera.h`, `mouse.h` (kernel), `keyboard.h` (kernel)

---

## Module: Tool System

### Types

```c
typedef struct {
    const char *name;
    void (*on_activate)(tool_t *self);
    void (*on_deactivate)(tool_t *self);
    bool (*on_event)(tool_t *self, const gui_event_t *event);
    void (*destroy)(tool_t *self);
} tool_vtable_t;

struct tool {
    const tool_vtable_t *vtable;
    camera_t            *camera;
    node_t              *scene_root;
    void                *userdata;
    bool                 active;
};
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `tool_manager_create` | `tool_manager_t *tool_manager_create(gui_event_bus_t *bus, camera_t *cam, node_t *scene_root)` | Creates tool manager, subscribes to event bus |
| `tool_manager_destroy` | `void tool_manager_destroy(tool_manager_t *tm)` | Cleans up |
| `tool_manager_add_tool` | `void tool_manager_add_tool(tool_manager_t *tm, tool_t *tool)` | Registers tool (order determines priority) |
| `pan_tool_create` | `tool_t *pan_tool_create(camera_t *camera, node_t *scene_root)` | Right-mouse or Space+LMB panning; H=home, F=fit keyboard shortcuts |
| `select_tool_create` | `tool_t *select_tool_create(camera_t *camera, node_t *scene_root)` | LMB to select, click background to deselect |
| `select_tool_get_selection` | `node_t *select_tool_get_selection(tool_t *t)` | Returns currently selected node or NULL |
| `move_tool_create` | `tool_t *move_tool_create(camera_t *camera, node_t *scene_root, tool_t *select_tool)` | Drag-to-move selected node; activated via select tool |

### Dependencies

- Depends on: `event_bus.h`, `node.h`, `camera.h`

---

## Module: Theme Engine

### Types

```c
typedef enum {
    THEME_COLOR_BACKGROUND,         /* 0xFF0B1120 */
    THEME_COLOR_WINDOW_BG,          /* 0xCC1E293B */
    THEME_COLOR_WINDOW_TITLEBAR,    /* 0xEE0F172A */
    THEME_COLOR_WINDOW_BORDER,      /* 0xFF475569 */
    THEME_COLOR_TEXT_PRIMARY,       /* 0xFFF8FAFC */
    THEME_COLOR_TEXT_SECONDARY,     /* 0xFF94A3B8 */
    THEME_COLOR_BUTTON_BG,          /* 0xFF334155 */
    THEME_COLOR_BUTTON_BG_HOVER,    /* 0xFF475569 */
    THEME_COLOR_BUTTON_BG_PRESS,    /* 0xFF1E293B */
    THEME_COLOR_BUTTON_BORDER,      /* 0xFF64748B */
    THEME_COLOR_BUTTON_TEXT,        /* 0xFFFFFFFF */
    THEME_COLOR_CLOSE_BTN,          /* 0xFFEF4444 */
    THEME_COLOR_MAX
} theme_color_id_t;
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `theme_engine_init` | `void theme_engine_init(void)` | Fills `s_palette[]` with dark slate/indigo palette |
| `theme_engine_get_color` | `uint32_t theme_engine_get_color(theme_color_id_t id)` | Returns color from palette (bounds-checked, returns white on OOB) |
| `theme_engine_set_color` | `void theme_engine_set_color(theme_color_id_t id, uint32_t color)` | Sets palette entry |

### Dependencies

- Standalone (no internal deps)

---

## Module: Animation Engine

### Types

```c
typedef enum {
    ANIM_PROP_X,
    ANIM_PROP_Y,
    ANIM_PROP_WIDTH,
    ANIM_PROP_HEIGHT,
    ANIM_PROP_OPACITY_FP,
    ANIM_PROP_COLOR
} anim_prop_t;

#define MAX_ANIMATIONS 64

typedef struct {
    node_t     *target;
    anim_prop_t prop;
    uint32_t   *color_target;
    int         start_val;
    int         end_val;
    int         duration_frames;
    int         current_frame;
    bool        active;
} animation_t;
```

### Functions

| Function | Signature | Description |
|---|---|---|
| `animation_engine_init` | `void animation_engine_init(void)` | Zeroes `s_animations[64]` |
| `animation_engine_tick` | `bool animation_engine_tick(void)` | Advances all active animations by 1 frame, applies interpolation (linear), returns true if any animation remains active |
| `animation_start` | `void animation_start(node_t *node, anim_prop_t prop, void *custom_target, int start, int end, int frames)` | Creates/overwrites animation. For `ANIM_PROP_COLOR`, `custom_target` is a `uint32_t*` to color variable |
| `animation_cancel_all` | `void animation_cancel_all(node_t *node)` | Deactivates all animations for a node |

### Dependencies

- Depends on: `node.h`

---

## Module: Focus Manager

### Functions

| Function | Signature | Description |
|---|---|---|
| `focus_manager_create` | `focus_manager_t *focus_manager_create(gui_event_bus_t *bus, node_t *root)` | Subscribes to `GUI_EVENT_NONE`, intercepts mouse clicks for hit-testing and keyboard events for Tab traversal |
| `focus_manager_destroy` | `void focus_manager_destroy(focus_manager_t *fm)` | Unsubscribes, frees |
| `focus_manager_get_focus` | `node_t *focus_manager_get_focus(focus_manager_t *fm)` | Returns focused node or NULL |
| `focus_manager_set_focus` | `void focus_manager_set_focus(focus_manager_t *fm, node_t *node)` | Blurs old, focuses new, posts `GUI_EVENT_WIN_FOCUS/BLUR` |
| `focus_manager_focus_next` | `void focus_manager_focus_next(focus_manager_t *fm)` | Tab traversal (stub) |

### Dependencies

- Depends on: `node.h`, `event_bus.h`

---

## Module: Window Manager

### Functions

| Function | Signature | Description |
|---|---|---|
| `window_manager_create` | `window_manager_t *window_manager_create(gui_event_bus_t *bus, node_t *root)` | Subscribes to `GUI_EVENT_WIN_FOCUS`, brings focused node to front |
| `window_manager_destroy` | `void window_manager_destroy(window_manager_t *wm)` | Unsubscribes, frees |
| `window_manager_bring_to_front` | `void window_manager_bring_to_front(node_t *node)` | Moves node to end of parent's children array (highest z-order), marks parent dirty |

### Dependencies

- Depends on: `node.h`, `event_bus.h`

---

## Module: Asset Manager

### Functions

| Function | Signature | Description |
|---|---|---|
| `asset_manager_init` | `void asset_manager_init(void)` | Parses PSF font from `_binary_src_drivers_font_psf_start`, builds glyph_t[256] |
| `asset_manager_destroy` | `void asset_manager_destroy(void)` | Clears loaded flag |
| `asset_manager_get_font` | `const glyph_t *asset_manager_get_font(const char *name)` | Returns `s_default_font` (name param ignored; only one font for now) |

### Dependencies

- Depends on: `glyph_t` (renderer.h)

---

## Module: Layout Engine

### Functions

| Function | Signature | Description |
|---|---|---|
| `layout_engine_compute` | `void layout_engine_compute(node_t *node)` | If `vtable->layout` set, calls it. Then dispatches to `layout_vbox` or `layout_hbox` based on `node->layout_type`, or recurses for `LAYOUT_ABSOLUTE`. Clears `NODE_DIRTY_LAYOUT`. |

### Implementation

- `layout_vbox`: Pre-pass measures total flex weight and fixed child height; distributes remaining height proportionally; aligns per child's `layout_align`
- `layout_hbox`: Same logic for horizontal stacking
- Supports `margin[4]`, `padding[4]`, `flex_weight`, `layout_align`

### Dependencies

- Depends on: `node.h`

---

## Widget API

### Window Node

| Function | Signature | Description |
|---|---|---|
| `window_node_create` | `node_t *window_node_create(const char *name, int x, int y, int w, int h, const char *title)` | Creates NODE_WINDOW with title bar, close button (red dot), border. Close button sends `SIGKILL` to bound process. |
| `window_node_set_title` | `void window_node_set_title(node_t *win, const char *text)` | Updates title text |
| `window_node_set_pid` | `void window_node_set_pid(node_t *win, int pid)` | Binds a process ID to the window (for close-button kill) |

### Button

| Function | Signature | Description |
|---|---|---|
| `button_create` | `node_t *button_create(const char *name, int x, int y, int w, int h, const char *text)` | Creates NODE_BUTTON with hover/press colors, click callback support |
| `button_set_text` | `void button_set_text(node_t *button, const char *text)` | Updates text, marks dirty |
| `button_set_on_click` | `void button_set_on_click(node_t *button, button_click_cb_t cb, void *userdata)` | Sets click handler. Type: `void (*cb)(node_t*, void*)` |

### Label

| Function | Signature | Description |
|---|---|---|
| `label_create` | `node_t *label_create(const char *name, int x, int y, const char *text, uint32_t color)` | Creates NODE_LABEL for single-line text |
| `label_set_text` | `void label_set_text(node_t *label, const char *text)` | Updates text, auto-sizes width |
| `label_set_color` | `void label_set_color(node_t *label, uint32_t color)` | Updates text color |

### Panel

| Function | Signature | Description |
|---|---|---|
| `panel_create` | `node_t *panel_create(const char *name, int x, int y, int w, int h, uint32_t bg_color)` | Creates NODE_PANEL with optional background and border |
| `panel_set_bg_color` | `void panel_set_bg_color(node_t *panel, uint32_t color)` | Sets background color |
| `panel_set_border` | `void panel_set_border(node_t *panel, uint32_t color, int thickness)` | Sets border color and thickness |

---

## Syscall API (120–124)

Syscall convention for the GUI subsystem: `syscall` instruction with arguments in `rax` (number), `rdi` (a1), `rsi` (a2), `rdx` (a3). Return value in `rax`.

### Syscall 120 — canvas_create

```
Signature:  uint32_t sys_gui_canvas_create(int width, int height, const char* title)
Parameters:
  - rdi: width  (pixels)
  - rsi: height (pixels)
  - rdx: pointer to null-terminated title string (user-space)
Returns:
  - rax: Node ID (uint32_t handle) of the created window, or 0 on failure
Errors:
  - Returns 0 if g_compositor is NULL, scene_root is NULL, or OOM
Behavior:
  - Creates a NODE_WINDOW at screen center ((screen_w - width)/2, (screen_h - height)/2)
  - Binds window to the calling process (current_task->id) for close-button kill
  - Sets LAYOUT_ABSOLUTE, padding[0]=30 (title bar space)
  - Adds window as child of g_compositor->scene_root
```

### Syscall 121 — node_create

```
Signature:  uint32_t sys_gui_node_create(node_type_t type, const char* text)
Parameters:
  rdi: node_type_t (0-10, see NODE_* constants)
  rsi: pointer to text (only used for NODE_LABEL and NODE_BUTTON)
Returns:
  rax: Node ID (uint32_t handle), or 0 on failure
Errors:
  Returns 0 for unknown type or OOM
Behavior per type:
  NODE_LABEL  → calls label_create("app_lbl", 0, 0, text, 0xFFFFFFFF), size = strlen×8 × 16
  NODE_BUTTON → calls button_create("app_btn", 0, 0, 100, 30, text)
  NODE_PANEL  → calls panel_create("app_pnl", 0, 0, 100, 100, 0x88000000)
```

### Syscall 122 — canvas_add / node_add_child

```
Signature:  int sys_gui_canvas_add(uint32_t parent_id, uint32_t child_id)
Parameters:
  rdi: parent node ID
  rsi: child node ID
Returns:
  rax: 0 on success, -1 on failure
Errors:
  -1 if g_compositor or scene_root is NULL, or parent/child not found
Behavior:
  Looks up both nodes via node_find_by_id, calls node_add_child(), marks dirty
```

### Syscall 123 — node_move

```
Signature:  int sys_gui_node_move(uint32_t node_id, int x, int y)
Parameters:
  rdi: node ID
  rsi: new local X position
  rdx: new local Y position
Returns:
  rax: 0 on success, -1 if node not found
Behavior:
  Calls node_set_position(n, x, y)
```

### Syscall 124 — camera_zoom

```
Signature:  int sys_gui_camera_zoom(float zoom)
Parameters:
  rdi: float zoom (passed as int scaled by 1000 in user-space, divided by 1000.0f in kernel)
Returns:
  rax: 0 on success, -1 on failure
Behavior:
  Sets camera->zoom_fp = (int)(zoom * 65536.0f), marks full invalidate
```

---

## Legacy LGX API (being phased out)

Old syscalls via `int $0x80`:

| Syscall | Name | Description |
|---|---|---|
| 10 | `get_fb_info` | Returns framebuffer info via registers |
| 11 | `liw_present_fb` | Flips back-buffer |
| 12 | `liw_draw_pixel` | Draws a single pixel |
| 13 | `liw_present_frame` | Presents a raw pixel buffer |

These are being replaced by the Scene Graph SDK (syscalls 120-124). Applications like `doomgeneric` still use the old LGX API.

---

## GUI Subsystem Initialization Order

```
1. gui_init() from kernel_main()
2.   scene_graph_init()
3.   theme_engine_init()
4.   animation_engine_init()
5.   event_bus_create()
6.   input_manager_create(bus)
7.   camera_create(sw, sh)
8.   fb_renderer_create()
9.   node_create(NODE_CANVAS, "canvas") → g_scene->root
10.  window_node_create("demo_win", ...)
11.  widget creation (label, button, panel)
12.  node_add_child → hierarchy
13.  layout_engine_compute(win)
14.  animation_start() for window appearance
15.  tool_manager_create(bus, camera, root)
16.    tool_manager_add_tool(move)
17.    tool_manager_add_tool(select)
18.    tool_manager_add_tool(pan)
19.  focus_manager_create(bus, root)
20.  window_manager_create(bus, root)
21.  compositor_create(renderer, camera, bus, input, root)
22. gui_compositor_task() → infinite loop calling compositor_frame()
```

---

## Math Infrastructure

### rect.h — AABB Operations

```c
gui_rect_t rect_make(int x, int y, int w, int h);
gui_rect_t rect_zero(void);
bool rect_is_empty(gui_rect_t r);
bool rect_contains_point(gui_rect_t r, int px, int py);
bool rect_intersects(gui_rect_t a, gui_rect_t b);
gui_rect_t rect_intersection(gui_rect_t a, gui_rect_t b);
gui_rect_t rect_union(gui_rect_t a, gui_rect_t b);
gui_rect_t rect_offset(gui_rect_t r, int dx, int dy);
gui_rect_t rect_inflate(gui_rect_t r, int margin);
gui_rect_t rect_clip_to(gui_rect_t r, gui_rect_t bounds);
```

### transform.h — 3×3 Affine Transform (16.16 fixed-point)

```c
gui_transform_t transform_identity(void);
gui_transform_t transform_translation(int32_t tx, int32_t ty);
gui_transform_t transform_scale(int32_t sx, int32_t sy);
gui_transform_t transform_uniform_scale(int32_t s);
gui_transform_t transform_concat(gui_transform_t a, gui_transform_t b);
gui_pointi_t transform_apply(gui_transform_t t, int32_t px, int32_t py);
gui_rect_t transform_apply_rect(gui_transform_t t, gui_rect_t r);
gui_transform_t transform_invert_simple(gui_transform_t t);
```

---

## Performance & Memory

- All kernel GUI allocations use `kmalloc`/`kfree` from `kheap.h` (no pools yet)
- Back-buffer: `width * height * 4` bytes (e.g., 1024×768 = 3 MB)
- No variable-length arrays (C99 VLA forbidden in kernel)
- All math is integer/fixed-point — no SSE, no float in hot paths (camera, transform)
- `gui_event_t` is 40 bytes; ring buffer is 256 entries = 10 KB
- Subscriber table: 64 entries
- Animation slots: 64 entries
- Maximum children per node: 64
- Dirty-rect tracking pre-allocates 64 rectangles (not yet used in Phase 1)