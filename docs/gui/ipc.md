# IPC for GUI — LiwusOS (Future Architecture)

## Objective

Document the current and planned inter-process communication (IPC) model for the LiwusOS GUI subsystem. Covers how user-space applications communicate with the kernel compositor, how events are delivered, and the shared memory model for pixel data transfer.

---

## Problems Solved

- Current apps have no way to receive keyboard/mouse events from the compositor
- No mechanism for the compositor to signal applications about state changes
- No shared memory protocol for efficient pixel data exchange between kernel and user-space
- The need to transition from the monolithic kernel-compositor model to a service-oriented architecture

---

## Current Architecture (Phase 1)

```
┌──────────────────────────────────────────────────────────────────┐
│                     KERNEL SPACE                                  │
│                                                                   │
│  ┌───────────────────┐    ┌─────────────────────────────┐        │
│  │  User App Process │    │  GUI Compositor (kernel task)│        │
│  │  (e.g., demo_gui) │    │                              │        │
│  │                   │    │  ┌─────────────────────────┐  │        │
│  │  ┌─────────────┐  │    │  │ Event Bus              │  │        │
│  │  │liwus_gui.c  │──┼────┼──┤ Input Manager          │  │        │
│  │  │(syscall     │  │    │  │ Scene Graph            │  │        │
│  │  │ wrappers)   │  │    │  │ Renderer / Framebuffer  │  │        │
│  │  └─────────────┘  │    │  │ Camera                 │  │        │
│  │                   │    │  └─────────────────────────┘  │        │
│  └───────────────────┘    └──────────────────────────────┘        │
└──────────────────────────────────────────────────────────────────┘
```

**Key characteristics:**
- User-space apps issue **syscalls 120–124** (scene graph operations)
- All rendering is **kernel-side** — the compositor task owns the framebuffer
- Apps are **stateless** from a rendering perspective; they create/arrange nodes and the compositor draws them
- **No event delivery** to user-space apps — mouse/keyboard events are consumed entirely within the kernel event bus

---

## Future Architecture (Phase 6 — Planned)

```
┌─────────────────────────────────────────────────────────────────┐
│                      USER SPACE                                    │
│                                                                    │
│  ┌─────────────────┐    ┌───────────────────┐                  │
│  │  App A          │    │  App B             │                    │
│  │  ┌─────────────┐│    │  ┌───────────────┐  │                    │
│  │  │ Event       ││    │  │ Event         │  │                    │
│  │  │ Handler     ││    │  │ Handler       │  │                    │
│  │  └──────┬──────┘│    │  └──────┬────────┘  │                    │
│  │         │       │    │         │             │                    │
│  └─────────┼───────┘    └─────────┼───────────┘                    │
│            │                       │                               │
│     ┌──────┴───────────────────────┴─────────┐                     │
│     │   GUI IPC Library (user-space)        │                     │
│     │   - event channel reader                │                     │
│     │   - shared memory allocator             │                     │
│     │   - scene graph proxy                   │                     │
│     └──────────────────┬─────────────────────┘                     │
│                        │                                            │
├────────────────────────┼────────────────────────────────────────────┤
│                  KERNEL SPACE                                       │
│                        │                                            │
│  ┌─────────────────────┴─────────────────────┐                    │
│  │  GUI Compositor Service                    │                    │
│  │                                            │                    │
│  │  ┌────────────┐  ┌──────────────────────┐  │                    │
│  │  │ IPC Router │  │ Event Distribution  │  │                    │
│  │  │ - per-app   │  │ - filter by PID      │  │                    │
│  │  │   channels  │  │ - route to mem-channels│  │                    │
│  │  └─────┬──────┘  └──────────┬───────────┘  │                    │
│  │        │                    │                 │                    │
│  │  ┌─────┴──────────────────┴────────────┐  │                    │
│  │  │ Shared Memory Pools                        │  │                    │
│  │  │ - one per app                          │  │                    │
│  │  │ - pixel buffer (optional, for apps that  │  │                    │
│  │  │   want custom rendering)               │  │                    │
│  │  └────────────────────────────────────────┘  │                    │
│  └────────────────────────────────────────────┘                    │
└────────────────────────────────────────────────────────────────────┘
```

---

## Message Format for GUI IPC (Planned)

### Header

```c
typedef struct {
    uint32_t magic;          /* 0x4C495755 ("LIWU") */
    uint32_t version;        /* IPC protocol version */
    uint32_t pid;            /* source/destination process ID */
    uint32_t seq;            /* sequence number for ordering */
    uint32_t type;           /* gui_ipc_msg_type_t */
    uint32_t payload_len;    /* bytes following header */
    uint64_t timestamp;      /* monotonic frame count */
} gui_ipc_header_t;
```

### Message Types (Planned)

```c
typedef enum {
    /* App → Compositor */
    GUI_IPC_NODE_CREATE  = 0x100,   /* create a node */
    GUI_IPC_NODE_DESTROY = 0x101,   /* destroy a node */
    GUI_IPC_NODE_MOVE    = 0x102,   /* reposition a node */
    GUI_IPC_NODE_PROP    = 0x103,   /* set node property */
    GUI_IPC_WINDOW_PAINT = 0x110,   /* app wants to paint pixels */
    
    /* Compositor → App */
    GUI_IPC_EVENT_MOUSE  = 0x200,   /* mouse event */
    GUI_IPC_EVENT_KEY    = 0x201,   /* keyboard event */
    GUI_IPC_EVENT_FOCUS  = 0x202,   /* focus change */
    GUI_IPC_EVENT_WIN    = 0x203,   /* window lifecycle (close, resize) */
    GUI_IPC_FRAME_SYNC   = 0x210,   /* vsync / frame ready signal */
    
    /* System */
    GUI_IPC_PING         = 0x300,
    GUI_IPC_PONG         = 0x301,
    GUI_IPC_ERROR        = 0xFFF,
} gui_ipc_msg_type_t;
```

### Payload Examples (Planned)

```c
typedef struct {
    uint32_t node_id;
    int32_t  x, y;
} gui_ipc_node_move_t;

typedef struct {
    uint32_t node_id;
    uint32_t property_id;  /* e.g., OPACITY, VISIBLE, Z_ORDER */
    int64_t  value;
} gui_ipc_node_prop_t;

typedef struct {
    int32_t  x, y;
    int32_t  dx, dy;
    uint8_t  button;       /* 0=none, 1=L, 2=R, 3=M */
    uint8_t  modifiers;    /* bit0=Shift, bit1=Ctrl, bit2=Alt */
} gui_ipc_mouse_event_t;

typedef struct {
    uint8_t  scancode;
    uint32_t keycode;
    uint32_t unicode;
    uint8_t  pressed;      /* 1=down, 0=up */
    uint8_t  modifiers;
} gui_ipc_key_event_t;
```

---

## Shared Memory Model (Planned)

### Pixel Data Transfer

For apps that want to provide raw pixel content (e.g., a terminal emulator widget, image viewer):

1. Compositor allocates a shared memory region per app
2. App writes pixel data to the shared memory
3. App posts a `GUI_IPC_WINDOW_PAINT` message referencing the shared memory offset and dirty rect
4. Compositor blits from shared memory into the scene graph node's content

```c
typedef struct {
    uint64_t shm_key;       /* identifies the shared memory region */
    int32_t  dirty_x, dirty_y;
    int32_t  dirty_w, dirty_h;
    uint32_t format;        /* 0=ARGB8888, 1=RGB565, etc. */
} gui_ipc_paint_t;
```

### Allocation

```c
// Kernel service (future)
void *gui_ipc_alloc_shm(uint32_t pid, size_t size);

// User-space library (future)
void *gui_ipc_map_shm(void *shm_key, size_t size);
```

---

## Event Delivery to User-Space (Planned)

### Current Status

**No event delivery.** Events are consumed entirely within the kernel: `input_manager_poll()` → `event_bus_post()` → widgets/tools/handlers.

### Planned Flow

```
Mouse/Keyboard IRQ → Input Driver
  → input_manager_poll()
    → event_bus_post() [kernel event bus]
      → dispatcher checks if target node has user-space app binding
        → [kernel bus] → kernel handlers (tools, focus, window manager)
        → [kernel → user] → serialize event → write to app's IPC channel
          → user-space library deserializes → calls app's event handler
```

### Delivery Channels

Two options (architecture decision pending):

1. **Per-app kernel ring buffer** (similar to Linux evdev):
   - Each app gets a kernel ring buffer readable via `mmap()` or a new syscall
   - Compositor writes events into the app's ring buffer
   - App polls or blocks on the ring buffer

2. **Shared memory event queue:**
   - Same-model as the kernel event bus but in user-accessible memory
   - Compositor writes events as lock-free producer, app reads as consumer
   - No syscall overhead for event reading

---

## Window Painting Model

### Current (Phase 1)

```
App sets node properties via syscall
  → Kernel allocates/updates node_t
    → Compositor walks scene graph
      → node_vtable->draw() writes to backbuffer
        → renderer_present() flips to VRAM
```

The app never touches pixels — it only defines the scene graph structure.

### Future (Phase 5–6)

```
Hybrid model:
  Option A: Scene graph nodes (simple widgets)
    → Same as current: kernel renders

  Option B: App-owned pixel buffer (complex rendering)
    → App allocates shared memory → App renders pixels with its own code
    → App posts paint message with dirty rect
    → Compositor blits from shared memory to the off-screen buffer
    → Compositor flips to VRAM
```

---

## IPC Flow Diagram (Planned)

```
 ┌─────────┐   syscall 120-124   ┌────────────┐
 │  App A  │────────────────────▶│  Kernel     │
 │         │  (create, add,      │  Compositor │
 │         │   move, zoom)       │             │
 │         │                     │  ┌─────────┐│
 │  ┌──────┴──┐                  │  │ Scene   ││
 │  │Event    │  ←─── IPC ───────│  │ Graph   ││
 │  │Callback │  (mouse move  at │  └─────────┘│
 │  │         │    x=100,y=200   │             │
 │  └─────────┘                  │  ┌─────────┐│
 │                               │  │Camera   ││
 │         ┌─────────┐           │  └─────────┘│
 │         │Shared   │◄──────────│             │
 │         │Memory   │  (pixel   │  ┌─────────┐│
 │         │Region   │   data)   │  │Renderer ││
 │         └─────────┘           │  └─────────┘│
 └──────────────────────────────┘             ┘
```

---

## Dependencies

- **Kernel:** syscall dispatcher, task scheduler (IPC channels), VMM (shared memory mapping)
- **Compositor:** event routing logic, per-app channel manager
- **User-space library:** `gui_ipc.h`, ring buffer reader, event dispatcher
- **Initrd:** IPC configuration/service discovery

---

## Limitations & Trade-offs

| Concern | Current | Future Solution |
|---|---|---|
| Event latency for user-space | N/A (no events) | Sub-frame latency via shared memory event queue |
| Scheduling overhead | Zero IPC | Minimal — lock-free ring buffers, no syscall per event |
| Security | Full kernel trust | Shared memory regions isolated by VMM; typed IPC messages validated |
| Complexity | Simple syscall model | IPC router, shared memory allocator, protocol versioning |
| Debugging | Serial prints in kernel | IPC monitor tool |

---

## Future Extensions

| Extension | Description | Phase |
|---|---|---|
| App lifecycle events | Compositor notifies app on focus/blur/close | Phase 6 |
| Remote display protocol | Network transparency: compositor serializes scene graph changes over TCP | Phase 6 |
| Multiple views per app | App can have multiple windows/viewports | Phase 5 |
| Input method protocol | IME integration for text input | Phase 6 |
| Drag-and-drop between apps | Cross-app drag via IPC channel | Phase 6 |
| Clipboard/selection | Shared clipboard between apps | Phase 6 |