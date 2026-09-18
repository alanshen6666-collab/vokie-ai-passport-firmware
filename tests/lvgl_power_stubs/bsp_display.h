#pragma once
#include <stdbool.h>
#include "esp_err.h"
void *bsp_display_panel(void);
void *bsp_display_io(void);
esp_err_t bsp_lvgl_set_paused(bool paused);
