# Coding Guidelines — LiwusOS GUI

## Objective

Establish consistent coding conventions, file organization, naming rules, and module dependency constraints for all code in the LiwusOS GUI subsystem (`src/kernel/gui/`).

---

## Problems Solved

- Multiple developers contributing to a kernel-side GUI need a shared convention
- The kernel environment imposes constraints (no stdlib, no exceptions, no floating-point in hot paths, no VLA)
- Module dependencies must be enforced to prevent circular includes and architecture violations
- Fixed-point arithmetic conventions must be consistent across camera, transform, and layout code

---

## File Organization

```
src/kernel/gui/
├── gui_main.c / .h             # Bootstrap and compositor task entry
├── math/
│   ├── rect.h                  # AABB operations (header-only)
│   └── transform.h             # 3×3 affine transform (header-only)
├── scene/
│   ├── node.h / .c             # Scene graph node_t, scene_graph_t, traversal
│   └── camera.h / .c           # Fixed-point camera with zoom/pan
├── render/
│   ├── renderer.h / .c         # Abstract renderer interface
│   ├── fb_renderer.h / .c      # Software framebuffer backend
│   └── compositor.h / .c       # Compositor frame loop
├── core/
│   ├── event_bus.h / .c        # Typed, prioritized event bus
│   ├── theme_engine.h / .c     # Color palette manager
│   └── animation_engine.h / .c # Linear tweening engine
├── input/
│   ├── input_manager.h / .c    # Hardware state polling
│   └── tools/
│       ├── tool.h              # Abstract tool interface
│       ├── tool_manager.h / .c # Tool registry
│       ├── pan_tool.h / .c     # Camera panning
│       ├── select_tool.h / .c  # Node selection
│       └── move_tool.h / .c    # Node dragging
├── layout/
│   └── layout_engine.h / .c   # Flexbox-like layout
├── widgets/
│   ├── window_node.h / .c      # Decorated window widget
│   ├── button.h / .c           # Clickable button
│   ├── label.h / .c            # Single-line text label
│   └── panel.h / .c            # Container panel
└── window/
    ├── focus_manager.h / .c     # Keyboard focus routing
    └── window_manager.h / .c    # Z-order management
```

**Include paths:**
- Within `gui/`: relative includes (e.g., `"../scene/node.h"`)
- From outside `gui/`: kernel-wide via `"gui/scene/node.h"` (though no external includes call GUI code directly except `syscall.c`)

---

## Naming Conventions

### Functions and Variables

```c
/* snake_case for all functions and variables */
void scene_graph_init(void);
node_t *node_create(node_type_t type, const char *name);
static int current_y = 0;  /* or s_current_y for file-static */
```

### Types

```c
/* _t suffix for typedefs */
typedef struct node node_t;
typedef struct gui_renderer gui_renderer_t;
typedef enum { ... } node_type_t;
```

### Enumerators and Macros

```c
/* SCREAMING_CASE for all enum values and #defines */
#define NODE_MAX_CHILDREN 64
#define NODE_DIRTY_TRANSFORM (1u << 0)
enum { NODE_GENERIC = 0, NODE_CANVAS = 1 };
```

### Struct Fields

```c
/* snake_case */
struct node {
    uint32_t id;
    int local_x, local_y;
    bool visible;
};
```

### Global Variables

```c
/* Prefix with g_ for module-level globals, s_ for file-static */
scene_graph_t *g_scene;        /* declared in node.h, defined in node.c */
compositor_t  *g_compositor;  /* declared in compositor.h, defined in compositor.c */
static uint32_t s_palette[THEME_COLOR_MAX];  /* theme_engine.c */
```

---

## Include Style

```c
/* Within gui/ tree -> relative paths */
#include "../math/rect.h"
#include "../core/event_bus.h"

/* From kernel-wide headers -> by name only (set -I flags) */
#include "kheap.h"
#include "string.h"
#include "serial.h"
#include "mouse.h"
#include "keyboard.h"
#include "task.h"
```

---

## Memory Management

```c
/* All GUI allocations use the kernel heap */
node_t *n = (node_t *)kmalloc(sizeof(node_t));
kfree(n);

/* NEVER use variable-length arrays (VLA) */
uint32_t tmp[64];          /* OK: fixed size */
/* uint32_t tmp[n]; */      /* FORBIDDEN: VLA */

/* Always zero memory after allocation */
memset(n, 0, sizeof(node_t));

/* Always check kmalloc return values */
node_t *n = (node_t *)kmalloc(sizeof(node_t));
if (!n) return NULL;
```

---

## Error Handling

```c
/* Return NULL / false on failure — no exceptions */
node_t *node_create(...) {
    node_t *n = kmalloc(...);
    if (!n) return NULL;    /* never assert on OOM */
    return n;
}

bool node_add_child(node_t *parent, node_t *child) {
    if (!parent || !child) return false;   /* parameter validation */
    if (parent->child_count >= NODE_MAX_CHILDREN) return false;
    ...
}
```

---

## Fixed-Point Arithmetic

### Constants

```c
#define CAMERA_ZOOM_SCALE  1024    /* 1.0 = 1024 */
#define CAMERA_POS_SCALE   256     /* 1px = 256 */
#define TRANSFORM_SCALE    65536   /* 16.16 fixed-point */
```

### Rules

```c
/* No floating point in hot paths (render, camera, transform) */
int32_t zoom_fp;  /* scaled by CAMERA_ZOOM_SCALE */
int32_t pos_x_fp; /* scaled by CAMERA_POS_SCALE */

/* Use int64_t for intermediate products to avoid overflow */
int64_t diff = (int64_t)(wx * CAMERA_POS_SCALE) - (int64_t)c->pos_x_fp;
int result = (int)((diff * c->zoom_fp) / ((int64_t)CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE));

/* Use helper macros for clarity */
#define WORLD_TO_SCREEN(world_val, pos_fp, zoom_fp) \
    (((((int64_t)(world_val) * CAMERA_POS_SCALE) - (int64_t)(pos_fp)) * (zoom_fp)) / \
     ((int64_t)CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE))
```

### Float Permitted Only In:
- `camera_zoom_at()` — temporary for zoom pivot calculation (not hot path)
- `fb_blit_scaled()` — nearest-neighbour scaling (Phase 1; will be converted to fixed-point)
- `transform_invert_simple()` — uses float division (marked as temporary)
- `animation_engine.c` — `opacity` float conversions
- `input_manager_poll()` — not performance-critical
- `camera_zoom()` syscall — float conversion from user-space

---

## Vtable Pattern

```c
/* Backend-abstracted operations table */
typedef struct {
    void  (*draw)(node_t *self, struct gui_renderer *r);
    bool  (*on_event)(node_t *self, const gui_event_t *event);
    void  (*layout)(node_t *self);
    void  (*destroy)(node_t *self);
} node_vtable_t;

/* Ops are optional — check for NULL before calling */
if (node->vtable && node->vtable->draw) {
    node->vtable->draw(node, r);
}

/* Ops table is const — one shared instance per type */
static const node_vtable_t button_vtable = {
    .draw     = button_draw,
    .on_event = button_on_event,
    .layout   = NULL,        /* use layout_engine instead */
    .destroy  = button_destroy,
};
```

---

## Event Handling

```c
/* NEVER read hardware directly — always go through EventBus */
/* FORBIDDEN: */
// if (is_left_clicked()) { ... }
// if (keyboard_is_pressed(0x1D)) { ... }

/* CORRECT: subscribe to EventBus and handle typed events */
static void my_handler(const gui_event_t *event, void *userdata) {
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        // process
    }
}

/* Return true to stop propagation */
bool button_on_event(node_t *self, const gui_event_t *e) {
    if (e->type == GUI_EVENT_MOUSE_DOWN) {
        return true;  /* consume the event */
    }
    return false;     /* let it propagate */
}
```

---

## Dependency Rules

```
┌─────────────────────────────────────────────────────┐
│              Dependency Diagram                   │
│                                                   │
│  math/    (rect.h, transform.h)                     │
│    │  (no dependencies)                            │
│    ▼                                              │
│  scene/   (node.h, camera.h)                      │
│    │        depends on math/                       │
│    ▼                                              │
│  core/    (event_bus, theme, animation)           │
│    │        depends on scene/                      │
│    ▼                                              │
│  input/   (input_manager, tools/)                  │
│    │        depends on core/ + scene/              │
│    ▼                                              │
│  layout/  (layout_engine)                         │
│    │        depends on scene/                       │
│    ▼                                              │
│  render/  (renderer, compositor)                  │
│    │        depends on scene/ + core/ + input/     │
│    ▼                                              │
│  widgets/ (button, label, panel, window_node)      │
│    │        depends on scene/ + render/ + core/    │
│    ▼                                              │
│  window/  (focus_manager, window_manager)          │
│             depends on scene/ + core/              │
│                                                   │
│  gui_main.c  — depends on ALL modules              │
└─────────────────────────────────────────────────────┘
```

### Rules

1. **math/** — no dependencies (only stdint, stdbool)
2. **scene/** — depends only on **math/**
3. **core/** — depends on **scene/**
4. **layout/** — depends on **scene/**
5. **render/** — depends on **scene/**, **core/**, **input/**
6. **input/** — depends on **core/**, **scene/**
7. **widgets/** — depends on **scene/**, **render/**, **core/**
8. **window/** — depends on **scene/**, **core/**
9. **gui_main.c** — depends on all modules
10. **NO module** may depend on **widgets/** or **gui_main.c**
11. **NO circular dependencies** allowed

---

## Code Style

### Indentation

```c
/* 4-space indentation, NO tabs */
int main(void) {
    int x = 0;
    if (x) {
        do_something();
    }
    return 0;
}
```

### Braces

```c
/* Braces on same line for control flow */
if (condition) {
    do_something();
} else {
    do_other();
}

/* Braces on new line for function definitions */
void function(void)
{
    /* ... */
}
```

### Comments

```c
/* Use /* */ style comments only */
```

### Line Length

Prefer lines under 100 characters. Long function signatures may wrap:

```c
void renderer_blit(gui_renderer_t *r,
                   int dest_x, int dest_y,
                   const uint32_t *src, int src_w, int src_h, int src_pitch,
                   int src_x, int src_y, int copy_w, int copy_h);
```

### Null Checks

```c
/* Check early, return early */
void node_set_position(node_t *node, int x, int y) {
    if (!node) return;
    if (node->local_x == x && node->local_y == y) return;  /* no-op */
    node->local_x = x;
    node->local_y = y;
    ...
}
```

---

## Kernel-Specific Constraints

```c
/* No standard library (except kernel's own string.h) */
/* No FILE*, no printf */

/* Use serial_print() for debug output */
extern void serial_print(const char *s);

/* Use kernel string functions */
#include "string.h"   /* provides memset, memcpy, strlen, strcpy */

/* No malloc/free — use kmalloc/kfree from kheap.h */
#include "kheap.h"

/* No SSE/MMX/AVX — kernel compiled with -mno-sse -mno-mmx */
/* All math must be integer or fixed-point */

/* No C++ features — pure C99/C11 */
```

---

## Module Header Template

```c
/*
 * gui/module_name/module.h
 *
 * Brief description of module purpose.
 * Design notes, architecture decisions, constraints.
 */

#ifndef GUI_MODULE_H
#define GUI_MODULE_H

#include <stdint.h>
#include <stdbool.h>

/* --- Public Types --- */

/* --- Constants --- */

/* --- Functions --- */

#endif /* GUI_MODULE_H */
```

---

## Commit Message Style

```
gui: Brief description of changes

- Detailed breakdown of changes
- Reference related code paths

Examples:
  gui(scene): add node_update_transforms lazy recomputation
  gui(compositor): fix cursor ghost by restoring saved pixels
  gui(syscall): implement canvas_create (syscall 120)
```

---

## Enforcement

- These guidelines are conventions, not enforced by tooling
- Code review should ensure consistency
- New modules should follow these conventions from the start
- Legacy LGX code (`libliw.h`, `view.c`, `doomgeneric_liwus.c`) may not follow these conventions and is exempt |