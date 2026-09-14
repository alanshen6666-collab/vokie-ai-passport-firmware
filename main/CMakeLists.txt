idf_component_register(
    SRCS "main.c" "vokie_ble.c" "ui_status.c"
         "../assets/fonts/vokie_title_barlow_condensed_bold_22.c"
    INCLUDE_DIRS "." "../assets/fonts"
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash json
)
