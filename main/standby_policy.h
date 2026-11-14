#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef VOKIE_DEEP_SLEEP_IDLE_MS
#define VOKIE_DEEP_SLEEP_IDLE_MS (15U * 60 * 1000)
#endif
#define VOKIE_AUDIO_INTERVAL 12     // 15 ms, in Bluetooth's 1.25 ms units.
#define VOKIE_IDLE_INTERVAL_MIN 72  // 90 ms.
#define VOKIE_IDLE_INTERVAL_MAX 96  // 120 ms.
#define VOKIE_LINK_SUPERVISION_TIMEOUT 400 // 4 s, in 10 ms units.

static inline bool vokie_link_matches(bool idle, uint16_t interval, uint16_t latency)
{
    if (idle) return interval >= VOKIE_IDLE_INTERVAL_MIN &&
                     interval <= VOKIE_IDLE_INTERVAL_MAX && latency == 0;
    return interval <= VOKIE_AUDIO_INTERVAL && latency == 0;
}

static inline bool vokie_deep_sleep_due(uint32_t now_ms, uint32_t activity_ms,
                                       bool busy, bool usb_active)
{
    return !busy && !usb_active && now_ms - activity_ms >= VOKIE_DEEP_SLEEP_IDLE_MS;
}
