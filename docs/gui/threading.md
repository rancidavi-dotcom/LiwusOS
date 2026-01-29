# Threading Architecture (Future)

## Objective

Define the multi-threaded evolution of the LiwusOS GUI compositor, transitioning from a single kernel task to a parallel pipeline of dedicated threads. Each thread owns specific responsibilities and communicates via lock-free queues and shared state protected by spinlocks.

## Problems Solved

- **Single-threaded bottleneck**: One task handles input polling, event dispatch, layout, animation, rendering, and presentation. As scene complexity grows, 16.67 ms budget becomes insufficient.
- **Input latency**: Polling hardware in the same thread as rendering introduces jitter. Input thread can run at higher priority.
- **Asset loading stalls**: Font and image loading (future) blocks the render thread. Dedicated asset thread loads asynchronously.
- **Animation stutter**: Layout recomputation during animation ticks causes frame drops. Layout thread offloads computation.
- **CPU underutilization**: Modern multicore CPUs are underused by a single-threaded compositor.

## Architecture

### Thread Model

```
                    ┌─────────────────────┐
                    │   Compositor Thread  │  (main orchestrator)
                    │   (highest priority) │
                    └──────┬──────┬───────┘
                           │      │
              ┌────────────┘      └────────────┐
              ▼                                 ▼
    ┌─────────────────┐               ┌─────────────────┐
    │   Input Thread   │               │  Layout Thread   │
    │  (polls hardware) │              │ (computes layout) │
    │  → lock-free post │              │ → writes positions│
    └────────┬─────────┘               └────────┬─────────┘
             │                                   │
             ▼                                   ▼
    ┌─────────────────┐               ┌─────────────────┐
    │  Render Thread   │              │ Animation Thread │
    │ (fills backbuffer)│             │ (ticks tweens)   │
    │ → double-buffer   │             │ → marks dirty    │
    └────────┬─────────┘               └────────┬─────────┘
             │                                   │
             ▼                                   ▼
    ┌─────────────────┐               ┌─────────────────┐
    │   Asset Thread   │              │  (future)        │
    │ (loads from disk)│              │  GC / Compactor   │
    └─────────────────┘               └─────────────────┘
```

### Data Flow

```
Input Thread                        Asset Thread
    │                                    │
    │ lock-free ring buffer              │ mailbox
    ▼                                    ▼
Compositor Thread ── scene graph ── Render Thread
    │                     │               │
    │                     ▼               │
    │            Layout Thread            │
    │               (writes               │
    │           node->local_x/y)          │
    │                                    ▼
    └──── anim_engine_tick ────►   backbuffer
                                        │
                                    present()
                                        ▼
                                     VRAM
```

### Thread Responsibilities

#### Compositor Thread (Main Orchestrator)
- Receives events from Input Thread via lock-free ring buffer.
- Dispatches events to subscribers (widgets, tools, managers).
- Coordinates frame lifecycle: `FRAME_BEGIN` → process → `FRAME_END`.
- Owns the scene graph root pointer (read-mostly, write-locked for mutations).
- Starts render, layout, and animation work in parallel via a task graph.
- **Current**: This is `gui_compositor_task()` running `compositor_frame()` in a loop.

#### Input Thread
- Polls `mouse.c` and `keyboard.c` hardware registers at fixed interval (e.g., 1 kHz).
- Diffs state against previous frame.
- Posts `gui_event_t` to lock-free ring buffer (`event_bus_post` is already thread-safe by design — single producer, single consumer).
- **No heap allocation**: Events are stack-allocated and memcpy'd into the ring buffer.

#### Render Thread
- Owns the framebuffer backbuffer (`fb_state_t::backbuf`).
- Receives render commands from compositor via a command buffer (future: `render_cmd_t` ring).
- Processes `fill_rect`, `draw_rect`, `blit`, `draw_glyph` commands.
- Does NOT access the scene graph directly (uses pre-recorded command lists).
- Calls `present()` to swap/memcpy to VRAM.

#### Layout Thread
- Computes `layout_engine_compute()` for dirty subtrees.
- Writes `node->local_x`, `node->local_y`, `node->width`, `node->height`.
- Node positions protected by per-node spinlock or atomic store.
- Wakes on `NODE_DIRTY_LAYOUT` event.

#### Animation Thread
- Calls `animation_engine_tick()` each frame.
- Interpolates property values and writes them to node structs.
- Marks nodes dirty (`NODE_DIRTY_PAINT`) when values change.
- Highest frequency: runs at display refresh rate.

#### Asset Thread
- Receives load requests via a mailbox (e.g., `asset_manager_get_font("DejaVu")`).
- Loads font blobs from disk, parses PSF1/TrueType headers.
- Returns loaded asset via completion callback.
- Currently synchronous (loads embedded font at init). Future: disk-backed.

### Task Graph (Frame Pipeline)

```
Frame N:
  [Compositor] FRAME_BEGIN
       │
       ├── [Input] poll() ─────────────► events posted to ring
       │
       ├── [Layout] compute dirty ─────► writes positions
       │
       ├── [Animation] tick() ─────────► writes values, marks dirty
       │
       └── [Render] process commands ──► fills backbuffer
                                              │
                                         [Compositor] FRAME_END
                                              │
                                         present()
```

Dependencies:
- Layout must complete before Render (positions needed for screen_bounds).
- Animation can run in parallel with Layout if they touch different node properties.
- Input is independent and runs every frame regardless of render state.
- Asset loading is asynchronous and may span multiple frames.

## Synchronization Strategy

See also: `synchronization.md`

| Resource | Protection | Contention |
|----------|-----------|------------|
| Event ring buffer | Lock-free (single producer, single consumer) | None |
| Scene graph (read) | No protection (read-only during render) | None |
| Scene graph (write) | Spinlock + transactional batch | Low |
| Node position/ size | Per-node spinlock or atomic store | Low |
| Backbuffer | Double-buffered + fence | Present boundary |
| Animation state | Thread-local (anim thread owns `s_animations[]`) | None |
| Asset cache | Read-write lock | Low (load once) |

## APIs

### Not yet implemented — proposed:

```c
// Thread creation (future kernel API)
tid_t thread_create(const char *name, void (*entry)(void*), void *arg, int priority);

// Lock-free queue (already partially exists as event_bus ring buffer)
typedef struct lf_queue lf_queue_t;
lf_queue_t *lf_queue_create(int capacity);
bool lf_queue_push(lf_queue_t *q, const void *data, uint32_t size);
bool lf_queue_pop(lf_queue_t *q, void *data, uint32_t size);

// Render command buffer
typedef enum { RCMD_FILL_RECT, RCMD_BLIT, RCMD_GLYPH, RCMD_SET_CLIP } rcmd_type_t;
typedef struct { rcmd_type_t type; /* args */ } render_cmd_t;
void render_thread_submit(render_cmd_t *cmds, uint32_t count);
```

## Dependencies

- Kernel threading primitives (`task.h` already has `switch_task()` and `task_create`).
- Spinlock primitive (to be created in `sync.h`).
- Lock-free ring buffer (event bus design already supports single-producer single-consumer).
- Atomic operations (`__sync_fetch_and_add` GCC built-ins).

## Limitations / Trade-offs

| Trade-off | Rationale |
|-----------|-----------|
| Per-node spinlocks | Simplifies concurrent access but adds overhead. Consider RCU for read-heavy scene graph. |
| No work stealing (Phase 1) | Static assignment of work to threads. Simpler implementation. Work stealing with per-thread task queues planned. |
| Command buffer memory | Render commands are allocated from a fixed pool. Overflow stalls the compositor. |
| Thread count is fixed | No dynamic thread pool. Each core gets a dedicated role. |
| No GPU compute | All threads run software rasterization. GPU would change the model significantly. |

## Future Extensions

| Feature | Description |
|---------|-------------|
| Work stealing | Idle threads steal render tasks from busy threads. Balances load for uneven scene complexity. |
| Task graph scheduler | DAG-based scheduler that automatically resolves dependencies and fans out work. |
| GPU command submission | Render thread submits Vulkan command buffers instead of software fill. |
| Dynamic thread pool | Threads created/destroyed based on load. Power-efficient for mobile targets. |
| Frame prediction | Animations predict next frame's node positions to overlap layout and render. |

## Usage Examples

```c
// Future: multi-threaded compositor init
void gui_init_multithreaded(void) {
    scene_graph_init();
    gui_event_bus_t *bus = event_bus_create();

    thread_create("input",  input_thread_entry,  bus, PRIORITY_HIGH);
    thread_create("layout", layout_thread_entry, bus, PRIORITY_NORMAL);
    thread_create("anim",   anim_thread_entry,   bus, PRIORITY_NORMAL);
    thread_create("render", render_thread_entry, bus, PRIORITY_HIGH);
    thread_create("asset",  asset_thread_entry,  bus, PRIORITY_LOW);

    // Compositor thread becomes a coordinator
    compositor_create(renderer, camera, bus, input, root);
    // compositor_frame() now orchestrates via task graph
}
```
