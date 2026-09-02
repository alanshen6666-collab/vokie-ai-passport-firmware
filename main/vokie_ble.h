#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Starts the Vokie Passport BLE peripheral and its NimBLE host task.
esp_err_t vokie_ble_start(void);

// Returns true only after the host subscribed to Control and Audio.
bool vokie_ble_ready(void);
