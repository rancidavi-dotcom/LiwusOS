/*
 * gui/core/event_bus.h
 *
 * Typed, prioritised Event Bus for the LiwusOS GUI.
 *
 * Design:
 *   ┌─────────────┐    post()    ┌──────────────┐   dispatch()   ┌──────────────┐
 *   │ InputManager│ ──────────▶ │  EventQueue  │ ─────────────▶ │  Subscriber  │
 *   └─────────────┘             └──────────────┘                 └──────────────┘
 *
 * Rules:
 *   - Hardware drivers NEVER call subscribers directly.
 *   - Subscribers NEVER read hardware registers.
 *   - Events propagate through the Scene Graph: Capture → Target → Bubble.
 *   - Any handler may call event_stop_propagation() to cancel bubbling.
 *
 * Threading model (kernel):
 *   - All event posting happens from kernel tasks.
 *   - dispatch() is called synchronously in the compositor task.
 *   - The ring-buffer is protected by a spinlock so IRQ handlers may post.
 */
#ifndef GUI_EVENT_BUS_H
#define GUI_EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * Event types — add new IDs at the end, never renumber.
 * -------------------------------------------------------------------------- */

typedef enum {
    /* Input */
    GUI_EVENT_NONE           = 0,
    GUI_EVENT_MOUSE_MOVE     = 1,
    GUI_EVENT_MOUSE_DOWN     = 2,
    GUI_EVENT_MOUSE_UP       = 3,
    GUI_EVENT_MOUSE_SCROLL   = 4,
    GUI_EVENT_KEY_DOWN       = 5,
    GUI_EVENT_KEY_UP         = 6,
    GUI_EVENT_KEY_CHAR       = 7,
    GUI_EVENT_MOUSE_ENTER    = 8,
    GUI_EVENT_MOUSE_LEAVE    = 9,

    /* Window lifecycle */
    GUI_EVENT_WIN_FOCUS      = 10,
    GUI_EVENT_WIN_BLUR       = 11,
    GUI_EVENT_WIN_CLOSE      = 12,
    GUI_EVENT_WIN_MOVE       = 13,
    GUI_EVENT_WIN_RESIZE     = 14,

    /* Canvas / Camera */
    GUI_EVENT_CANVAS_PAN     = 20,
    GUI_EVENT_CANVAS_ZOOM    = 21,

    /* Node changes */
    GUI_EVENT_NODE_DIRTY     = 30,  /* a node's visual content changed */
    GUI_EVENT_NODE_ADDED     = 31,
    GUI_EVENT_NODE_REMOVED   = 32,

    /* System */
    GUI_EVENT_FRAME_BEGIN    = 40,
    GUI_EVENT_FRAME_END      = 41,

    GUI_EVENT_TYPE_COUNT
} gui_event_type_t;

/* --------------------------------------------------------------------------
 * Priority — lower value = processed first.
 * -------------------------------------------------------------------------- */

typedef enum {
    GUI_PRIORITY_CRITICAL = 0,
    GUI_PRIORITY_HIGH     = 1,
    GUI_PRIORITY_NORMAL   = 2,
    GUI_PRIORITY_LOW      = 3,
} gui_event_priority_t;

/* --------------------------------------------------------------------------
 * Event payload
 * -------------------------------------------------------------------------- */

/* Mouse payload */
typedef struct {
    int x, y;          /* screen coordinates */
    int dx, dy;         /* delta (move/scroll) */
    uint8_t button;     /* 0=none, 1=left, 2=right, 3=middle */
} gui_mouse_payload_t;

/* Keyboard payload */
typedef struct {
    uint8_t  scancode;
    uint32_t keycode;
    uint32_t unicode;
    uint8_t  modifiers; /* bit0=Shift bit1=Ctrl bit2=Alt bit3=Super */
} gui_key_payload_t;

/* Generic opaque payload for custom events */
typedef struct {
    uint64_t a, b, c, d;
} gui_generic_payload_t;

/* The event structure — keep it small to pack well in the ring buffer. */
typedef struct {
    gui_event_type_t     type;
    gui_event_priority_t priority;
    uint32_t             target_id;  /* node ID: 0 = broadcast */
    bool                 propagating;
    union {
        gui_mouse_payload_t   mouse;
        gui_key_payload_t     key;
        gui_generic_payload_t generic;
    };
} gui_event_t;

/* --------------------------------------------------------------------------
 * Handler callback
 * -------------------------------------------------------------------------- */

typedef void (*gui_event_handler_t)(const gui_event_t *event, void *userdata);

/* Returned by event_bus_subscribe — opaque handle used to unsubscribe. */
typedef uint32_t gui_subscription_id_t;

/* --------------------------------------------------------------------------
 * EventBus API
 * -------------------------------------------------------------------------- */

#define GUI_EVENT_QUEUE_CAPACITY 256

typedef struct gui_event_bus gui_event_bus_t;

/* Allocates and initialises a new event bus (kernel heap). */
gui_event_bus_t* event_bus_create(void);

/* Free all resources. */
void event_bus_destroy(gui_event_bus_t *bus);

/*
 * Register a handler for a specific event type.
 * Pass GUI_EVENT_NONE as type to receive ALL events (use sparingly).
 * Returns a subscription ID for later unsubscription.
 */
gui_subscription_id_t event_bus_subscribe(
    gui_event_bus_t      *bus,
    gui_event_type_t      type,
    gui_event_handler_t   handler,
    void                 *userdata);

/* Remove a previously registered handler. */
void event_bus_unsubscribe(gui_event_bus_t *bus, gui_subscription_id_t id);

/*
 * Enqueue an event.  Safe to call from interrupt context — uses a spinlock.
 * Returns false if the ring buffer is full (event dropped).
 */
bool event_bus_post(gui_event_bus_t *bus, const gui_event_t *event);

/*
 * Dispatch all queued events to subscribers, in priority order.
 * Must be called from a single thread (the compositor task).
 * Returns the number of events dispatched.
 */
uint32_t event_bus_dispatch(gui_event_bus_t *bus);

/*
 * Cancel further propagation of the event currently being dispatched.
 * Valid only inside a handler callback.
 */
void event_stop_propagation(gui_event_bus_t *bus);

/* --------------------------------------------------------------------------
 * Convenience posting helpers
 * -------------------------------------------------------------------------- */

static inline void event_post_mouse_move(gui_event_bus_t *bus,
                                          int x, int y, int dx, int dy) {
    gui_event_t e;
    e.type = GUI_EVENT_MOUSE_MOVE;
    e.priority = GUI_PRIORITY_HIGH;
    e.target_id = 0;
    e.propagating = true;
    e.mouse.x = x; e.mouse.y = y;
    e.mouse.dx = dx; e.mouse.dy = dy;
    e.mouse.button = 0;
    event_bus_post(bus, &e);
}

static inline void event_post_mouse_button(gui_event_bus_t *bus,
                                            int x, int y,
                                            uint8_t button, bool pressed) {
    gui_event_t e;
    e.type = pressed ? GUI_EVENT_MOUSE_DOWN : GUI_EVENT_MOUSE_UP;
    e.priority = GUI_PRIORITY_HIGH;
    e.target_id = 0;
    e.propagating = true;
    e.mouse.x = x; e.mouse.y = y;
    e.mouse.dx = 0; e.mouse.dy = 0;
    e.mouse.button = button;
    event_bus_post(bus, &e);
}

static inline void event_post_key(gui_event_bus_t *bus,
                                   uint8_t scancode, bool pressed) {
    gui_event_t e;
    e.type = pressed ? GUI_EVENT_KEY_DOWN : GUI_EVENT_KEY_UP;
    e.priority = GUI_PRIORITY_HIGH;
    e.target_id = 0;
    e.propagating = true;
    e.key.scancode = scancode;
    e.key.keycode = 0;
    e.key.unicode = 0;
    e.key.modifiers = 0;
    event_bus_post(bus, &e);
}

static inline void event_post_node_dirty(gui_event_bus_t *bus, uint32_t node_id) {
    gui_event_t e;
    e.type = GUI_EVENT_NODE_DIRTY;
    e.priority = GUI_PRIORITY_NORMAL;
    e.target_id = node_id;
    e.propagating = false;
    e.generic.a = node_id;
    e.generic.b = e.generic.c = e.generic.d = 0;
    event_bus_post(bus, &e);
}

#endif /* GUI_EVENT_BUS_H */
