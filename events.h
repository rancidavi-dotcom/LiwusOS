#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    EVENT_NONE,
    EVENT_MOUSE_CLICK,
    EVENT_MOUSE_MOVE,
    EVENT_KEY_PRESS
} event_type_t;

typedef struct {
    event_type_t type;
    int mx, my;
    char key;
    bool handled;
} event_t;

#endif
