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

void ui_status_init(void);
void ui_status_set_state(ui_status_state_t state, const char *message);
void ui_status_touch(void);
