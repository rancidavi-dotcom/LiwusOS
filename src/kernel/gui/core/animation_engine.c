/*
 * gui/core/animation_engine.c
 */
#include "animation_engine.h"
#include "string.h"

static animation_t s_animations[MAX_ANIMATIONS];

void animation_engine_init(void) {
    memset(s_animations, 0, sizeof(s_animations));
}

static int interp(int start, int end, int t, int duration) {
    if (duration <= 0) return end;
    if (t >= duration) return end;
    /* simple linear interpolation */
    return start + ((end - start) * t) / duration;
}

static uint32_t interp_color(uint32_t start, uint32_t end, int t, int duration) {
    if (duration <= 0 || t >= duration) return end;
    
    int a1 = (start >> 24) & 0xFF;
    int r1 = (start >> 16) & 0xFF;
    int g1 = (start >> 8) & 0xFF;
    int b1 = start & 0xFF;
    
    int a2 = (end >> 24) & 0xFF;
    int r2 = (end >> 16) & 0xFF;
    int g2 = (end >> 8) & 0xFF;
    int b2 = end & 0xFF;
    
    int a = interp(a1, a2, t, duration);
    int r = interp(r1, r2, t, duration);
    int g = interp(g1, g2, t, duration);
    int b = interp(b1, b2, t, duration);
    
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

void animation_start(node_t *node, anim_prop_t prop, void *custom_target, int start, int end, int frames) {
    if (!node || frames <= 0) return;
    
    /* Find existing animation for this node/prop and overwrite, or find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_ANIMATIONS; i++) {
        if (s_animations[i].active && s_animations[i].target == node && s_animations[i].prop == prop) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        for (int i = 0; i < MAX_ANIMATIONS; i++) {
            if (!s_animations[i].active) {
                slot = i;
                break;
            }
        }
    }
    
    if (slot != -1) {
        s_animations[slot].target = node;
        s_animations[slot].prop = prop;
        s_animations[slot].color_target = (uint32_t*)custom_target;
        s_animations[slot].start_val = start;
        s_animations[slot].end_val = end;
        s_animations[slot].duration_frames = frames;
        s_animations[slot].current_frame = 0;
        s_animations[slot].active = true;
    }
}

void animation_cancel_all(node_t *node) {
    if (!node) return;
    for (int i = 0; i < MAX_ANIMATIONS; i++) {
        if (s_animations[i].active && s_animations[i].target == node) {
            s_animations[i].active = false;
        }
    }
}

bool animation_engine_tick(void) {
    bool running = false;
    
    for (int i = 0; i < MAX_ANIMATIONS; i++) {
        if (!s_animations[i].active) continue;
        
        animation_t *a = &s_animations[i];
        a->current_frame++;
        
        int val;
        if (a->prop == ANIM_PROP_COLOR) {
            val = interp_color(a->start_val, a->end_val, a->current_frame, a->duration_frames);
        } else {
            val = interp(a->start_val, a->end_val, a->current_frame, a->duration_frames);
        }
        
        /* Apply */
        switch (a->prop) {
            case ANIM_PROP_X:
                node_set_position(a->target, val, a->target->local_y);
                break;
            case ANIM_PROP_Y:
                node_set_position(a->target, a->target->local_x, val);
                break;
            case ANIM_PROP_WIDTH:
                a->target->width = val;
                a->target->dirty |= NODE_DIRTY_PAINT | NODE_DIRTY_LAYOUT;
                break;
            case ANIM_PROP_HEIGHT:
                a->target->height = val;
                a->target->dirty |= NODE_DIRTY_PAINT | NODE_DIRTY_LAYOUT;
                break;
            case ANIM_PROP_OPACITY_FP:
                a->target->opacity = (float)val / 65536.0f; /* We don't actually use opacity float properly yet, but just in case */
                a->target->dirty |= NODE_DIRTY_PAINT;
                break;
            case ANIM_PROP_COLOR:
                if (a->color_target) {
                    *(a->color_target) = val;
                    a->target->dirty |= NODE_DIRTY_PAINT;
                }
                break;
        }
        
        if (a->current_frame >= a->duration_frames) {
            a->active = false;
        } else {
            running = true;
        }
    }
    
    return running;
}
