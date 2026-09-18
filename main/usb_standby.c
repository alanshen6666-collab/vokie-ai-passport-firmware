// main/usb_standby.c
// ESP32-C3 USB Serial/JTAG needs its PHY clock and cannot survive light sleep.
// Add a five-second grace period above IDF's fast connection monitor so brief
// SOF gaps retain NO_LIGHT_SLEEP and the APB frequency floor. This protects
// against clock loss; it does not establish the cause of an observed host-side
// disconnect. A long-silent bus releases the locks for battery standby.
#include "usb_standby.h"

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "usb_standby_policy.h"

static const char *TAG = "usb_standby";

#if CONFIG_PM_ENABLE
static esp_timer_handle_t s_timer;
static esp_pm_lock_handle_t s_sleep_lock;
static esp_pm_lock_handle_t s_freq_lock;
static usb_standby_t s_policy;
static bool s_locks_held;

static void on_poll(void *arg)
{
    (void)arg;
    const bool attached = usb_serial_jtag_is_connected();
    const int64_t now = esp_timer_get_time();
    const int64_t silent_us =
        attached ? 0 : now - s_policy.last_attached_us;
    const bool held = usb_standby_update(&s_policy, attached, now);
    if (held == s_locks_held) return;
    if (held) {
        // NO_LIGHT_SLEEP keeps the peripheral powered; APB_FREQ_MAX keeps
        // BBPLL, and therefore the USJ PHY clock, alive at the 40 MHz floor.
        ESP_ERROR_CHECK(esp_pm_lock_acquire(s_sleep_lock));
        ESP_ERROR_CHECK(esp_pm_lock_acquire(s_freq_lock));
        ESP_LOGI(TAG, "USB bus active: console power locks held");
    } else {
        ESP_ERROR_CHECK(esp_pm_lock_release(s_sleep_lock));
        ESP_ERROR_CHECK(esp_pm_lock_release(s_freq_lock));
        ESP_LOGI(TAG, "USB bus silent for %lu ms; light sleep permitted",
                 (unsigned long)(silent_us / 1000));
    }
    s_locks_held = held;
}
#endif

void usb_standby_init(void)
{
#if CONFIG_PM_ENABLE
    if (s_timer) return;
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0,
                                       "usb_console", &s_sleep_lock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0,
                                       "usb_apb", &s_freq_lock));
    // Seed detached so a battery boot never holds the locks; the first poll
    // that observes an active bus opens the grace window instead.
    s_policy = (usb_standby_t){
        .held = false,
        .last_attached_us = -USB_STANDBY_RELEASE_GRACE_US,
    };
    const esp_timer_create_args_t cfg = {
        .callback = on_poll,
        .name = "usb_standby",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&cfg, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, USB_STANDBY_POLL_US));
    on_poll(NULL);
#endif
}
