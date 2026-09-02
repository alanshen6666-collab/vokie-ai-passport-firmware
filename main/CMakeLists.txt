idf_component_register(
    SRCS "main.c" "vokie_ble.c" "ui_status.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash json
)
