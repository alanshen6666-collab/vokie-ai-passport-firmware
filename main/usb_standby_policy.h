#pragma once

#include <stdbool.h>
#include <stdint.h>

// How often the driver re-reads the USJ connection monitor.
#define USB_STANDBY_POLL_US (2LL * 1000 * 1000)
// A bus reporting no connection for this long after the last attach is
// treated as unplugged and battery light sleep is permitted again.
#define USB_STANDBY_RELEASE_GRACE_US (5LL * 1000 * 1000)

typedef struct {
    bool held;                // power locks currently required
    int64_t last_attached_us; // last poll that observed an active bus
} usb_standby_t;

// Pure policy evaluated on every poll. `attached_now` is the value of
// usb_serial_jtag_is_connected() at `now`. Short host-side interruptions
// (serial-port probes, bus glitches) keep the locks held through the grace
// window; only a bus silent past the grace releases them. Seed
// last_attached_us with -USB_STANDBY_RELEASE_GRACE_US so a boot starts
// detached and the first observed attach opens the grace window.
static inline bool usb_standby_update(usb_standby_t *s, bool attached_now,
                                      int64_t now)
{
    if (attached_now) s->last_attached_us = now;
    s->held = now - s->last_attached_us < USB_STANDBY_RELEASE_GRACE_US;
    return s->held;
}
