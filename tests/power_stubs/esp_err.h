#pragma once
#include "../battery_stubs/esp_err.h"
#include <assert.h>
#define ESP_ERROR_CHECK(expr) do { assert((expr) == ESP_OK); } while (0)
