#include "ui_idle.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const int64_t sec = 1000000;
    ui_idle_t s = {.state = UI_STATUS_READY, .last_touch_us = sec};
    assert(ui_idle_brightness(&s, 4 * sec - 1) == 65);
    assert(ui_idle_brightness(&s, 4 * sec) == 18);
    assert(ui_idle_brightness(&s, 21 * sec - 1) == 18);
    assert(ui_idle_brightness(&s, 21 * sec) == 0);
    assert(ui_idle_wait_us(&s, sec, 31 * sec) == 3 * sec);
    assert(ui_idle_wait_us(&s, 4 * sec, 31 * sec) == 17 * sec);
    assert(ui_idle_wait_us(&s, 21 * sec, 31 * sec) == 10 * sec);
    assert(ui_idle_wait_us(&s, 31 * sec, 31 * sec) == 0);

    // Repeated host-ready packets while asleep do not postpone sleep or wake.
    assert(!ui_idle_set_state(&s, UI_STATUS_READY, NULL, 100 * sec));
    assert(s.last_touch_us == sec && ui_idle_brightness(&s, 100 * sec) == 0);
    assert(ui_idle_set_state(&s, UI_STATUS_DISCONNECTED, "Waiting", 100 * sec));
    assert(ui_idle_brightness(&s, 100 * sec) == 65);
    assert(!ui_idle_set_state(&s, UI_STATUS_DISCONNECTED, "Waiting", 119 * sec));
    assert(ui_idle_brightness(&s, 120 * sec) == 0);

    ui_idle_touch(&s, UI_STATUS_HINT_VOICE, 121 * sec);
    assert(ui_idle_brightness(&s, 121 * sec) == 65);
    assert(ui_idle_hints(&s, 124 * sec) == UI_STATUS_HINT_NONE);
    ui_idle_set_state(&s, UI_STATUS_RECORDING, "Listening", 122 * sec);
    assert(ui_idle_hints(&s, 200 * sec) == UI_STATUS_HINT_VOICE);
    assert(ui_idle_brightness(&s, 200 * sec) == 65);
    ui_idle_set_state(&s, UI_STATUS_PROCESSING, "Processing", 201 * sec);
    assert(ui_idle_brightness(&s, 300 * sec) == 38);
    assert(ui_idle_wait_us(&s, 300 * sec, 330 * sec) == 30 * sec);
    ui_idle_set_state(&s, UI_STATUS_SUCCESS, "Sent", 301 * sec);
    assert(ui_idle_hints(&s, 301 * sec) == UI_STATUS_HINT_NONE);
    assert(ui_idle_brightness(&s, 321 * sec) == 0);
    ui_idle_set_state(&s, UI_STATUS_RECORDING, "Listening", 400 * sec);
    assert(ui_idle_hints(&s, 400 * sec) == UI_STATUS_HINT_NONE);

    // Startup hints expire even if the host becomes active.
    s.hints = UI_STATUS_HINT_ALL; s.hints_from_key = false;
    s.hints_deadline_us = 3 * sec;
    ui_idle_set_state(&s, UI_STATUS_RECORDING, "Listening", sec);
    assert(ui_idle_hints(&s, 3 * sec - 1) == UI_STATUS_HINT_ALL);
    assert(ui_idle_hints(&s, 3 * sec) == UI_STATUS_HINT_NONE);
    // Microsecond deadlines must still work after the 32-bit microsecond wrap.
    int64_t late = (1LL << 40);
    ui_idle_set_state(&s, UI_STATUS_READY, "Ready", late);
    assert(ui_idle_brightness(&s, late + 20 * sec) == 0);
    puts("UI idle policy tests: PASS");
}
