#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "vokie_ble.h"
#include "ui_status.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Vokie AI Passport firmware starting");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGW(TAG, "I2C initialization failed; audio may be unavailable");
    }
    // Initialize before UI/audio workers start using the shared bus. A missing
    // gauge is optional hardware; the status UI will display an unknown SOC.
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "Battery unavailable; displaying --%%");
    }
    if (bsp_display_init() == ESP_OK && bsp_lvgl_init()) {
        bsp_display_backlight(65);
        ui_status_init();
    } else {
        ESP_LOGW(TAG, "Display initialization failed; continuing headless");
    }
    err = vokie_ble_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Vokie BLE startup failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Ready: advertise as Vokie Passport; click UP to talk");
    }
}
