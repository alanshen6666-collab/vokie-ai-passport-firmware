#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BUTTON_EDGE_PRESS, BUTTON_EDGE_RELEASE, BUTTON_EDGE_LONG, BUTTON_EDGE_OTHER
} button_edge_t;
typedef enum {
    BUTTON_ACTION_NONE, BUTTON_ACTION_CLICK, BUTTON_ACTION_LONG
} button_action_t;
typedef struct {
    bool pressed, long_sent;
    uint32_t started_ms, context;
} button_gesture_t;

// context identifies the host connection; zero means no ready host. The caller
// owns this state on the timer task. Legacy delayed CLICK/DOUBLE are ignored.
static inline button_action_t button_gesture_update(button_gesture_t *s,
        button_edge_t edge, uint32_t now_ms, uint32_t context, uint32_t long_ms)
{
    if (edge == BUTTON_EDGE_PRESS) {
        if (!s->pressed) {
            *s = (button_gesture_t){ .pressed = true,
                .started_ms = now_ms, .context = context };
        }
        return BUTTON_ACTION_NONE;
    }
    if (!s->pressed || edge == BUTTON_EDGE_OTHER) return BUTTON_ACTION_NONE;
    const bool valid = s->context != 0 && s->context == context;
    const bool held = now_ms - s->started_ms >= long_ms;
    if (edge == BUTTON_EDGE_RELEASE) {
        s->pressed = false;
        if (!valid || s->long_sent) return BUTTON_ACTION_NONE;
        return held ? BUTTON_ACTION_LONG : BUTTON_ACTION_CLICK;
    }
    if (edge == BUTTON_EDGE_LONG && held && !s->long_sent) {
        s->long_sent = true;
        return valid ? BUTTON_ACTION_LONG : BUTTON_ACTION_NONE;
    }
    return BUTTON_ACTION_NONE;
}
