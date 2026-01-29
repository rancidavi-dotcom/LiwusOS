# Testing Strategy — LiwusOS GUI

## Objective

Document the testing strategy for the LiwusOS GUI subsystem, including unit tests for math primitives, render verification, widget integration tests, visual regression, and the test harness architecture.

---

## Problems Solved

- Currently **no automated tests exist** — all GUI verification is manual via QEMU + visual inspection
- Kernel code operating on pixel buffers cannot use standard testing frameworks (no filesystem, no libc)
- Fixed-point math (camera, transforms) is error-prone and needs parametric testing
- Widget behavior (hit-testing, event handling, layout) must be verified independently of the compositor
- Visual regressions from backbuffer changes cannot be caught without reference rendering

---

## Current State (Phase 1)

### Testing Approach

- Manual visual inspection in QEMU
- Serial debug prints from `node_draw_recursive()`, `window_draw()`
- Hand-testing by launching `demo_gui` from the initrd

### What is NOT Tested

- Camera coordinate conversions (world↔screen)
- Transform composition (16.16 fixed-point)
- Rect operations (intersection, union, containment)
- Layout engine (vbox, hbox, flex)
- Alpha blending (per-pixel ARGB)
- Widget event handling (button click detection)
- Hit-testing (screen bounds checking)
- Node lifecycle (create → add → remove → destroy)
- Memory management (no leaks, no double-free)
- Compositor frame sequence

---

## Future Test Architecture

```
┌────────────────────────────────────────────────────────┐
│                  Test Harness Architecture                │
│                                                          │
│  ┌──────────────────────────────────────────────────┐  │
│  │              Test Runner Process                    │  │
│  │  (kernel task, runs in kernel space)                │  │
│  │                                                     │  │
│  │  ┌────────────────────────────────────────────┐          │  │
│  │  │  Test Suite Registry                │          │  │
│  │  │  ├─ test_math_rect.c                │          │  │
│  │  │  ├─ test_math_transform.c          │          │  │
│  │  │  ├─ test_render_alpha.c             │          │  │
│  │  │  ├─ test_widget_button.c           │          │  │
│  │  │  ├─ test_layout_vbox.c              │          │  │
│  │  │  ├─ test_scene_hierarchy.c          │          │  │
│  │  │  └─ test_compositor_basic.c         │          │  │
│  │  └────────────────────────────────────────────┘          │  │
│  │                                                     │  │
│  │  ┌────────────────────────────────────────────┐          │  │
│  │  │  Test Runner Backend                       │          │  │
│  │  │  ├── ASSERT(cond) macro                      │          │  │
│  │  │  ├── EXPECT_EQ(a, b)                        │          │  │
│  │  │  ├── EXPECT_NEAR(a, b, eps)                 │          │  │
│  │  │  ├── TEST(name) { ... }                      │          │  │
│  │  │  └── serial_print test results              │          │  │
│  │  └────────────────────────────────────────────┘          │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │              Visual Regression Engine                │  │
│  │  (Compares rendered backbuffer against reference)   │  │
│  │                                                     │  │
│  │  1. Render test scene into off-screen framebuffer  │  │
│  │  2. Compare pixel-by-pixel against reference .ppm  │  │
│  │  3. Report PASS/FAIL with diff-count                │  │
│  └────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
```

---

## Unit Test Framework Design

### Macros

```c
/* gui/gui_test.h */

#include <stdbool.h>
#include "serial.h"

static int g_test_passed = 0;
static int g_test_failed = 0;

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            serial_print("[FAIL] "); \
            serial_print(msg); \
            serial_print("\n"); \
            g_test_failed++; \
            return; \
        } \
    } while(0)

#define EXPECT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            serial_print("[FAIL] "); \
            serial_print(msg); \
            serial_print(": expected "); \
            serial_print_int((int)(b)); \
            serial_print(" got "); \
            serial_print_int((int)(a)); \
            serial_print("\n"); \
            g_test_failed++; \
        } else { \
            g_test_passed++; \
        } \
    } while(0)

#define TEST(name) \
    static void test_##name(void)

#define RUN_TEST(name) \
    do { \
        serial_print("[TEST] " #name "\n"); \
        g_test_passed = 0; \
        g_test_failed = 0; \
        test_##name(); \
        serial_print("[RESULT] " #name ": "); \
        serial_print_int(g_test_passed); \
        serial_print(" passed, "); \
        serial_print_int(g_test_failed); \
        serial_print(" failed\n"); \
    } while(0)
```

---

## Unit Test Suites

### 1. Math — Rect Operations

```c
TEST(rect_contains_point_inside) {
    gui_rect_t r = rect_make(10, 20, 100, 50);
    ASSERT(rect_contains_point(r, 50, 40), "center point should be inside");
    ASSERT(rect_contains_point(r, 10, 20), "top-left edge should be inside");
    ASSERT(!rect_contains_point(r, 9, 20), "1px left should be outside");
    ASSERT(!rect_contains_point(r, 110, 70), "bottom-right should be outside");
}

TEST(rect_intersection_partial) {
    gui_rect_t a = rect_make(0, 0, 100, 100);
    gui_rect_t b = rect_make(50, 50, 100, 100);
    gui_rect_t c = rect_intersection(a, b);
    ASSERT(c.x == 50 && c.y == 50, "intersection x,y");
    ASSERT(c.width == 50 && c.height == 50, "intersection w,h");
    ASSERT(rect_is_empty(rect_intersection(a, rect_make(200, 200, 10, 10))), "no overlap");
}

TEST(rect_union_adjacent) {
    gui_rect_t a = rect_make(0, 0, 50, 50);
    gui_rect_t b = rect_make(50, 0, 50, 50);
    gui_rect_t u = rect_union(a, b);
    ASSERT(u.width == 100 && u.height == 50, "union adjacents");
}

TEST(rect_inflate) {
    gui_rect_t r = rect_make(100, 100, 10, 10);
    gui_rect_t i = rect_inflate(r, 5);
    ASSERT(i.x == 95 && i.y == 95, "inflate outer corner");
    ASSERT(i.width == 20 && i.height == 20, "inflate dimensions");
}
```

### 2. Math — Transform Tests

```c
TEST(transform_identity) {
    gui_transform_t t = transform_identity();
    gui_pointi_t p = transform_apply(t, 42, 73);
    ASSERT(p.x == 42 && p.y == 73, "identity transform");
}

TEST(transform_translation) {
    gui_transform_t t = transform_translation(100, 200);
    gui_pointi_t p = transform_apply(t, 10, 20);
    ASSERT(p.x == 110 && p.y == 220, "translation");
}

TEST(transform_concat) {
    gui_transform_t t1 = transform_translation(10, 20);
    gui_transform_t t2 = transform_translation(30, 40);
    gui_transform_t r = transform_concat(t1, t2);
    gui_pointi_t p = transform_apply(r, 0, 0);
    ASSERT(p.x == 40 && p.y == 60, "concat translations");
}

TEST(transform_apply_rect) {
    gui_transform_t t = transform_translation(50, 25);
    gui_rect_t r = rect_make(0, 0, 100, 100);
    gui_rect_t tr = transform_apply_rect(t, r);
    ASSERT(tr.x == 50 && tr.y == 25, "rect top-left");
    ASSERT(tr.width == 100 && tr.height == 100, "rect dimensions preserved");
}
```

### 3. Render — Alpha Blend Tests

```c
TEST(blend_opaque) {
    uint32_t bg = 0xFFFF0000;
    uint32_t fg = 0xFF0000FF;  /* fully opaque blue */
    uint32_t result = alpha_blend(bg, fg);
    ASSERT(result == fg, "opaque fg should overwrite bg");
}

TEST(blend_transparent) {
    uint32_t bg = 0xFFFF0000;
    uint32_t fg = 0x000000FF;  // fully transparent
    uint32_t result = alpha_blend(bg, fg);
    ASSERT(result == bg, "transparent fg should preserve bg");
}

TEST(blend_50pct) {
    uint32_t bg = 0xFFFF0000;  /* R=255 */
    uint32_t fg = 0x800000FF;  /* A=128 (50%): B=255 */
    uint32_t result = alpha_blend(bg, fg);
    uint8_t r = (result >> 16) & 0xFF;  /* ~128 */
    uint8_t b = result & 0xFF;          /* ~128 */
    ASSERT(r > 100 && r < 150, "red ~50%");
    ASSERT(b > 100 && b < 150, "blue ~50%");
}

/* Verify fb_fill_rect produces correct pixels */
TEST(fill_rect_opaque_clip) {
    // Create a 100x100 backbuffer
    // Fill a sub-rect
    // Check that only the sub-rect pixels are set
    // Check out-of-bounds pixels are unchanged
}
```

### 4. Widget — Button Tests

```c
TEST(button_hit_test) {
    node_t *btn = button_create("test", 10, 20, 100, 30, "Click");
    node_update_transforms(btn, transform_identity());
    extern compositor_t *g_compositor;
    /* need to simulate camera → screen_bounds update */
    /* call update_screen_bounds(btn) */
    
    /* Camera at 1.0 zoom, position (0,0) */
    /* Button at world (10,20,100,30) → screen (10,20,100,30) */
    /* node_hit_test on screen point (15,25) should return btn */
    /* node_hit_test on screen point (5,5) should return NULL */
}

TEST(button_click_event) {
    bool clicked = false;
    node_t *btn = button_create("Button", 0, 0, 100, 30, "Click");
    button_set_on_click(btn, callback, &clicked);
    
    /* Simulate MOUSE_DOWN at center of button */
    gui_event_t e = {
        .type = GUI_EVENT_MOUSE_DOWN,
        .mouse = { .x = 50, .y = 15, .button = 1 }
    };
    bool handled = button_on_event(btn, &e);
    ASSERT(handled, "button should handle click");
    
    /* Simulate MOUSE_UP */
    e.type = GUI_EVENT_MOUSE_UP;
    button_on_event(btn, &e);
    ASSERT(clicked, "callback should fire");
}
```

### 5. Scene Graph Tests

```c
TEST(hierarchy_add_child) {
    node_t *parent = node_create(NODE_GROUP, "parent");
    node_t *child = node_create(NODE_LABEL, "child");
    ASSERT(node_add_child(parent, child), "add child");
    ASSERT(child->parent == parent, "parent link");
    ASSERT(parent->child_count == 1, "child count");
}

TEST(hierarchy_remove_child) {
    node_t *parent = node_create(NODE_GROUP, "parent");
    node_t *child = node_create(NODE_LABEL, "child");
    node_add_child(parent, child);
    node_remove_child(parent, child);
    ASSERT(parent->child_count == 0, "empty after remove");
    ASSERT(child->parent == NULL, "detached from parent");
}

TEST(max_children) {
    node_t *parent = node_create(NODE_GROUP, "parent");
    for (int i = 0; i < NODE_MAX_CHILDREN; i++) {
        node_t *c = node_create(NODE_LABEL, "c");
        ASSERT(node_add_child(parent, c), "should add up to max");
    }
    /* 65th should fail */
    node_t *extra = node_create(NODE_LABEL, "extra");
    ASSERT(!node_add_child(parent, extra), "should reject overflow");
    node_destroy(extra);
}

TEST(depth_first_destroy) {
    node_t *root = node_create(NODE_CANVAS, "root");
    node_t *child = node_create(NODE_WINDOW, "child");
    node_t *grandchild = node_create(NODE_BUTTON, "gc");
    node_add_child(child, grandchild);
    node_add_child(root, child);
    node_destroy(root);
    /* Should not crash. All memory freed. */
}

TEST(hit_test_depth_order) {
    /* Create node A at (0,0,100,100), node B at (0,0,100,100) */
    /* B is added after A (higher z-order) */
    /* Hit test at (50,50) should return B, not A */
}
```

### 6. Layout Engine Tests

```c
TEST(vbox_vertical_stacking) {
    node_t *container = node_create(NODE_PANEL, "container");
    container->width = 200;
    container->height = 300;
    container->layout_type = LAYOUT_VBOX;
    
    node_t *a = node_create(NODE_PANEL, "a"); a->height = 30;
    node_t *b = node_create(NODE_PANEL, "b"); b->height = 40;
    node_add_child(container, a);
    node_add_child(container, b);
    
    layout_engine_compute(container);
    ASSERT(a->local_y == 0, "first child at top");
    ASSERT(b->local_y == 30, "second child below first");
}

TEST(hbox_distributes_width) {
    /* Similar test for horizontal layout */
}

TEST(flex_weight_distribution) {
    /* Container 300px, padding 10 each side = 280 available */
    /* Two children with flex_weight 1 and 2 */
    /* Children should get ~93px and ~186px */
}
```

### 7. Camera Tests

```c
TEST(world_to_screen_identity) {
    camera_t *cam = camera_create(800, 600);
    /* default: zoom=1.0, pos=(0,0) */
    int sx = camera_world_to_screen_x(cam, 100);
    int sy = camera_world_to_screen_y(cam, 100);
    ASSERT(sx == 100, "identity world→screen X");
    ASSERT(sy == 100, "identity world→screen Y");
    camera_destroy(cam);
}

TEST(screen_to_world_identity) {
    camera_t *cam = camera_create(800, 600);
    int wx = camera_screen_to_world_x(cam, 100);
    int wy = camera_screen_to_world_y(cam, 100);
    ASSERT(wx == 100, "identity screen→world X");
    ASSERT(wy == 100, "identity screen→world Y");
    camera_destroy(cam);
}

TEST(zoom_2x) {
    camera_t *cam = camera_create(800, 600);
    cam->zoom_fp = CAMERA_ZOOM_SCALE * 2;  /* 2.0× */
    int sx = camera_world_to_screen_x(cam, 100);
    ASSERT(sx == 200, "2x zoom: world 100 → screen 200");
    camera_destroy(cam);
}

TEST(roundtrip_zoom_pan) {
    // world→screen→world should return original value
    camera_t *cam = camera_create(800, 600);
    cam->zoom_fp = CAMERA_ZOOM_SCALE * 2;  // 2.0x
    cam->pos_x_fp = 50 * CAMERA_POS_SCALE;
    
    int original = 123;
    int screen = camera_world_to_screen_x(cam, original);
    int world = camera_screen_to_world_x(cam, screen);
    ASSERT(world == original, "roundtrip");
    camera_destroy(cam);
}
```

---

## Visual Regression Testing

### Approach

1. Render a known scene to an off-screen backbuffer
2. Compare pixel-by-pixel against a reference `.ppm` file (stored as C array)
3. Report number of differing pixels

```c
typedef struct {
    const uint32_t *reference_pixels;
    int width, height;
    char *description;
} visual_test_t;

int run_visual_test(const visual_test_t *test) {
    // Setup test scene (e.g., canvas + button + label)
    // Render to a known backbuffer
    // Compare pixel-by-pixel
    int diff_count = 0;
    for (int y = 0; y < test->height; y++) {
        for (int x = 0; x < test->width; x++) {
            if (backbuf[y * width + x] != test->reference_pixels[y * width + x]) {
                diff_count++;
            }
        }
    }
    serial_print("[VISUAL] %s: %d differing pixels\n", test->description, diff_count);
    return diff_count;
}
```

### Reference Image Storage

Rendered reference frames embedded as C arrays:

```c
/* test/reference/button_click_scene.h */
static const uint32_t g_button_click_ref[] = {
    0xFF0B1120, 0xFF0B1120, 0xFF0B1120, /* ... 1024×768 pixels */ 
};
```

Generated by running QEMU + dumping `fb_renderer_backbuf()` to a file.

---

## Test Environment

### Approach 1: Kernel-Side Test Task

A dedicated kernel task that runs test suites:

```c
void gui_test_task(void) {
    serial_print("[GUI_TEST] Starting GUI test suite\n");
    
    scene_graph_init();
    RUN_TEST(rect_contains_point_center);
    RUN_TEST(rect_intersection_partial);
    /* ... */
    
    scene_graph_destroy();
    serial_print("[GUI_TEST] Suite complete\n");
    while (1);
}
```

Launched from kernel_main after GUI init, with serial output captured.

### Approach 2: User-Space Test App

A user-space ELF (`apps/test_gui.elf`) that:
1. Creates a canvas
2. Adds widgets
3. Verifies syscall return values
4. Exits with 0 (pass) or 1 (fail)

```c
int main() {
    int pass = 0, fail = 0;
    
    Canvas c = canvas_create(400, 300, "Test");
    if (c == 0) { exit(1); }
    pass++;
    
    Node n = text_create("Hello");
    if (n == 0) { exit(1); }
    pass++;
    
    canvas_add(c, n);
    // Verify via syscall return? Not directly visible.
    // Instead: camera_zoom(2.0) and verify return code
    int ret = camera_zoom(2.0);  // no return value concept currently
    
    exit(0);
}
```

Approach 1 (kernel-side) is preferred for comprehensive testing.

---

## Current Test Stubs

Several test files exist or are planned:

| File | Status | Content |
|---|---|---|
| `apps/test_open.c` | Exists | VFS/file test, not GUI |
| `apps/doomgeneric/` | Exists | Non-test, production app |
| `apps/demo_gui/` | Exists | Manual demo, not automated |

No GUI-specific test files currently exist.

---

## Dependencies

- **Serial driver:** For test result output
- **Scene graph:** Must be initializable in test mode without full GUI init
- **Camera:** No renderer required for coordinate tests
- **Renderer:** Requires `fb_renderer_create()` + backbuffer for visual tests
- **Math module:** Fully independent — easiest to test first

---

## Limitations & Trade-offs

| Concern | Mitigation |
|---|---|
| No test framework library available | Minimal macros (ASSERT, EXPECT_EQ) are sufficient |
| Kernel-side tests affect system stability | Run tests before compositor starts; halt on failure |
| Visual tests require pixel-perfect rendering | Allow per-pixel tolerance (e.g., ±1 in each channel) |
| No filesystem for reference images | Embed references as C arrays at compile time |
| User-space tests have no event verification | Events are kernel-only; test via kernel-side test task |

---

## Future Extensions

| Extension | Description | Phase |
|---|---|---|
| Automated test in CI | Run QEMU in batch mode, capture serial output, grep for PASS/FAIL | Phase 4 |
| Fuzz testing | Random sequences of node_create/add_child/move to find crashes | Phase 4 |
| Regression test images | .ppm files as C arrays for each widget type | Phase 4 |
| Coverage tracking | Track which code paths are exercised | Phase 5 |
| Compositor stress test | Test with 100+ nodes, rapid animations, extreme zoom | Phase 5 |
| User-space test suite | ELF runner with exit codes for CI | Phase 6 |