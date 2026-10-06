#pragma once

#include <stdbool.h>

typedef enum {
    UI_STATUS_STARTING = 0,
    UI_STATUS_READY,
    UI_STATUS_RECORDING,
    UI_STATUS_PROCESSING,
    UI_STATUS_SUCCESS,
    UI_STATUS_ERROR,
    UI_STATUS_DISCONNECTED
} ui_status_state_t;

typedef enum {
    UI_STATUS_HINT_NONE = 0,
    UI_STATUS_HINT_VOICE = 1 << 0,
    UI_STATUS_HINT_SEND = 1 << 1,
    UI_STATUS_HINT_UNDO = 1 << 2,
    UI_STATUS_HINT_ALL = (1 << 3) - 1,
} ui_status_hint_t;

void ui_status_init(void);
void ui_status_set_state(ui_status_state_t state, const char *message);
// Non-blocking: wake the display and request one hint, or all three hints.
void ui_status_touch(ui_status_hint_t hint);

// Optional OpenClaw reply overlay. Safe to call from any task; rendering
// happens on the status worker, and the backlight stays on while visible.
void ui_status_show_reply(const char *text);
bool ui_status_reply_visible(void);
// Advance to the next page; returns false when the overlay closed itself.
bool ui_status_reply_next_page(void);
void ui_status_reply_dismiss(void);
