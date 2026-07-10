# Synchronization Primitives

## Objective

Define the concurrency control mechanisms for the LiwusOS GUI subsystem, covering the current single-threaded model (no synchronization needed), the event bus ring buffer design, and the future multi-threaded synchronization strategy using spinlocks, atomic operations, and lock-free data structures.

## Problems Solved

- **Current safety**: All GUI code runs in a single kernel task (`gui_compositor_task`). No concurrent access to shared state. No synchronization primitives needed — but the architecture must prepare for threading.
- **Lock-free event posting**: Interrupt handlers (IRQs from mouse/keyboard) cannot acquire spinlocks. The event bus ring buffer is already lock-free for single-producer (IRQ) / single-consumer (compositor).
- **Backbuffer integrity**: Render commands must not overlap with `present()`. Double-buffering with a swap fence resolves this.
- **Transactional scene graph**: Batch Node mutations (add/remove/reparent) so that the render thread sees a consistent tree.
- **Future-proofing**: The design must allow incremental addition of spinlocks, atomics, and lock-free queues without rewriting the core.

## Architecture

### Current Synchronization State

```
Kernel Task: gui_compositor_task()
  │
  ├── input_manager_poll()     ─── reads hardware (no locks)
  ├── event_bus_dispatch()     ─── single consumer
  ├── camera_update()          ─── no locks
  ├── node_update_transforms() ─── walks tree (no locks)
  ├── node_draw_recursive()    ─── reads node state (no locks)
  └── renderer_present()      ─── memcpy to VRAM (no locks)
```

**No locks are taken anywhere**. The event bus ring buffer uses a simple `head/tail/count` scheme that is safe for single-producer, single-consumer without atomics (on x86, aligned 32-bit writes are atomic).

### Event Bus Ring Buffer (Lock-Free for SPSC)

```
struct gui_event_bus {
    gui_event_t   ring[RING_CAPACITY];  // 256 entries
    uint32_t      head;                 // next write position (producer)
    uint32_t      tail;                 // next read position  (consumer)
    uint32_t      count;                // number of events pending
};
```

**Producer** (interrupt handler or input thread):
```c
bool event_bus_post(gui_event_bus_t *bus, const gui_event_t *event) {
    if (bus->count >= RING_CAPACITY) return false;  // drop on overflow
    bus->ring[bus->head] = *event;       // write event
    bus->head = (bus->head + 1) % RING_CAPACITY;  // advance head
    bus->count++;                          // publish
    return true;
}
```

**Consumer** (compositor task):
```c
uint32_t event_bus_dispatch(gui_event_bus_t *bus) {
    // Drain ring into local scratch
    while (bus->count > 0) {
        bus->pending[bus->pending_count++] = bus->ring[bus->tail];
        bus->tail = (bus->tail + 1) % RING_CAPACITY;
        bus->count--;
    }
    // Sort by priority, dispatch handlers
    // ...
}
```

**Safety justification**: On x86, aligned 32-bit stores are atomic. The producer writes `ring[head]`, then increments `head` and `count`. The consumer reads `count`, then reads `ring[tail]`. Because `count` acts as a memory barrier (data dependency ordering), the consumer will see the fully written event. This is NOT safe for multi-producer but is safe for SPSC.

### Future Synchronization Matrix

| Resource | Current | Future Protection | Type |
|----------|---------|-------------------|------|
| Event bus ring buffer | Lock-free SPSC | Lock-free SPSC (same) | No change needed |
| Event bus subscriber table | No protection | Spinlock | Write: subscribe/unsubscribe. Read: dispatch. |
| Scene graph (tree structure) | No protection | Batch spinlock (transactional) | Write: add/remove/reparent. Read: render walk. |
| Node properties (`local_x/y`, `width`, `height`) | No protection | Per-node atomic or spinlock | Layout thread writes, render thread reads. |
| Backbuffer (`fb_state_t::backbuf`) | Single buffer | Double buffer + present fence | Render thread writes, compositor presents. |
| Theme palette (`s_palette[]`) | No protection | Spinlock or rwlock | Rare writes (runtime theme change), frequent reads. |
| Animation engine (`s_animations[]`) | No protection | Thread-local (owned by anim thread) | No sharing needed. |
| Asset cache (`s_default_font[]`) | No protection | Rwlock | Write once at init, read every frame. |
| Cursor save buffer | No protection | No sharing (render thread only) | Not accessed by other threads. |

### Double-Buffering (Backbuffer Swap)

```
Frame N:
  Render Thread writes to backbuf_A
  Compositor reads backbuf_A for cursor overlay
  Compositor calls present() ─── memcpy backbuf_A → VRAM

Frame N+1:
  Render Thread writes to backbuf_B
  Compositor presents backbuf_B

Synchronization:
  fence: render completes before present starts
  "swap ready" flag toggled by compositor after present()
```

### Transactional Scene Graph Updates

Batch node mutations to provide a consistent view to the render thread:

```c
void scene_graph_begin_transaction(void) {
    spinlock_acquire(&g_scene_lock);
}

void scene_graph_commit_transaction(void) {
    // Validate tree (no cycles, valid parent pointers)
    // Rebuild spatial index
    // Mark affected nodes dirty
    spinlock_release(&g_scene_lock);
}
```

All `node_add_child`, `node_remove_child`, `node_destroy` are deferred until `scene_graph_commit_transaction()`.

## APIs

### Current (no locks needed)

```c
// Event bus — already thread-safe for SPSC
bool event_bus_post(gui_event_bus_t *bus, const gui_event_t *event);
uint32_t event_bus_dispatch(gui_event_bus_t *bus);
```

### Future (proposed)

```c
// Spinlock
typedef struct { volatile uint32_t locked; } spinlock_t;
void spinlock_init(spinlock_t *l);
void spinlock_acquire(spinlock_t *l);
void spinlock_release(spinlock_t *l);

// Read-write lock
typedef struct { spinlock_t lock; uint32_t readers; } rwlock_t;
void rwlock_acquire_read(rwlock_t *l);
void rwlock_release_read(rwlock_t *l);
void rwlock_acquire_write(rwlock_t *l);
void rwlock_release_write(rwlock_t *l);

// Atomic operations (GCC builtins)
#define atomic_inc(p)     __sync_fetch_and_add(p, 1)
#define atomic_dec(p)     __sync_fetch_and_sub(p, 1)
#define atomic_cas(p, o, n) __sync_val_compare_and_swap(p, o, n)

// Transactional scene graph
void scene_graph_begin_transaction(void);
void scene_graph_commit_transaction(void);

// Double-buffer swap
void renderer_swap_buffers(gui_renderer_t *r);
void renderer_wait_for_present(gui_renderer_t *r);
```

## Dependencies

- GCC atomic builtins (`__sync_*`) — no platform-specific assembly needed for initial implementation.
- Future: inline assembly for `pause` instruction (spinlock backoff), `mfence`/`lfence` for memory ordering.

## Limitations / Trade-offs

| Approach | Trade-off |
|----------|-----------|
| SPSC ring buffer | Not safe for multi-producer. If multiple interrupt handlers post events concurrently, events may be lost. Mitigation: input thread aggregates IRQ events before posting. |
| Per-node spinlocks | Memory overhead (one lock per node = ~4 bytes + padding). Consider a hash-based lock table for sparse locking. |
| Double buffering | Doubles backbuffer memory (2 × 8 MB = 16 MB for 1920×1080). Acceptable for the benefit of race-free rendering. |
| Transactional scene graph | All mutations are batched. Interactive drag operations must commit every frame to provide visual feedback. |
| No RCU | Read-Copy-Update would allow lock-free scene graph reads. Complexity is high — deferred for later. |

## Future Extensions

| Feature | Approach |
|---------|----------|
| Lock-free multi-producer queue | `uint32_t` head with CAS (compare-and-swap) for multi-producer event posting. |
| RCU for scene graph | `synchronize_rcu()` grace period after node removal. Render thread never blocks. |
| Fence-based present sync | Render thread signals semaphore when backbuffer is complete; compositor blocks on semaphore before present. |
| Per-frame timing | Track lock contention stats (`spinlock_acquire` counts, wait cycles) per frame for profiling. |

## Usage Examples

```c
// Current: zero synchronization code
void compositor_frame(compositor_t *c) {
    input_manager_poll(c->input);       // no lock
    event_bus_dispatch(c->bus);         // no lock (single consumer)
    node_update_transforms(c->scene_root, transform_identity()); // no lock
    node_draw_recursive(c->scene_root, c->renderer); // no lock
    renderer_present(c->renderer);      // no lock
}

// Future: multi-threaded with synchronization
void render_thread_frame(void) {
    spinlock_acquire(&g_scene_lock);
    // Snapshot nodes that need rendering
    // Copy positions atomically
    spinlock_release(&g_scene_lock);

    render_commands_execute();
    renderer_swap_buffers(g_renderer);  // atomic pointer swap
}
```
