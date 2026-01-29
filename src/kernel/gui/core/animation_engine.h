/*
 * gui/core/animation_engine.h
 *
 * Simple tweening engine for node properties.
 */
#ifndef GUI_ANIMATION_ENGINE_H
#define GUI_ANIMATION_ENGINE_H

#include "../scene/node.h"

typedef enum {
    ANIM_PROP_X,
    ANIM_PROP_Y,
    ANIM_PROP_WIDTH,
    ANIM_PROP_HEIGHT,
    ANIM_PROP_OPACITY_FP, /* fixed point 16.16 */
    ANIM_PROP_COLOR       /* RGB interpolation */
} anim_prop_t;

typedef struct {
    node_t     *target;
    anim_prop_t prop;
    uint32_t   *color_target; /* if prop is COLOR, pointer to color variable */
    int         start_val;
    int         end_val;
    int         duration_frames;
    int         current_frame;
    bool        active;
} animation_t;

#define MAX_ANIMATIONS 64

void animation_engine_init(void);

/* Tick all active animations. Returns true if any animation is still running (requires redraw). */
bool animation_engine_tick(void);

/* Start an animation on a node. If an animation for the same node/prop exists, it is overwritten. */
void animation_start(node_t *node, anim_prop_t prop, void *custom_target, int start, int end, int frames);

/* Cancel animations for a specific node */
void animation_cancel_all(node_t *node);

#endif
