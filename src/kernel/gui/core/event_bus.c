/*
 * gui/core/event_bus.c
 *
 * Implementation of the LiwusOS GUI Event Bus.
 *
 * Internals:
 *   - Fixed-size ring buffer for posted events (lock-free post, single
 *     consumer dispatch).
 *   - Subscription table of MAX_SUBSCRIBERS entries.
 *   - Dispatch sorts by priority each frame (insertion sort — small N).
 */

#include "event_bus.h"
#include "kheap.h"
#include "string.h"

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

#define MAX_SUBSCRIBERS 64
#define RING_CAPACITY   GUI_EVENT_QUEUE_CAPACITY

/* --------------------------------------------------------------------------
 * Internal structures
 * -------------------------------------------------------------------------- */

typedef struct {
    gui_subscription_id_t  id;
    gui_event_type_t       filter;    /* GUI_EVENT_NONE = wildcard (all) */
    gui_event_handler_t    handler;
    void                  *userdata;
    bool                   active;
} subscriber_t;

struct gui_event_bus {
    /* Ring buffer — producer/consumer */
    gui_event_t   ring[RING_CAPACITY];
    uint32_t      head;   /* next write position */
    uint32_t      tail;   /* next read position  */
    uint32_t      count;

    /* Scratch space: sorted snapshot for dispatch */
    gui_event_t   pending[RING_CAPACITY];
    uint32_t      pending_count;

    /* Subscription table */
    subscriber_t  subs[MAX_SUBSCRIBERS];
    uint32_t      sub_count;
    uint32_t      next_sub_id;

    /* Propagation control — set inside dispatch loop */
    bool          stop_propagation;
};

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

gui_event_bus_t *event_bus_create(void) {
    gui_event_bus_t *bus = (gui_event_bus_t *)kmalloc(sizeof(gui_event_bus_t));
    if (!bus) return NULL;
    memset(bus, 0, sizeof(gui_event_bus_t));
    bus->next_sub_id = 1;
    return bus;
}

void event_bus_destroy(gui_event_bus_t *bus) {
    if (!bus) return;
    kfree(bus);
}

/* --------------------------------------------------------------------------
 * Subscription
 * -------------------------------------------------------------------------- */

gui_subscription_id_t event_bus_subscribe(gui_event_bus_t     *bus,
                                           gui_event_type_t    type,
                                           gui_event_handler_t handler,
                                           void               *userdata) {
    if (!bus || !handler) return 0;
    if (bus->sub_count >= MAX_SUBSCRIBERS) return 0;

    /* Find a free slot (may reuse a destroyed slot) */
    for (uint32_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!bus->subs[i].active) {
            bus->subs[i].id       = bus->next_sub_id++;
            bus->subs[i].filter   = type;
            bus->subs[i].handler  = handler;
            bus->subs[i].userdata = userdata;
            bus->subs[i].active   = true;
            bus->sub_count++;
            return bus->subs[i].id;
        }
    }
    return 0;
}

void event_bus_unsubscribe(gui_event_bus_t *bus, gui_subscription_id_t id) {
    if (!bus || id == 0) return;
    for (uint32_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (bus->subs[i].active && bus->subs[i].id == id) {
            bus->subs[i].active = false;
            bus->sub_count--;
            return;
        }
    }
}

/* --------------------------------------------------------------------------
 * Posting (ring buffer — safe from interrupt context)
 * -------------------------------------------------------------------------- */

bool event_bus_post(gui_event_bus_t *bus, const gui_event_t *event) {
    if (!bus || !event) return false;
    if (bus->count >= RING_CAPACITY) return false;   /* drop on overflow */

    bus->ring[bus->head] = *event;
    bus->head = (bus->head + 1) % RING_CAPACITY;
    bus->count++;
    return true;
}

/* --------------------------------------------------------------------------
 * Dispatch
 * -------------------------------------------------------------------------- */

/* Insertion sort by priority (ascending — CRITICAL first). */
static void sort_pending(gui_event_t *arr, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        gui_event_t key = arr[i];
        int j = (int)i - 1;
        while (j >= 0 && arr[j].priority > key.priority) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

uint32_t event_bus_dispatch(gui_event_bus_t *bus) {
    if (!bus) return 0;

    /* Drain the ring buffer into a local snapshot so post() can be called
     * safely by handlers without corrupting the iteration. */
    bus->pending_count = 0;
    while (bus->count > 0 && bus->pending_count < RING_CAPACITY) {
        bus->pending[bus->pending_count++] = bus->ring[bus->tail];
        bus->tail = (bus->tail + 1) % RING_CAPACITY;
        bus->count--;
    }

    if (bus->pending_count == 0) return 0;

    /* Sort by priority */
    sort_pending(bus->pending, bus->pending_count);

    uint32_t dispatched = 0;
    for (uint32_t ei = 0; ei < bus->pending_count; ei++) {
        const gui_event_t *ev = &bus->pending[ei];
        bus->stop_propagation = false;

        for (uint32_t si = 0; si < MAX_SUBSCRIBERS; si++) {
            subscriber_t *s = &bus->subs[si];
            if (!s->active) continue;

            /* Wildcard (GUI_EVENT_NONE) or exact match */
            if (s->filter != GUI_EVENT_NONE && s->filter != ev->type) continue;

            s->handler(ev, s->userdata);
            dispatched++;

            if (bus->stop_propagation) break;
        }
    }

    return dispatched;
}

void event_stop_propagation(gui_event_bus_t *bus) {
    if (bus) bus->stop_propagation = true;
}
