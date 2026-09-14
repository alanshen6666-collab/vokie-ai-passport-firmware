#pragma once

#include <stdbool.h>
#include <stdio.h>

// Pure presentation policy shared by the status UI and host tests. Invalid
// readings must never look like an empty battery or retain a stale percentage.
// Voltage is a measured fallback, never converted to an uncalibrated SOC.
typedef struct {
    char text[7];
    unsigned bars; // 0..4; voltage/unknown use a neutral empty outline.
    bool low;
} ui_battery_view_t;

static inline ui_battery_view_t ui_battery_view(int soc, int mv)
{
    ui_battery_view_t view = {.text = "--%", .bars = 0, .low = false};
    if (soc >= 0 && soc <= 100) {
        snprintf(view.text, sizeof(view.text), "%d%%", soc);
        view.bars = (unsigned)(soc + 24) / 25;
        view.low = soc <= 20;
    } else if (mv > 0 && mv <= 9999) {
        // Truncate to 10 mV for a compact display; validity comes from the BSP.
        snprintf(view.text, sizeof(view.text), "%d.%02dV", mv / 1000, (mv % 1000) / 10);
    }
    return view;
}
