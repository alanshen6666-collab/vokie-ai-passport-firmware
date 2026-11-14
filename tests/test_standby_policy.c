#include "standby_policy.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const uint32_t threshold = VOKIE_DEEP_SLEEP_IDLE_MS;
    assert(!vokie_deep_sleep_due(threshold - 1, 0, false, false));
    assert(vokie_deep_sleep_due(threshold, 0, false, false));
    assert(!vokie_deep_sleep_due(threshold, 0, true, false));
    assert(!vokie_deep_sleep_due(threshold, 0, false, true));
    assert(!vokie_deep_sleep_due(threshold, threshold, false, false));
    assert(vokie_deep_sleep_due(threshold - 2, UINT32_MAX - 1, false, false));

    assert(vokie_link_matches(false, 12, 0));
    assert(!vokie_link_matches(false, 12, 1));
    assert(!vokie_link_matches(false, 24, 0));
    assert(!vokie_link_matches(true, 12, 0));
    assert(vokie_link_matches(true, 72, 0));
    assert(vokie_link_matches(true, 96, 0));
    assert(!vokie_link_matches(true, 97, 0));
    puts("Standby link and deep sleep policy tests: PASS");
    return 0;
}
