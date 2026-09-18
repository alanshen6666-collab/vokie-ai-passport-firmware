#include "usb_standby_policy.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const int64_t sec = 1000000;
    const int64_t poll = USB_STANDBY_POLL_US;

    // Battery boot: seeded detached, and the bus never reports attached.
    usb_standby_t s = {
        .held = false,
        .last_attached_us = -USB_STANDBY_RELEASE_GRACE_US,
    };
    assert(!usb_standby_update(&s, false, 0));
    assert(!usb_standby_update(&s, false, poll));
    assert(!usb_standby_update(&s, false, 60 * sec));
    assert(!s.held);

    // USB boot: every poll reports attached and the locks stay held.
    for (int i = 0; i < 10; ++i) {
        assert(usb_standby_update(&s, true, i * poll));
        assert(s.held && s.last_attached_us == i * poll);
    }

    // A host-side interruption shorter than the grace keeps the locks held.
    const int64_t attached_at = 9 * poll;
    assert(usb_standby_update(&s, true, attached_at));
    assert(usb_standby_update(&s, false, attached_at + poll));
    assert(usb_standby_update(&s, false, attached_at + 2 * poll));
    assert(s.held);
    // The release happens exactly at the grace boundary, counted from the
    // last attach, and re-attach acquires again on the following poll.
    assert(usb_standby_update(&s, false,
                              attached_at + USB_STANDBY_RELEASE_GRACE_US - 1));
    assert(s.held);
    assert(!usb_standby_update(&s, false,
                               attached_at + USB_STANDBY_RELEASE_GRACE_US));
    assert(!s.held);
    assert(usb_standby_update(&s, true, attached_at + 6 * poll));
    assert(s.held && s.last_attached_us == attached_at + 6 * poll);

    // The grace restarts from the latest attach, not the first silence.
    assert(usb_standby_update(&s, true, 100 * sec));
    assert(usb_standby_update(&s, false, 102 * sec));
    assert(usb_standby_update(&s, true, 104 * sec));
    assert(usb_standby_update(&s, false, 106 * sec));
    assert(usb_standby_update(&s, false, 108 * sec));
    assert(s.held);
    assert(!usb_standby_update(&s, false, 110 * sec));
    assert(!s.held);

    puts("USB standby policy tests: PASS");
}
