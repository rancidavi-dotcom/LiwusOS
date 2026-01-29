# Clipboard and Drag & Drop (Future Architecture)

## Objective

Define the architectural blueprint for clipboard (copy/paste) and drag-and-drop subsystems in the LiwusOS GUI. Both are currently **not implemented** — this document describes the planned design for future integration. The clipboard enables data transfer between applications and widgets via shared buffers. Drag-and-drop provides visual interactive transfer with real-time feedback.

## Problems Solved

- **No data transfer mechanism**: Currently, text selected in one widget cannot be pasted into another.
- **No drag-and-drop**: Users cannot drag files from a file manager to a terminal, or rearrange windows via drag.
- **Multi-format support**: Both clipboard and DND must support plain text, rich text, images, and custom data types.
- **IPC for user-space**: Kernel GUI tasks and future user-space processes must share clipboard data through a controlled interface.
- **Visual feedback**: Drag operations need ghost sprites, drop target highlighting, and cancel animations.

---

# Clipboard Subsystem

## Architecture

### Data Model

```
clipboard_t
 ├── owner_id         (pid or 0 for kernel)
 ├── timestamp        (last modification)
 └── formats[]
      ├── MIME type   (e.g., "text/plain", "text/rtf", "image/png")
      ├── data ptr
      └── size
```

### Ownership

- **Selection owner**: The widget or process that most recently claimed the clipboard via `clipboard_claim()`.
- **Kernel clipboard service**: Owns the buffer. Widgets and user-space tasks call into it.
- **Lost ownership**: When a new owner claims the clipboard, the previous owner receives `GUI_EVENT_CLIPBOARD_LOST`.

### Data Flow

```
Application A                 Clipboard Service (kernel)               Application B
     │                               │                                       │
     │ clipboard_claim()             │                                       │
     │─────────────────────────────►│                                       │
     │                               │                                       │
     │ clipboard_set("text/plain",  │                                       │
     │   "Hello", 6)                │                                       │
     │─────────────────────────────►│                                       │
     │                               │  stores in internal buffer            │
     │                               │                                       │
     │                               │         clipboard_get("text/plain")   │
     │                               │◄──────────────────────────────────────│
     │                               │──────────────────────────────────────►│
     │                               │         "Hello" (copy of buffer)      │
```

### Event Bus Integration

```c
// Clipboard events (future additions to gui_event_type_t)
GUI_EVENT_CLIPBOARD_COPY    = 50,  // Request: copy selection to clipboard
GUI_EVENT_CLIPBOARD_PASTE   = 51,  // Request: paste at cursor position
GUI_EVENT_CLIPBOARD_CUT     = 52,  // Request: cut (copy + delete selection)
GUI_EVENT_CLIPBOARD_LOST    = 53,  // Notification: another owner claimed
GUI_EVENT_CLIPBOARD_CHANGED = 54,  // Notification: clipboard content changed
```

## APIs (Proposed)

### Public

```c
// Initialize clipboard service
void clipboard_init(void);

// Claim clipboard ownership (returns true if successful)
bool clipboard_claim(uint32_t owner_id);

// Release ownership
void clipboard_release(uint32_t owner_id);

// Set clipboard data in a specific format
bool clipboard_set_data(uint32_t owner_id, const char *mime_type,
                        const void *data, uint32_t size);

// Get clipboard data (returns NULL if format not available)
const void *clipboard_get_data(const char *mime_type, uint32_t *out_size);

// Check available formats
uint32_t clipboard_get_format_count(void);
const char *clipboard_get_format_name(uint32_t index);

// Clear clipboard
void clipboard_clear(void);
```

### Supported MIME Types (Initial)

| MIME Type | Description |
|-----------|-------------|
| `text/plain` | UTF-8 plain text |
| `text/rtf` | Rich Text Format |
| `text/uri-list` | File URIs (drag from file manager) |
| `image/argb` | Raw 32-bit ARGB pixels + w/h header |
| `application/x-liwus-node-id` | Internal node reference (DND between widgets) |

---

# Drag and Drop Subsystem

## Architecture

### Drag Session State Machine

```
                    ┌─────────────────────────────┐
                    │         IDLE                │
                    └──────────┬──────────────────┘
                               │ mouse_down on draggable
                               │ + drag threshold exceeded
                               ▼
                    ┌─────────────────────────────┐
                    │     DRAG_START              │
                    │  (create ghost sprite,      │
                    │   capture input exclusive)  │
                    └──────────┬──────────────────┘
                               │
                  ┌────────────┼────────────┐
                  │            │            │
                  ▼            ▼            ▼
        ┌──────────────┐ ┌──────────┐ ┌──────────┐
        │ DRAG_OVER    │ │ DRAG_OVER│ │ DRAG_OVER│
        │ target_A     │ │ target_B │ │ (none)   │
        └──────┬───────┘ └────┬─────┘ └────┬─────┘
               │              │            │
               │ mouse_move   │            │
               └──────────────┘            │
                                           │
                    ┌──────────────────────┘
                    │ mouse_up (on valid target)
                    │ OR Escape key
                    ▼
          ┌───────────────────┐      ┌──────────────────┐
          │     DROP          │      │   DRAG_CANCEL    │
          │ (send data to     │      │ (animate ghost   │
          │  drop target)     │      │  back to origin) │
          └────────┬──────────┘      └────────┬─────────┘
                   │                          │
                   ▼                          ▼
                    ┌────────────────────────┐
                    │         IDLE           │
                    └────────────────────────┘
```

### Drag Session Lifecycle

1. **DRAG_START**: User presses mouse button on a draggable node. After moving > 4 pixels (drag threshold), a session begins:
   - Input is captured exclusively (no other widgets receive events until drop/cancel).
   - A ghost sprite is created — a semi-transparent copy of the dragged node rendered to an offscreen buffer.
   - The drag data package is populated with formats (e.g., `text/uri-list` for a file, `text/plain` for selected text).

2. **DRAG_OVER / DRAG_LEAVE**: As the cursor moves:
   - The compositor hit-tests against registered drop targets.
   - Entering a target: target receives `GUI_EVENT_DRAG_ENTER` with the data package.
   - Moving within a target: target receives `GUI_EVENT_DRAG_OVER`.
   - Leaving a target: target receives `GUI_EVENT_DRAG_LEAVE`.
   - The cursor icon changes based on the target's response (accept/reject).

3. **DROP** / **DRAG_CANCEL**:
   - On mouse up over an accepting target: `GUI_EVENT_DROP` is sent. The target reads the data.
   - On Escape key press or mouse up over empty space: the session cancels, ghost animates back to origin.
   - The ghost is removed, input capture is released.

### Data Package

```c
typedef struct {
    uint32_t    format_count;
    const char *mime_types[DRAG_MAX_FORMATS];  // e.g., {"text/plain", "image/argb"}
    void       *data[DRAG_MAX_FORMATS];
    uint32_t    sizes[DRAG_MAX_FORMATS];
    node_t     *source_node;      // the node that initiated the drag
    uint32_t    source_node_id;   // user-space handle
    int         cursor_offset_x;  // ghost offset from cursor
    int         cursor_offset_y;
} drag_data_t;
```

### Drop Target Registration

```c
typedef struct {
    node_t           *node;           // the target node
    gui_rect_t        hot_rect;       // screen-space hot zone
    const char      **accepted_types; // array of MIME types, NULL-terminated
    uint32_t          accepted_count;
    // Callbacks:
    bool (*on_drag_enter)(node_t *target, const drag_data_t *data);
    void (*on_drag_over)(node_t *target, const drag_data_t *data);
    void (*on_drag_leave)(node_t *target, const drag_data_t *data);
    bool (*on_drop)(node_t *target, const drag_data_t *data);
} drop_target_t;
```

### Event Bus Integration

```c
// Drag & drop events (future additions to gui_event_type_t)
GUI_EVENT_DRAG_START  = 60,  // A drag session has started
GUI_EVENT_DRAG_OVER   = 61,  // Cursor is over a drop target
GUI_EVENT_DRAG_ENTER  = 62,  // Cursor entered a drop target
GUI_EVENT_DRAG_LEAVE  = 63,  // Cursor left a drop target
GUI_EVENT_DROP        = 64,  // Data was dropped on a target
GUI_EVENT_DRAG_CANCEL = 65,  // Drag was cancelled (Escape or click elsewhere)
```

## APIs (Proposed)

### Public

```c
// Initialize DND subsystem
void dnd_init(void);

// Register a node as a drag source
void dnd_register_source(node_t *node);

// Register a node as a drop target with accepted format list
drop_target_t *dnd_register_target(node_t *node, const char **accepted_types);

// Unregister
void dnd_unregister_source(node_t *node);
void dnd_unregister_target(node_t *node);

// Start a drag session programmatically
bool dnd_start_drag(node_t *source, drag_data_t *data);

// Query current drag state
bool dnd_is_dragging(void);
const drag_data_t *dnd_current_data(void);
node_t *dnd_current_target(void);
```

### Widget Integration Example (File Manager → Terminal)

```c
// File manager registers as drag source:
dnd_register_source(file_list_node);

// In file manager's on_event for GUI_EVENT_MOUSE_DOWN:
if (drag_threshold_exceeded) {
    drag_data_t data;
    data.mime_types[0] = "text/uri-list";
    data.data[0] = "file:///home/user/document.txt";
    data.sizes[0] = 36;
    data.source_node = file_list_node;
    dnd_start_drag(file_list_node, &data);
}

// Terminal registers as drop target:
const char *accepted[] = {"text/uri-list", "text/plain", NULL};
drop_target_t *target = dnd_register_terminal(terminal_node, accepted);

// Terminal's on_drop callback parses URIs and runs commands:
bool terminal_on_drop(node_t *target, const drag_data_t *data) {
    const char *uris = (const char *)data->data[0];
    // Run: wget <uri>  or  cd <path>
    return true;
}
```

## Dependencies

- Event bus (for posting drag/clipboard events)
- Renderer (for drawing drag ghost sprite)
- Compositor (for exclusive input capture during drag)
- `kheap.h` (for clipboard buffer allocation)
- `string.h`, `memcpy` (for clipboard data copying)

## Limitations / Trade-offs

| Feature | Trade-off |
|---------|-----------|
| Clipboard data is kernel-owned | All data passes through kernel heap. Large images consume kernel memory. Consider a shared page for large transfers. |
| Synchronous clipboard read | `clipboard_get_data()` returns a pointer to kernel data. Should return a copy to prevent the owner from mutating it. |
| Single drag session | Only one drag operation at a time. Simultaneous drag (multi-touch) requires per-pointer sessions. |
| No cross-screen DND | Drag across multiple monitors not yet considered. Requires compositor coordination. |
| Ghost sprite is a static bitmap | The ghost is captured at drag start. If the source node animates, the ghost does not update. A dynamic ghost would require re-rendering the source each frame. |
| Drop target registration is flat | No spatial acceleration for hit-testing targets. Simple list iteration is fine for < 100 targets. QuadTree integration planned. |

## Performance / Memory Optimizations

- **Clipboard data is lazily converted**: `text/rtf` is only generated when requested. The owner provides a converter callback.
- **Drag ghost is a single `kmalloc`**: Captured as `uint32_t` pixels of the source node bounding box. Freed on drop/cancel.
- **No per-frame allocations**: The clipboard buffer persists until overwritten. The drag data package is stack-allocated (refers to existing node data with pointers).
- **Drop target list**: Sorted by z-order so topmost targets get priority. Insertion is O(N) but targets are seldom added after init.

## Future Extensions

| Feature | Approach |
|---------|----------|
| User-space clipboard IPC | Expose clipboard via `sys_clipboard_get/set` syscalls. Kernel validates user-provided pointers. |
| Clipboard history | Ring buffer of last N clipboard entries. Ctrl+Shift+V cycles through history. |
| Drag preview thumbnails | For file DND, render a scaled icon + filename as the ghost. |
| Spring-loaded folders | Hovering over a folder during drag auto-opens it after a delay. |
| Multi-item drag | Select multiple files → drag all as `text/uri-list` with \n-separated URIs. |
| Cross-process drag | User-space processes send drag data via shared memory pages. Kernel mediates the transfer. |

## Usage Examples

```c
// Clipboard: copy selected text from a terminal
void terminal_copy_selection(node_t *terminal, const char *text, uint32_t len) {
    if (clipboard_claim(terminal->id)) {
        clipboard_set_data(terminal->id, "text/plain", text, len);
        // Terminal may also offer "text/rtf" or "application/x-terminal-cmd"
    }
}

// Clipboard: paste into an input field
void input_field_paste(node_t *field) {
    uint32_t size;
    const void *data = clipboard_get_data("text/plain", &size);
    if (data) {
        input_field_insert_text(field, (const char *)data, size);
    }
}

// Drag-and-drop: window rearrangement
void window_titlebar_on_event(node_t *win, const gui_event_t *e) {
    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        // Start drag session for the window
        drag_data_t d = {0};
        d.mime_types[0] = "application/x-liwus-node-id";
        d.data[0] = &win->id;
        d.sizes[0] = sizeof(win->id);
        d.source_node = win;
        dnd_start_drag(win, &d);
    }
    if (e->type == GUI_EVENT_DROP) {
        // Reorder windows in the scene graph
        uint32_t *dragged_id = (uint32_t *)e->generic.a;
        // ...
    }
}
```
