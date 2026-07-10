# Profiling Infrastructure — LiwusOS GUI

## Objective

Document the profiling facilities available and planned for the LiwusOS GUI subsystem. Covers frame timing, draw call counting, memory tracking, performance counters, and the planned in-game profiler with graphs.

---

## Problems Solved

- Full repaint every frame (Phase 1) provides no insight into which phase dominates frame time
- No visibility into how many draw calls are made per frame
- No tracking of node count growth, backbuffer memory, or dirty rect accumulation
- No way to measure fill rate, culling efficiency, or render overhead
- Performance regressions are invisible without quantitative frame data

---

## Current State (Phase 1)

### What is Tracked

| Metric | Location | How |
|---|---|---|
| Frame number | `compositor_t.frame_number` | Incremented per compositor_frame() |
| Node count | `scene_graph_t.node_count` | Updated by node_create/destroy |
| Full redraw flag | `compositor_t.full_redraw` | Set by invalidate calls |
| Camera dirtiness | `camera_t.dirty` | Set on pan/zoom/reset |

### What is NOT Tracked

- Time spent per compositor_frame phase
- Number of draw calls per frame
- Backbuffer dimensions
- Dirty rect count (always full redraw)
- Culled vs visible node count
- Memory used by scene graph

---

## Planned Phase 4 Profiling Infrastructure

### Frame Timing

Measure and log wall-clock time for each phase of `compositor_frame()`:

```c
typedef enum {
    PHASE_POLL_INPUT,
    PHASE_DISPATCH_EVENTS,
    PHASE_CAMERA_UPDATE,
    PHASE_ANIMATIONS,
    PHASE_TRANSFORM,
    PHASE_BACKGROUND,
    PHASE_DRAW_NODES,
    PHASE_CURSOR,
    PHASE_PRESENT,
    PHASE_COUNT
} frame_phase_t;

typedef struct {
    const char *name;
    uint64_t    start_ticks;
    uint64_t    elapsed_ns;     /* elapsed this frame */
    uint64_t    total_ns;     /* cumulative */
    uint32_t    sample_count; /* frames measured */
    uint64_t    max_ns;       /* worst case */
    uint64_t    min_ns;       /* best case */
} frame_phase_stats_t;

static frame_phase_stats_t s_phase_stats[PHASE_COUNT];

void profile_phase_start(frame_phase_t phase) {
    s_phase_stats[phase].start_ticks = timer_read_ns();
}

void profile_phase_end(frame_phase_t phase) {
    uint64_t elapsed = timer_read_ns() - s_phase_stats[phase].start_ticks;
    frame_phase_stats_t *s = &s_phase_stats[phase];
    s->elapsed_ns = elapsed;
    s->total_ns += elapsed;
    if (elapsed > s->max_ns) s->max_ns = elapsed;
    if (elapsed < s->min_ns || s->sample_count == 0) s->min_ns = elapsed;
    s->sample_count++;
}
```

Usage in compositor:

```c
void compositor_frame(compositor_t *c) {
    prof_phase_start(PHASE_POLL_INPUT);
    input_manager_poll(c->input);
    prof_phase_end(PHASE_POLL_INPUT);
    
    prof_phase_start(PHASE_DISPATCH_EVENTS);
    event_bus_dispatch(c->bus);
    prof_phase_end(PHASE_DISPATCH_EVENTS);
    
    // ... etc
}
```

### Draw Call Count

```c
typedef struct {
    uint32_t fill_rect;      /* solid color rects */
    uint32_t draw_rect;      /* border rects */
    uint32_t blit;            /* blit operations */
    uint32_t blit_scaled;     /* scaled blits */
    uint32_t draw_glyph;      /* font glyph draws */
} draw_call_stats_t;
```

Counters incremented in each `fb_*` function:

```c
static void fb_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color) {
    g_draw_call_count.fill_rect++;
    // ...
}
```

Reset at the beginning of each `compositor_frame()`, logged at end.

### Memory Tracking

```c
typedef struct {
    uint32_t   node_count;
    uint32_t   child_slots_used;   /* sum of all child_counts */
    uint32_t   child_slots_total;  /* node_count * NODE_MAX_CHILDREN */
    size_t     backbuffer_bytes;    /* width * height * 4 */
    size_t     scene_heap_bytes;   /* node_t allocations */
    size_t     event_bus_bytes;    /* sizeof(gui_event_bus_t) + ring buffer */
    size_t     camera_bytes;       /* sizeof(camera_t) */
    size_t     renderer_bytes;     /* sizeof(gui_renderer_t) + backend */
    size_t     total_gui_bytes;    /* sum of all tracked */
} gui_memory_stats_t;
```

### Performance Counters

```c
typedef struct {
    uint64_t    frame_number;
    float       fps;              /* frames per second */
    
    /* Timing (microseconds) */
    uint64_t    frame_time_us;
    uint64_t    poll_input_us;
    uint64_t    dispatch_events_us;
    uint64_t    camera_update_us;
    uint64_t    transform_pass_us;
    uint64_t    background_us;
    uint64_t    draw_nodes_us;
    uint64_t    cursor_us;
    uint64_t    present_us;
    
    /* Draw calls */
    uint32_t    draw_fill_rect;
    uint32_t    draw_border_rect;
    uint32_t    draw_blit;
    uint32_t    draw_blit_scaled;
    uint32_t    draw_glyph;
    uint32_t    total_draw_calls;
    
    /* Scene graph */
    uint32_t    node_count;
    uint32_t    culled_nodes;
    uint32_t    visible_nodes;
    
    /* Dirty rects */
    uint32_t    dirty_rect_count;
    uint32_t    dirty_area_pixels;  /* total area of all dirty rects */
    
    /* Camera */
    int32_t     cam_pos_x_fp;
    int32_t     cam_pos_y_fp;
    int32_t     cam_zoom_fp;
    
    /* Memory */
    uint32_t    gui_heap_kb;
} gui_perf_counters_t;

static gui_perf_counters_t g_perf;
```

---

## Profiling Data Flow Diagram

```
compositor_frame()
  │
  ├─ [PROFILE start] ───────────────────────────────┐
  │                                                  │
  ├─ poll_input                                      │
  ├─ dispatch_events                                 │
  ├─ camera_update                                   │
  ├─ animation_engine_tick                           │
  ├─ node_update_transforms                          │
  ├─ draw_background                                 │
  ├─ node_draw_recursive                             │
  │     └─ per-widget draw()                         │
  ├─ cursor_draw                                     │
  ├─ renderer_present                                │
  │                                                  │
  ├─ [PROFILE collect]                              │
  │     ├─ read per-phase timers                      │
  │     ├─ reset draw call counters                   │
  │     ├─ read g_scene->node_count                    │
  │     ├─ calculate FPS from frame_number            │
  │     └─ store to gui_perf_counters                 │
  │                                                  │
  └─ [PROFILE output]                                │
        ├─ serial_print formatted report            │
        └─ overlay render (if debug mode enabled)     │
```

---

## Profiling Output Formats

### Serial Log Format

```
[PROFILE] frame=1234  fps=58  time=17239us
    phases:
      poll_input:       112μs  (0.6%)
      dispatch_events:  245μs  (1.4%)
      camera_update:    12μs   (0.1%)
      animations:       89μs   (0.5%)
      transform_pass:  430μs  (2.5%)
      background:     4120μs  (23.9%)
      draw_nodes:    10450μs  (60.6%)   <-- hotspot
      cursor:          420μs  (2.4%)
      present:        1240μs  (7.2%)
    draws: fill=42 border=12 blit=0 glyph=28 total=82
    culled: 4/12 nodes (33% savings projected)
    memory: scene=2.4KB bus=10.2KB camera=36B backbuf=3072KB
```

### On-Screen Overlay

```
┌─ PROFILER ───────────────────────────┐
│  FPS: 58    Frame: 17.2ms            │
│  ─────────────────────────────────  │
│  ● poll_input    0.1ms  ██          │
│  ● events        0.2ms  ███         │
│  ● transforms    0.4ms  ██████      │
│  ● background    4.1ms  ████████████│
│  ● draw nodes   10.5ms  ████████████│
│  ● present       1.2ms  ███████     │
│  ─────────────────────────────────  │
│  Draws: 82    Memory: 3.1 MB        │
│  Nodes: 12                           │
└──────────────────────────────────────┘
```

---

## Generating Perf Data

### Frame Time Measurement Using PIT/HPET

The kernel timer (`timer.h`, `timer_get_ticks()`) provides millisecond resolution. For microsecond precision, the HPET or TSC (RDTSC) would be used:

```c
static inline uint64_t read_tsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
```

TSC frequency calibration: divide by `cpu_mhz` to convert to microseconds.

### Draw Call Counting

Each renderer operation increments a shared counter:

```c
// In renderer.h, add to static inline wrappers:
static inline void renderer_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color) {
    if (r && r->ops->fill_rect) {
        g_draw_calls.fill_rect++;
        r->ops->fill_rect(r, rect, color);
    }
}
```

### Memory Usage

Tracked by wrapping `kmalloc`/`kfree` for GUI allocations (or by traversing the scene graph and summing node sizes):

```c
size_t gui_memory_usage(void) {
    size_t total = g_scene->node_count * sizeof(node_t);
    // Add child userdata sizes (widget-specific)
    // Add compositor, camera, renderer, event_bus
    // Add backbuffer
    return total;
}
```

---

## Profiling in QEMU

QEMU's `-smp` and `icount` options help create reproducible profiling conditions:

```bash
qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -serial file:profile.log \
    -m 512M
```

For timing accuracy, disable KVM (which shortcuts real hardware) and use `-icount` for deterministic execution:

```bash
qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -serial file:profile.log \
    -icount shift=7 \
    -no-kvm
```

---

## Current Profiling Capabilities

| Capability | Phase 1 | Phase 4 (Planned) |
|---|---|---|
| Frame counter | ✅ `frame_number` | ✅ + FPS calculation |
| Serial debug messages | ✅ `serial_print()` | ✅ + formatted reports |
| Node count | ✅ `scene_graph_t.node_count` | ✅ + tracking by type |
| Per-phase timing | ❌ | ✅ TSC-based phase timers |
| Draw call counting | ❌ | ✅ Counter in renderer ops |
| Memory tracking | ❌ | ✅ Per-module breakdown |
| On-screen profiler | ❌ | ✅ DEBUG node overlay |
| Historical graphs | ❌ | Rolling window of 60 samples |
| Culling stats | ❌ (no culling) | ✅ Viewport/occlusion counters |

---

## Dependencies

- **TSC/RDTSC instruction** — available on all x86-64 CPUs
- **Timer driver** (`timer.h`) — for millisecond gettimeofday
- **Serial driver** — for formatted output of perf data
- **Renderer ops** — draw call counters added to every ops wrapper
- **Compositor** — phase markers inserted into compositor_frame
- **Scene graph** — node_count already tracked; adding per-node userdata size tracking

---

## Limitations & Trade-offs

| Concern | Mitigation |
|---|---|
| RDTSC overhead (tens of ns per call) | Only measure key phases, not individual draw calls |
| Serial output slows frame loop | Profiling data batched and output every N frames (configurable) |
| 32-bit timers wrap at 4.29B ticks | Use 64-bit TSC or wrap-aware delta logic |
| No floating point for display | All profiling values as integers (μs, count, KB) |
| Overlay rendering costs FPS | Profiler overlay drawn by DEBUG node; only visible when toggled |

---

## Future Extensions

| Extension | Description |
|---|---|
| Rolling frame graph | 60-sample window shown as ASCII bar chart on serial or overlay |
| Per-node draw cost | Track which nodes consume the most draw time |
| Heap allocator statistics | Track GUI module's kmalloc/kfree pattern |
| Backbuffer write pixel count | Fill rate: total pixels written per frame |
| Dirty rect efficiency ratio | `dirty_pixels_repainted / total_screen_pixels` |
| Profile snapshot to file | Dump profile data to initrd file |