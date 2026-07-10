# Performance Architecture

## Objective

Deliver a smooth 60 FPS GUI compositor (16.67 ms frame budget) on the host CPU target, using no floating-point arithmetic (`-mno-sse`), no GPU acceleration, and a single-threaded kernel task. The current implementation uses full redraw every frame; this document describes the planned optimization path.

## Problems Solved

- **Full redraw bottleneck**: Every frame copies `W × H × 4` bytes from backbuffer to VRAM and fills the entire backbuffer. At 1920×1080 this is ~8 MB per frame.
- **Overdraw**: Nodes behind opaque windows are painted and then immediately overwritten.
- **Off-screen rendering**: Nodes outside the camera viewport consume fill rate.
- **Allocation storms**: Frequent `kmalloc`/`kfree` during widget animation or rapid creation.
- **Redundant layout**: Layout engine recomputes the full tree each frame even when nothing changes.
- **Float penalty**: All coordinate math uses `int32_t` or fixed-point (`TRANSFORM_SCALE=65536`, `CAMERA_POS_SCALE=256`, `CAMERA_ZOOM_SCALE=1024`). No `float` or `double` anywhere except `fb_blit_scaled` and `transform_invert_simple` (marked for removal).

## Architecture

### Current Frame Budget (Phase 1 — Full Redraw)

```
compositor_frame()                           ≈ 16 ms (target)
 ├── input_manager_poll()                    ≈ 0.1 ms
 ├── event_bus_dispatch()                    ≈ 0.05 ms
 ├── camera_update()                         ≈ 0.01 ms
 ├── animation_engine_tick()                 ≈ 0.02 ms
 ├── node_update_transforms()                ≈ 0.1 ms (full tree)
 ├── draw_background()                       ≈ 4 ms (fill + dot grid)
 ├── node_draw_recursive()                   ≈ 8 ms (all visible nodes)
 ├── cursor_draw()                           ≈ 0.02 ms
 ├── renderer_present() (memcpy to VRAM)     ≈ 3.5 ms (8 MB at ~2.3 GB/s)
 └── switch_task()                           ≈ 0.01 ms
```

**Bottleneck**: `renderer_present()` + `draw_background()` account for ~50% of frame time on full redraw.

### Optimization Pipeline (Phases)

```
Phase 1 ── Full Redraw (current) ── Correct, simple, baseline
     │
Phase 2 ── Dirty Regions ────────── Only repaint changed areas
     │
Phase 3 ── Viewport Culling ─────── Skip off-screen nodes
     │
Phase 4 ── Occlusion Culling ───── Skip hidden nodes
     │
Phase 5 ── Render Batching ──────── Merge consecutive fill_rect calls
     │
Phase 6 ── Texture Atlas / Glyph Cache ── Batch sprites and text
     │
Phase 7 ── Object Pool ──────────── Zero-alloc widget creation
     │
Phase 8 ── Layout Cache ─────────── Skip unchanged subtrees
```

## Optimization Techniques

### 1. Dirty Regions (Phase 2)

**Concept**: Track modified screen areas in `compositor_t::dirty_rects[]` (max 64). Only repaint those regions instead of the full backbuffer.

**Implementation**:
```
compositor_invalidate(rect) → adds to dirty_rects[]
compositor_invalidate_full() → sets full_redraw = true

Frame loop:
  if full_redraw: clear entire backbuffer, draw all nodes
  else:
    for each dirty rect:
      clear that rect (fill with background)
      set clip rect
      draw nodes intersecting that rect (requires spatial index)
```

**When to use**: Any time a node changes (button hover, animation tick, text update). Use `compositor_invalidate()` instead of `full_redraw = true`.

**Trade-off**: Requires spatial queries ("which nodes overlap this rect?"). Without a spatial index, must iterate the full tree. QuadTree solves this.

### 2. Viewport Culling (Phase 3)

**Concept**: Skip nodes whose `screen_bounds` do not intersect the screen rectangle `[0,0, W,H]`.

**Implementation**:
```c
void node_draw_recursive(node_t *node, gui_renderer_t *r) {
    if (!node || !node->visible) return;
    // Viewport cull
    if (!rect_intersects(node->screen_bounds, screen_rect)) return;
    // ... draw and recurse ...
}
```

**When to use**: Always. Trivial check (4 integer comparisons) saves entire subtree draw costs. Critical for infinite canvas with many off-screen nodes.

### 3. Occlusion Culling (Phase 4)

**Concept**: If a node is completely hidden behind an opaque parent, skip it entirely.

**Implementation**: During the draw traversal, maintain a "clip stack" of opaque screen regions. A child is culled if its `screen_bounds` is entirely covered by an opaque ancestor.

**When to use**: Nodes inside windows with opaque backgrounds. The window panel covers all child widgets — no need to paint them if only the window's background rect is visible.

**Trade-off**: Only works for rectangular occlusion. Complex shapes (rounded corners, transparent windows) require pixel-level tests.

### 4. QuadTree Spatial Index

**Concept**: Partition the canvas into a QuadTree keyed by `screen_bounds`. `node_add_child` inserts the node into the tree. Dirty-rect queries return only candidate nodes.

```
Screen space partitioned into 4 quadrants, each sub-divided when > threshold nodes.
Query: O(log N) vs O(N) full walk
```

**When to use**: When scene exceeds ~200 nodes or widgets are spread across a large canvas.

### 5. Glyph Cache

**Concept**: Pre-render common text strings (button labels, window titles) into `uint32_t` ARGB buffers. Cache keyed by `(text_hash, fg_color, bg_color, zoom)`.

```c
glyph_cache_entry_t {
    uint32_t key_hash;   // FNV-1a of text+fg+bg+zoom
    uint32_t *bitmap;    // rendered pixels
    int w, h;
    bool lru_prev, lru_next;
};
```

**When to use**: Text-heavy UI (terminals, code editors, chat panels). Single words or short strings benefit most.

### 6. Texture Atlas

**Concept**: Pack multiple source images (button sprites, icons, cursor shapes) into a single `uint32_t` array. `blit` calls reference sub-rectangles in the atlas rather than individual buffers.

**When to use**: Many small fixed-size sprites. Reduces render state changes and improves cache locality.

### 7. Object Pool

**Concept**: Pre-allocate a slab of `node_t` structs. `node_create` returns from the slab; `node_destroy` returns to the slab. Same for small widget data.

```c
#define NODE_POOL_SIZE 256
static node_t s_node_pool[NODE_POOL_SIZE];
static uint32_t s_pool_head; // bitmap or free-list index
```

**When to use**: UI with dynamic widget creation (dropdowns that appear/disappear, popup menus, tooltips). Avoids `kmalloc` latency.

### 8. Render Batching

**Concept**: Merge consecutive `fill_rect` calls with the same color and clip into a single larger rect. Reduce function call overhead and memory writes.

```
Before: fill_rect(10,10,50,50, red), fill_rect(60,10,30,50, red)
After:  fill_rect(10,10,80,50, red)
```

**When to use**: Many adjacent same-color fills (e.g., grid backgrounds, table rows). Requires deferred rendering mode.

### 9. Layout Cache

**Concept**: Skip `layout_engine_compute` on subtrees where no layout property has changed (`NODE_DIRTY_LAYOUT` not set).

**When to use**: Always. Currently the compositor does not call layout each frame (only on node creation). When it does, the dirty flag check prevents wasted recomputation.

### 10. Lazy Updates

**Concept**: Defer non-critical recomputations (e.g., tooltip text layout, inspector overlay) by 1-2 frames. If the node changes again before the deferred computation runs, skip the intermediate render.

**When to use**: Tools like the inspector/debug overlay that update every frame but only need visual refresh at ~10 Hz.

## Fixed-Point Performance

All transform math uses `int32_t` with `int64_t` intermediates to avoid overflow:

```c
// Transform concat: 6 multiplies + 6 shifts
r.a = (int32_t)(((int64_t)b.a * a.a + (int64_t)b.c * a.b) / TRANSFORM_SCALE);

// Camera world→screen: 2 multiplies + 2 divides
int64_t diff = (int64_t)(wx * CAMERA_POS_SCALE) - (int64_t)c->pos_x_fp;
return (int)((diff * c->zoom_fp) / ((int64_t)CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE));
```

The `int64_t` intermediates prevent overflow when multiplying large world coordinates by zoom. Division by `CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE` (262,144) is a constant, but GCC cannot optimize it to a shift because it is not a power of two — this is an accepted cost.

## Measurements (Target)

| Operation | Current (est.) | Optimized (target) |
|-----------|---------------|-------------------|
| Full redraw (1920×1080) | ~16 ms | N/A (dirty regions) |
| Dirty rect repaint (200×100) | — | ~0.05 ms |
| Transform update (100 nodes) | ~0.1 ms | ~0.02 ms (skip clean) |
| Hit test (100 nodes) | ~0.02 ms | ~0.005 ms (QuadTree) |
| Layout compute (10 nodes) | ~0.01 ms | ~0.001 ms (cached) |
| `present()` memcpy | ~3.5 ms | ~0.01 ms (dirty partial) |

## Dependencies

- `int64_t` arithmetic (GCC built-in for 32-bit targets)
- No SSE, no FPU, no SIMD
- `memcpy`/`memset` for backbuffer operations (hand-optimized in `fb_present`)

## Limitations / Trade-offs

| Technique | Downside |
|-----------|----------|
| Dirty Regions | Requires spatial index for efficient node lookup; increases code complexity |
| Occlusion Culling | Does not handle non-rectangular alpha-blended windows |
| Glyph Cache | Memory cost per unique string; cache invalidation on theme change |
| Texture Atlas | Repacking when new images loaded; atlas size limits |
| Object Pool | Wastes memory if pool size exceeds actual usage |
| Render Batching | Requires deferred render command buffer instead of immediate mode |
| Fixed-point math | Limited precision for very large/small zoom levels; division is slower than FP |

## Future Extensions

| Feature | Impact |
|---------|--------|
| Dirty-region partial `present()` | Only copy changed scanlines to VRAM. Massive bandwidth saving. |
| GPU composition (Vulkan) | Eliminates software fill rate bottleneck. Requires Vulkan driver in kernel. |
| Signed Distance Field (SDF) fonts | Resolution-independent text rendering. Eliminates glyph cache for zoom. |
| DMA for backbuffer copy | Frees CPU during `present()`. |
| Multi-threaded render | Split screen into quadrants, render in parallel. Requires threading infra. |

## Usage Examples

```c
// Widgets signal dirtiness:
void button_set_text(node_t *button, const char *text) {
    // ... update text ...
    node_mark_dirty(button, NODE_DIRTY_PAINT);       // signals compositor
}

// Compositor responds:
void compositor_frame(compositor_t *c) {
    // Full redraw currently, but in Phase 2:
    if (c->full_redraw) {
        draw_background(c);
    } else {
        for (int i = 0; i < c->dirty_count; i++) {
            clear_rect(c, c->dirty_rects[i]);
            renderer_set_clip(c->renderer, c->dirty_rects[i]);
            // ... draw only affected nodes ...
        }
    }
}
```
