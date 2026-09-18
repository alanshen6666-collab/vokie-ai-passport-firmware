#pragma once

#include "ui_status.h"
#include <stdint.h>
#include <string.h>

#define UI_DIM_TIMEOUT_US (3LL * 1000 * 1000)
#define UI_OFF_TIMEOUT_US (20LL * 1000 * 1000)
#define UI_HINT_TIMEOUT_US (3LL * 1000 * 1000)

typedef struct {
    ui_status_state_t state;
    ui_status_hint_t hints;
    bool hints_from_key;
    int64_t last_touch_us;
    int64_t hints_deadline_us;
    uint32_t revision;
    char message[96];
} ui_idle_t;

static inline bool ui_idle_active(const ui_idle_t *s)
{
    return s->state == UI_STATUS_RECORDING || s->state == UI_STATUS_PROCESSING;
}

static inline uint8_t ui_idle_brightness(const ui_idle_t *s, int64_t now)
{
    if (!ui_idle_active(s)) {
        if (now - s->last_touch_us >= UI_OFF_TIMEOUT_US) return 0;
        if (now - s->last_touch_us >= UI_DIM_TIMEOUT_US) return 18;
    }
    return s->state == UI_STATUS_PROCESSING ? 38 : 65;
}

static inline ui_status_hint_t ui_idle_hints(const ui_idle_t *s, int64_t now)
{
    if ((!ui_idle_active(s) || !s->hints_from_key) && now >= s->hints_deadline_us)
        return UI_STATUS_HINT_NONE;
    return s->hints;
}

static inline void ui_idle_touch(ui_idle_t *s, ui_status_hint_t hint, int64_t now)
{
    s->last_touch_us = now;
    s->hints_deadline_us = now + UI_HINT_TIMEOUT_US;
    s->hints = hint;
    s->hints_from_key = true;
    ++s->revision;
}

// Identical host updates must not continually wake an otherwise idle device.
static inline bool ui_idle_set_state(ui_idle_t *s, ui_status_state_t state,
                                     const char *message, int64_t now)
{
    // Expired key hints must stay hidden if a later host event becomes active.
    // Evaluate against the previous state before changing active/idle mode.
    if (ui_idle_hints(s, now) == UI_STATUS_HINT_NONE) s->hints = UI_STATUS_HINT_NONE;
    char text[sizeof(s->message)] = {0};
    if (message) strncpy(text, message, sizeof(text) - 1);
    if (s->state == state && strcmp(s->message, text) == 0) return false;
    s->state = state;
    memcpy(s->message, text, sizeof(text));
    s->last_touch_us = now;
    ++s->revision;
    return true;
}

// Time to the next visible change or battery sample, not a polling period.
static inline int64_t ui_idle_wait_us(const ui_idle_t *s, int64_t now,
                                      int64_t next_battery)
{
    int64_t deadline = next_battery;
    if (!ui_idle_active(s)) {
        const int64_t dim = s->last_touch_us + UI_DIM_TIMEOUT_US;
        const int64_t off = s->last_touch_us + UI_OFF_TIMEOUT_US;
        if (dim > now && dim < deadline) deadline = dim;
        if (off > now && off < deadline) deadline = off;
    }
    if (s->hints && (!ui_idle_active(s) || !s->hints_from_key) &&
        s->hints_deadline_us > now && s->hints_deadline_us < deadline)
        deadline = s->hints_deadline_us;
    return deadline > now ? deadline - now : 0;
}
