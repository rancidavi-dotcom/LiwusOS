# Command System — Future Architecture for Undo/Redo

## Objective

Define a **Command Pattern** abstraction that translates Event Bus signals into reversible, composable command objects. The Command System will sit between the Event Bus and the Scene Graph / Camera, providing undo/redo capability, transaction grouping, and macro recording for canvas operations.

**Current status**: Not yet implemented. Events are processed directly by tools (PanTool, SelectTool, MoveTool) which mutate camera and node state inline. This document outlines the planned architecture.

## Problems Solved (Future)

- **Undo/Redo**: canvas operations (move node, pan camera, resize window) are not reversible today. Commands store `execute()` and `undo()` closures, enabling a stack-based undo system.
- **Transaction grouping**: complex operations (group move, batch resize) should be atomic — one undo undoes the entire transaction.
- **Macro recording**: user actions can be recorded as a sequence of commands and replayed or exported.
- **Separation of intent from execution**: tools emit commands rather than directly mutating state, making the system testable and auditable.
- **Deferred execution**: commands can be queued, scheduled, or transmitted to remote displays.

## Architecture (Planned)

```
  Event Bus                    Command System                   Scene / Camera
┌──────────────┐          ┌──────────────────────┐          ┌──────────────┐
│ MouseEvent   │ ──────▶  │  CommandInterpreter  │ ──────▶  │ camera_pan() │
│ KeyEvent     │          │  (translates events  │          │ node_set_    │
│ WindowEvent  │          │   to commands)       │          │ position()   │
└──────────────┘          │                      │          └──────────────┘
                          │  ┌────────────────┐  │
                          │  │ CommandQueue   │  │
                          │  │ [execute]──────┼──┼──────▶ execute()
                          │  │ [undo]─────────┼──┼──────▶ undo()
                          │  └────────────────┘  │
                          │                      │
                          │  ┌────────────────┐  │
                          │  │ UndoStack      │  │
                          │  │ [push] [pop]   │  │
                          │  │ [undo] [redo]  │  │
                          │  └────────────────┘  │
                          │                      │
                          │  ┌────────────────┐  │
                          │  │ Transaction    │  │
                          │  │ [begin] [end]  │  │
                          │  └────────────────┘  │
                          └──────────────────────┘
```

### Event → Command Translation

Today, tools subscribe to the Event Bus directly:

```
ToolManager (subscriber)
  └─ PanTool.on_event() → camera_pan(), camera_zoom_at()
  └─ MoveTool.on_event() → node_set_position()
  └─ SelectTool.on_event() → node_hit_test(), selection state
```

In the future, tools will emit `gui_command_t` objects instead:

```
ToolManager (subscriber)
  └─ PanTool.on_event() → command_push(CmdPan(dx, dy))
  └─ MoveTool.on_event() → command_push(CmdMoveNode(node_id, old_x, old_y, new_x, new_y))
  └─ SelectTool.on_event() → command_push(CmdSelect(node_id))
       │
       ▼
  CommandQueue → execute() → mutate camera/node
  UndoStack   ← push(command)
```

## Command Interface (Proposed)

```c
typedef struct gui_command gui_command_t;

typedef struct {
    const char *name;                           /* for debug/history UI */
    bool     (*execute)(gui_command_t *self);   /* returns false on failure */
    bool     (*undo)(gui_command_t *self);      /* returns false on failure */
    void     (*destroy)(gui_command_t *self);   /* free command resources */
    size_t   (*size)(void);                     /* memory footprint hint */
} gui_command_vtable_t;

struct gui_command {
    const gui_command_vtable_t *vtable;
    bool    done;          /* true after execute(), reset on undo() */
    uint32_t timestamp;    /* frame number for ordering */
};
```

## Command Types (Proposed)

| Command | Payload | Effect |
|---|---|---|
| `CmdMoveNode` | `node_id, old_x/y, new_x/y` | Translate a node in world space |
| `CmdResizeNode` | `node_id, old_w/h, new_w/h` | Resize a node |
| `CmdDeleteNode` | `node_id, serialized_state` | Remove node from scene (stored for undo) |
| `CmdCreateNode` | `node_type, parent_id, params` | Insert new node in scene |
| `CmdPanCamera` | `old_pos_fp, new_pos_fp` | Move camera position |
| `CmdZoomCamera` | `old_zoom_fp, new_zoom_fp, pivot` | Change zoom level |
| `CmdGroup` | `cmd_count, cmd_array` | Composite — undo/redo all children atomically |
| `CmdMacro` | `name, cmd_count, cmd_array` | Named sequence for replay |

## Command Queue (Proposed)

```c
#define CMD_QUEUE_CAPACITY 256

typedef struct {
    gui_command_t *queue[CMD_QUEUE_CAPACITY];
    uint32_t head, tail, count;
} command_queue_t;

bool command_queue_push(command_queue_t *q, gui_command_t *cmd);
gui_command_t *command_queue_pop(command_queue_t *q);
void command_queue_flush(command_queue_t *q);  /* execute all pending */
```

Commands are pushed by tools during event dispatch. The compositor calls `command_queue_flush()` at the start of each frame (after event dispatch, before render) to execute them in order.

## Undo/Redo Stack (Proposed)

```c
#define UNDO_STACK_DEPTH 256

typedef struct {
    gui_command_t *undo_stack[UNDO_STACK_DEPTH];
    uint32_t undo_count;
    gui_command_t *redo_stack[UNDO_STACK_DEPTH];
    uint32_t redo_count;
    uint32_t current_group;  /* transaction nesting counter */
} undo_manager_t;

void undo_manager_push(undo_manager_t *um, gui_command_t *cmd);
bool undo_manager_undo(undo_manager_t *um);
bool undo_manager_redo(undo_manager_t *um);
void undo_manager_begin_group(undo_manager_t *um);
void undo_manager_end_group(undo_manager_t *um);
```

### Transaction Grouping

```
undo_manager_begin_group(um);        // nesting counter ++
  cmd = CmdMoveNode(id, 10, 20);
  undo_manager_push(um, cmd);
  cmd = CmdResizeNode(id, 100, 200);
  undo_manager_push(um, cmd);
undo_manager_end_group(um);         // nesting counter --
```
A single undo pops all commands in the group as a `CmdGroup`.

## Relationship to Event Bus

The command interpreter would be a subscriber to the Event Bus:

```
event_bus_subscribe(bus, GUI_EVENT_NONE, command_interpreter, cmd_system);
```

Where `command_interpreter` filters relevant input events and pushes commands to the queue. This replaces the current ToolManager approach where tools directly mutate state.

However, not all event types generate commands:
- `MOUSE_MOVE` → no command (transient)
- `MOUSE_DOWN` on canvas → `CmdSelect` or `CmdDeselect`
- `KEY_DOWN` (H) → `CmdResetCamera` (grouped with inertia)
- `WIN_CLOSE` → `CmdDeleteNode`

Immediate (non-undoable) events like hover, cursor changes, scroll feedback still go through the current direct-dispatch path.

## Dependencies (Future)

- `event_bus.h` — source of commands (interpreter subscribes to bus)
- `scene/node.h` — node mutation APIs
- `scene/camera.h` — camera mutation APIs
- `kheap.h` — command memory management
- `string.h` — serialised payloads for undo state

## Limitations / Trade-offs

| Limitation | Rationale |
|---|---|
| Additional memory per command | Each command allocates its payload. A `CmdMoveNode` costs ~48 bytes; deep undo stacks may use ~12 KB. Mitigation: `size()` method for budget tracking. |
| Time-travel debugging complexity | Undo of side effects (socket writes, file I/O) is non-trivial. The initial scope is limited to canvas/camera operations. |
| Performance overhead | Command construction + vtable dispatch adds latency vs direct mutation. Acceptable because user input rate is << 100 events/s. |
| Not all actions are undoable | Scroll, cursor movement, transient highlights never enter the undo stack. |
| Composite command serialisation | Serialising subtree state for undo of `CmdDeleteNode` requires copying the node's full subtree. Maximum subtree depth is capped by `NODE_MAX_CHILDREN` (64) and depth limits. |

## Memory Budget

| Component | Size | Count | Total |
|---|---|---|---|
| Command queue | 4 bytes (ptr) | 256 | 1 KB |
| Undo stack | 4 bytes (ptr) | 256 | 1 KB |
| Redo stack | 4 bytes (ptr) | 256 | 1 KB |
| CmdMoveNode instance | ~48 bytes | 256 | 12 KB (peak) |
| CmdGroup wrapper | ~40 bytes | 32 | 1.3 KB |

Total estimated: **~16 KB** peak for full undo/redo with 256 entries.

## Future Extensions

1. **Macro recorder**: a `CmdMacro` collects all commands between `record_start()` / `record_stop()` and stores them as a named, replayable sequence.
2. **Command merging**: consecutive `CmdMoveNode` on the same node collapse into a single command containing the net delta.
3. **Predicate-based undo**: `undo_manager_can_undo(type)` enables context-sensitive undo (e.g., "undo last camera move only").
4. **Remote replay**: commands serialised to a byte stream for multiplayer or demo recording.
5. **Visual history panel**: display undo stack as a list in the UI, allowing random-access undo to any point.

## Relationship to Current Architecture

```
Current (as of build):
  Event → Tool.on_event() → state mutation (no history)

Planned:
  Event → Tool.on_event() → Command → execute() → state mutation
                                          ↓
                                    UndoStack ← push()
```

Tools will continue to exist but will construct and push commands rather than calling camera/node APIs directly. The concrete tool implementations (`pan_tool.c`, `move_tool.c`, `select_tool.c`) already have clean separation — the mutation calls are isolated, making them straightforward to wrap in command objects.

---

*Document v1.0 — describes the planned Command System architecture. Not yet implemented in the current LiwusOS build.*
