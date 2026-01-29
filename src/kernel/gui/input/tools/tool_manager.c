/*
 * gui/input/tools/tool_manager.c
 */
#include "tool_manager.h"
#include "kheap.h"
#include "string.h"

#define MAX_TOOLS 8

struct tool_manager {
    gui_event_bus_t       *bus;
    gui_subscription_id_t  sub_id;

    tool_t *tools[MAX_TOOLS];
    int     tool_count;
    
    camera_t *camera;
    node_t   *scene_root;
};

/* --------------------------------------------------------------------------
 * Event Handler
 * -------------------------------------------------------------------------- */

static void on_event(const gui_event_t *event, void *userdata) {
    tool_manager_t *tm = (tool_manager_t *)userdata;

    /* Only handle input events */
    if (event->type < GUI_EVENT_MOUSE_MOVE || event->type > GUI_EVENT_KEY_CHAR) {
        return;
    }

    bool consumed = false;

    for (int i = 0; i < tm->tool_count; i++) {
        if (tool_event(tm->tools[i], event)) {
            consumed = true;
            break;
        }
    }

    if (consumed) {
        event_stop_propagation(tm->bus);
    }
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

tool_manager_t *tool_manager_create(gui_event_bus_t *bus, camera_t *cam, node_t *scene_root) {
    if (!bus) return NULL;

    tool_manager_t *tm = (tool_manager_t *)kmalloc(sizeof(tool_manager_t));
    if (!tm) return NULL;
    memset(tm, 0, sizeof(tool_manager_t));

    tm->bus        = bus;
    tm->camera     = cam;
    tm->scene_root = scene_root;

    /* Assina todos os eventos (GUI_EVENT_NONE = all) */
    tm->sub_id = event_bus_subscribe(bus, GUI_EVENT_NONE, on_event, tm);

    return tm;
}

void tool_manager_destroy(tool_manager_t *tm) {
    if (!tm) return;
    if (tm->bus) {
        event_bus_unsubscribe(tm->bus, tm->sub_id);
    }
    kfree(tm);
}

void tool_manager_add_tool(tool_manager_t *tm, tool_t *tool) {
    if (!tm || !tool) return;
    if (tm->tool_count >= MAX_TOOLS) return;
    tm->tools[tm->tool_count++] = tool;
    tool_activate(tool);
}
