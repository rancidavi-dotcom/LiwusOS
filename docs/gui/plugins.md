# Plugin System — LiwusOS GUI (Future Architecture)

## Objective

Document the planned plugin system for extending the LiwusOS GUI with custom widget types, tools, themes, and scripting support. Covers plugin lifecycle, API, sandboxing, and the integration path using the existing Lua scripting engine (`third_party/lua`).

---

## Problems Solved

- **Widget extensibility:** Currently all widget types (button, label, panel, window) are hard-coded in `src/kernel/gui/widgets/`. Adding a new widget requires modifying kernel code and recompiling the entire kernel.
- **Third-party contributions:** Without a plugin system, external developers cannot add custom GUI elements without kernel access.
- **Runtime theme loading:** Theme colors are currently a static palette initialized in `theme_engine_init()` — no runtime customization.
- **Tool extensibility:** The three hardcoded tools (pan, select, move) cannot be extended or replaced without kernel recompilation.

---

## Current Architecture

```
src/kernel/gui/widgets/
├── button.c         # NODE_BUTTON — hardcoded vtable
├── label.c          # NODE_LABEL — hardcoded vtable
├── panel.c          # NODE_PANEL — hardcoded vtable
├── window_node.c    # NODE_WINDOW — hardcoded vtable
```

All widgets are compiled into the kernel binary. `node_vtable_t` is defined at compile time and cannot be extended at runtime.

---

## Future Plugin Architecture

### High-Level Design

```
┌─────────────────────────────────────────────┐
│           PLUGIN HOST (Kernel GUI)           │
│                                              │
│  ┌──────────────────────────────────────────┐│
│  │ Plugin Registry                          ││
│  │  ├─ widget_types[]                       ││
│  │  ├─ tools[]                              ││
│  │  ├─ themes[]                             ││
│  │  └─ scripts[]                            ││
│  └──────────┬───────────────────────────────┘│
│             │                                 │
│  ┌──────────▼───────────────────────────────┐│
│  │ Plugin Loader                            ││
│  │  │                                        ││
│  │  ├── Native Plugin loader (.elf)          ││
│  │  │    → load ELF, resolve symbols          ││
│  │  │    → call plugin_init()                 ││
│  │  │    → register widget_types/tools        ││
│  │  │                                          ││
│  │  └── Lua Script loader                     ││
│  │       → luaL_loadfile()                     ││
│  │       → call script_main()                  ││
│  │       → register via Lua API bindings       ││
│  └────────────────────────────────────────────┘│
│                                                 │
│  ┌──────────────────────────────────────────┐  │
│  │ Plugin Sandbox                           │  │
│  │  - restricted syscall whitelist           │  │
│  │  - memory budget                          │  │
│  │  - frame budget (milliseconds per tick)   │  │
│  └──────────────────────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

---

## Plugin Lifecycle

```
                ┌──────────────┐
                │  Load (ELF)  │
                │  or Lua src  │
                └──────┬───────┘
                       ▼
                ┌──────────────┐
                │   init()     │  ← plugin_interface_t.init()
                │   (setup)    │
                └──────┬───────┘
                       ▼
                ┌──────────────┐
                │  register()  │  ← plugin_interface.register()
                │  - widget    │     register_node_type()
                │  - tool      │     register_tool()
                │  - theme     │     register_theme()
                └──────┬───────┘
                       ▼
             ┌──────────────────┐
             │    Run (active)  │
             │  - handle events  │
             │  - animation      │
             │  - custom draws   │
             └──────┬───────────┘
                    ▼
             ┌──────────────┐
             │  unload()    │
             │  (cleanup)   │
             └──────────────┘
```

### API Functions

```c
typedef struct {
    const char *name;           /* plugin name */
    const char *version;        /* semver string */
    
    /* Called right after loading. Plugin allocates its state here. */
    bool (*init)(void **state_out);
    
    /* Called after init. Plugin registers types/tools/themes. */
    bool (*register_types)(void *state);
    
    /* Called every compositor frame (optional). */
    void (*tick)(void *state, uint64_t frame_number);
    
    /* Called on unload. Plugin frees all resources. */
    void (*unload)(void **state);
} plugin_interface_t;
```

---

## Plugin API (Planned)

### Registration Functions

```c
/* Register a new node type that kernel widgets can instantiate.
 * Returns a node_type_id >= GUI_PLUGIN_TYPE_START, or 0 on failure.
 */
int register_node_type(const char *name, const node_vtable_t *vtable);

/* Register a tool that will be added to the tool manager.
 * Returns a tool_id, or 0 on failure.
 */
int register_tool(const tool_vtable_t *vtable, int priority);

/* Register a theme palette override.
 * Returns a theme_id, or 0 on failure.
 */
int register_theme(const char *name, const uint32_t palette[THEME_COLOR_MAX]);
```

### Lifecycle Functions

```c
/* Load a plugin from an ELF binary. Returns plugin_id or 0. */
uint32_t plugin_load_elf(const char *path);

/* Load a Lua script as a plugin. Returns plugin_id or 0. */
uint32_t plugin_load_lua(const char *path);

/* Unload a plugin by ID. Ensures all registered types/tools are cleaned up. */
void plugin_unload(uint32_t plugin_id);

/* Get the current count of loaded plugins. */
uint32_t plugin_count(void);
```

### Query Functions

```c
/* List all loaded plugin names. Returns number written to buffer. */
uint32_t plugin_list(char *buf, uint32_t bufsize);

/* Get plugin status info. */
bool plugin_info(uint32_t id, plugin_info_t *info);
```

---

## Plugin Isolation (Sandboxing) — Planned

### Memory Budget

```c
typedef struct {
    uint64_t heap_max;         /* maximum kmalloc bytes */
    uint64_t heap_current;     /* current usage */
    uint32_t node_max;         /* maximum node_t allocations */
    uint32_t node_current;     /* current node count */
} plugin_budget_t;
```

### Frame Budget

```c
typedef struct {
    uint64_t tick_ns_max;      /* max microseconds per tick() call */
    uint64_t tick_ns_current;  /* last tick duration */
    uint32_t draw_count_max;    /* max draw calls per frame */
    uint32_t draw_count_current;
} plugin_frame_budget_t;
```

### Syscall Filter

- Native plugins (ELF) run in kernel mode via plugin loader; syscalls are vetted
- Lua scripts run in the Lua sandbox; only exposed API functions are callable
- All plugins have restricted access to:
  - Memory via budgeted kmalloc wrapper
  - File system via read-only VFS window (plugin directory)
  - No direct hardware access (no mouse/keyboard driver calls)

---

## Node Type Extension

Plugins can define new `node_type_t` values starting at `NODE_PLUGIN_BASE = 128`:

```c
#define NODE_PLUGIN_BASE  128
#define NODE_PLUGIN_MAX   255

#define NODE_SLIDER    128  /* example plugin type */
#define NODE_PROGRESS  129  /* example plugin type */
#define NODE_TREEVIEW  130  /* example plugin type */
```

The plugin provides a `node_vtable_t`:

```c
static const node_vtable_t slider_vtable = {
    .draw     = slider_draw,
    .on_event = slider_on_event,
    .layout   = NULL,
    .destroy  = slider_destroy,
};

int slider_type_id = register_node_type("Slider", &slider_vtable);
```

---

## Lua Scripting Integration

### Existing Resource

The codebase already has `third_party/lua/` — Lua 5.x sources can be compiled into the kernel or loaded as a plugin host.

### Exposed Lua API (Planned)

```lua
-- Create a canvas (window)
local canvas = gui.canvas_create(400, 300, "Lua Script")

-- Create widgets
local btn = gui.button_create("Click Me")
gui.node_move(btn, 20, 50)
gui.canvas_add(canvas, btn)

-- Event handler
gui.on_event(canvas, "click", function(node, x, y)
    print("Canvas clicked at", x, y)
end)

-- Camera control
gui.camera_zoom(1.5)

-- Custom theme override
gui.theme_set("THEME_COLOR_BACKGROUND", 0xFF112233)
```

### Lua State Management

- One Lua state per plugin script
- Coroutine-based yield to prevent blocking the compositor
- Garbage-collected userdata for node handles
- Type-checked API calls (Lua → C binding validates node IDs)

---

## Plugin Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     KERNEL SPACE                              │
│                                                               │
│  ┌─────────────────────────┐  ┌──────────────────────────┐  │
│  │  Plugin Manager          │  │  Lua Runtime             │  │
│  │  ┌─────────────────────┐ │  │  ┌────────────────────┐ │  │
│  │  │ Plugin ELF Loader   │ │  │  │ lua_State *L       │ │  │
│  │  │ → ELF parsing        │ │  │  │ refs to registered │ │  │
│  │  │ → symbol resolution  │ │  │  │   functions         │ │  │
│  │  │ → init/register call │ │  │  └────────────────────┘ │  │
│  │  └─────────────────────┘ │  └──────────────────────────┘  │
│  │  ┌─────────────────────┐ │                                 │
│  │  │ Native Plugin Foo   │ │  ┌──────────────────────────┐  │
│  │  │ init() → ok         │ │  │  Lua Plugin Bar           │  │
│  │  │ register:            │ │  │  script: lua/bar.lua      │  │
│  │  │  - "Slider" widget   │ │  │  registered:              │  │
│  │  │  - "Custom Tool"     │ │  │  - "ProgressBar" widget  │  │
│  │  └─────────────────────┘ │  └──────────────────────────┘  │
│  └─────────────────────────┘                                 │
│                                                               │
│  ┌──────────────────────────────────────────────────────────┐│
│  │  Plugin Registry                                          ││
│  │  ┌──────┬─────────┬──────────┬──────────┬─────────────┐ ││
│  │  │  ID  │  Name   │  Type    │  State   │  Budget      │ ││
│  │  ├──────┼─────────┼──────────┼──────────┼─────────────┤ ││
│  │  │  1   │  Slider │  Native  │  Active  │  64KB/1ms    │ ││
│  │  │  2   │  ScriptB│  Lua     │  Active  │  32KB/1ms    │ ││
│  │  └──────┴─────────┴──────────┴──────────┴─────────────┘ ││
│  └──────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

---

## Example: Native Plugin Skeleton

```c
// plugin_slider.c — compiled as a separate ELF, loaded by plugin manager
#include <gui/plugin_api.h>

typedef struct {
    int min_val, max_val;
    int current_val;
    bool dragging;
} slider_data_t;

static void slider_draw(node_t *self, gui_renderer_t *r) {
    slider_data_t *d = (slider_data_t *)self->userdata;
    // ... draw track, thumb, label
}

static bool slider_on_event(node_t *self, const gui_event_t *e) {
    // ... handle click/drag to change value
}

static const node_vtable_t slider_vtable = {
    .draw     = slider_draw,
    .on_event = slider_on_event,
    .destroy  = slider_destroy,
};

bool init(void **state) {
    *state = NULL;
    return true;
}

bool register_types(void *state) {
    int type_id = register_node_type("Slider", &slider_vtable);
    return type_id != 0;
}

void unload(void **state) {
    // cleanup
}
```

---

## Dependencies

- **Kernel ELF loader:** Must support loading plugin `.elf` files into kernel space (use existing `elf.h` parser)
- **Kernel symbols:** Plugin symbol resolution requires kernel symbol table for linking
- **Lua runtime:** `third_party/lua/` must be compiled as part of the kernel or as a loadable module
- **Plugin format:** ELF64 with exported `plugin_interface_t` symbol
- **Plugin API header:** `kernel/include/gui/plugin.h` — defines all registration functions

---

## Limitations & Trade-offs

| Concern | Mitigation |
|---|---|
| **Kernel stability:** A buggy plugin can crash the entire system | Sandbox budgets (memory, frame time); whitelist-based syscall filtering |
| **Binary size:** Kernel with Lua runtime + plugin loader increases binary size | Lua can be compiled separately and loaded on demand; plugin loader is minimal |
| **Performance:** Plugin tick() adds overhead to compositor frame loop | Frame budget cap; plugins are skipped if they exceed their time budget |
| **Security:** Malicious plugin could access kernel memory | Read-only VFS window; no direct hardware access; memory isolation via VMM |
| **ABI compatibility:** Plugin compiled against older kernel headers may break | API version checks on load; `plugin_interface_t` includes version field |
| **Lua GC pauses:** Garbage collection could cause frame drops | Incremental GC mode; yield strategy in Lua-C bindings |

---

## Future Extensions

| Extension | Description | Phase |
|---|---|---|
| Hot-reload plugins | Unload + reload plugin without kernel reboot | Post-Phase 6 |
| Plugin marketplace | Remote repository of plugins, downloaded via initrd | Post-Phase 6 |
| Visual plugin editor | GUI tool to create/modify plugins visually | Post-Phase 6 |
| Dependency resolution | Plugins can declare dependencies on other plugins | Post-Phase 6 |
| Multi-language support | Python (via `third_party/micropython`), Wasm | Post-Phase 6 |

---

## Execution and Priority

Currently, this entire system is **Future Architecture** — Phase 6 or later. The kernel has no plugin loading code at present. All widget types are hardcoded in `src/kernel/gui/widgets/`.