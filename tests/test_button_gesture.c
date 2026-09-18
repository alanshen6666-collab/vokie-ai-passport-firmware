#include "button_gesture.h"
#include <assert.h>
#include <stdio.h>

static button_gesture_t key;
static button_action_t edge(button_edge_t event, uint32_t at, uint32_t context) {
    return button_gesture_update(&key, event, at, context, 650);
}
int main(void) {
    assert(edge(BUTTON_EDGE_RELEASE, 0, 1) == BUTTON_ACTION_NONE);
    // Each release acts immediately, including a second press inside 180 ms.
    assert(edge(BUTTON_EDGE_PRESS, 20, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 80, 1) == BUTTON_ACTION_CLICK);
    assert(edge(BUTTON_EDGE_PRESS, 140, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 200, 1) == BUTTON_ACTION_CLICK);
    assert(edge(BUTTON_EDGE_OTHER, 400, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 420, 1) == BUTTON_ACTION_NONE);
    // A second, held press still has an independent 650 ms deadline. The
    // driver's rounded 640 ms callback cannot trigger early or trigger twice.
    assert(edge(BUTTON_EDGE_PRESS, 500, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 560, 1) == BUTTON_ACTION_CLICK);
    assert(edge(BUTTON_EDGE_PRESS, 620, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_LONG, 1260, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_LONG, 1270, 1) == BUTTON_ACTION_LONG);
    assert(edge(BUTTON_EDGE_LONG, 1290, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 1400, 1) == BUTTON_ACTION_NONE);
    // Release and expiry in either dispatch order give one long action.
    assert(edge(BUTTON_EDGE_PRESS, 2000, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 2650, 1) == BUTTON_ACTION_LONG);
    assert(edge(BUTTON_EDGE_LONG, 2650, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_PRESS, 3000, 1) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 3649, 1) == BUTTON_ACTION_CLICK);
    // Connecting while held, or reconnecting with a recycled BLE handle,
    // must not turn an old press into an input in the new connection.
    assert(edge(BUTTON_EDGE_PRESS, 4000, 0) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 4080, 2) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_PRESS, 5000, 2) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_LONG, 5650, 3) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 5700, 3) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_PRESS, 6000, 3) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 6080, 3) == BUTTON_ACTION_CLICK);
    // Millisecond wrap-around preserves the physical hold duration.
    assert(edge(BUTTON_EDGE_PRESS, UINT32_MAX-20, 3) == BUTTON_ACTION_NONE);
    assert(edge(BUTTON_EDGE_RELEASE, 39, 3) == BUTTON_ACTION_CLICK);
    puts("Button release/hold gesture tests: PASS");
}
