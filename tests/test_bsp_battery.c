#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp_battery.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "freertos/task.h"

// Independent fixture pinned to FoloToy PR #38 / 7eeb76db, stock 520 mAh cell.
static const uint8_t expected_profile[80] = {
    0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAD, 0xC7, 0xC8, 0xCA, 0xBD, 0xB1, 0xC1, 0x94,
    0x88, 0xD1, 0xBD, 0x97, 0x88, 0x66, 0x56, 0x4A,
    0x3F, 0x33, 0x26, 0x5C, 0x37, 0xD1, 0x27, 0xD8,
    0xCC, 0xB7, 0xCF, 0xB3, 0xB2, 0xAE, 0xA6, 0x9E,
    0x99, 0x97, 0x9B, 0x86, 0x47, 0x1E, 0x17, 0x26,
    0x49, 0x96, 0xD9, 0xE1, 0xDD, 0xDC, 0xD4, 0x59,
    0x00, 0x00, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C,
};
static uint8_t registers[256];
static unsigned writes, profile_writes, verified_bytes, removals, elapsed_ms;
static unsigned last_mode_ms, soc_attempts;
static int scenario;
static bool read_failure, calculate_soc = true;
static int fake_bus, fake_device;

esp_err_t bsp_i2c_init(void) { return ESP_OK; }
i2c_master_bus_handle_t bsp_i2c_bus(void) { return &fake_bus; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                  const i2c_device_config_t *config,
                                  i2c_master_dev_handle_t *device)
{
    assert(bus == &fake_bus && config->device_address == BSP_I2C_CW2017_ADDR);
    *device = &fake_device;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device == &fake_device);
    ++removals;
    return ESP_OK;
}
void vTaskDelay(TickType_t ticks) { elapsed_ms += ticks; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                            const void *tx, size_t tx_size, int timeout)
{
    const uint8_t *bytes = tx;
    const uint8_t reg = bytes[0], value = bytes[1];
    assert(device == &fake_device && tx_size == 2 && timeout == 100);
    ++writes;
    if (scenario == 1 && writes == 1) return ESP_ERR_TIMEOUT;
    if (reg == 0x08) {
        assert(value == 0x30 || value == 0xf0 || value == 0x00);
        if (value != 0x30) {
            assert(registers[8] == 0x30 && elapsed_ms - last_mode_ms >= 20);
        }
        if (scenario == 6 && value == 0x00) return ESP_ERR_TIMEOUT;
        if (value == 0x00) assert(registers[0x0b] & 0x80);
        last_mode_ms = elapsed_ms;
    } else if (reg >= 0x10 && reg < 0x60) {
        assert(registers[8] == 0xf0); // never write a running gauge
        assert(reg == 0x10 + profile_writes); // exactly 80 sequential bytes
        assert(value == expected_profile[reg - 0x10]);
        if (scenario == 2 && reg == 0x1c) return ESP_ERR_TIMEOUT;
        ++profile_writes;
    } else {
        assert(reg == 0x0b && registers[8] == 0xf0);
        assert(verified_bytes == 80); // UPDATE_FLAG only after complete readback
        assert(value == 0x94); // preserve the pre-existing 20% alert threshold
        if (scenario == 5) return ESP_ERR_TIMEOUT;
    }
    registers[reg] = value;
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                    const void *tx, size_t tx_size,
                                    void *rx, size_t rx_size, int timeout)
{
    const uint8_t reg = *(const uint8_t *)tx;
    assert(device == &fake_device && tx_size == 1 && timeout == 100);
    assert((size_t)reg + rx_size <= sizeof(registers));
    if (read_failure || (scenario == 4 && reg == 0)) return ESP_ERR_TIMEOUT;
    if (scenario == 11 && reg == 0x0b) return ESP_ERR_TIMEOUT;
    if (reg == 4 && calculate_soc) {
        ++soc_attempts;
        if (scenario == 13 && soc_attempts < 3) return ESP_ERR_TIMEOUT;
        if (scenario != 7 && registers[8] == 0 && (registers[0x0b] & 0x80) &&
            memcmp(registers + 0x10, expected_profile, 80) == 0) registers[4] = 63;
    }
    memcpy(rx, registers + reg, rx_size);
    if (reg >= 0x10 && reg < 0x60 && profile_writes == 80) {
        assert(rx_size == 1);
        ++verified_bytes;
        if (scenario == 3 && reg == 0x20) *(uint8_t *)rx ^= 1;
    }
    if (scenario == 14 && reg == 8) *(uint8_t *)rx = 0xf0;
    return ESP_OK;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    scenario = atoi(argv[1]);
    const int original_scenario = scenario;
    registers[0] = 0x0f;
    registers[8] = 0xf0;
    registers[4] = 0xfe;
    registers[5] = 0xe8;
    registers[0x0b] = 0x14;
    if (scenario >= 8 && scenario <= 12) {
        memcpy(registers + 0x10, expected_profile, 80);
        registers[0x0b] |= 0x80;
        registers[8] = 0;
        if (scenario == 9) registers[0x25] ^= 1; // flag alone is insufficient
        if (scenario == 10) registers[8] = 0xf0;
        if (scenario == 12) registers[0x0b] &= 0x7f;
    }
    const bool should_fail = (scenario >= 1 && scenario <= 7) || scenario == 11 || scenario == 14;
    esp_err_t result = bsp_battery_init();
    if (should_fail) {
        assert(result != ESP_OK && removals == 1);
        assert(bsp_battery_soc() == -1 && bsp_battery_mv() == -1);
        if (scenario == 7) {
            assert(result == ESP_ERR_TIMEOUT && soc_attempts == 50);
            assert(elapsed_ms >= 5000 && elapsed_ms < 5200);
        }
        // Partial upload / flag failure / timeout must permit a fresh retry.
        scenario = 0;
        writes = profile_writes = verified_bytes = 0;
        soc_attempts = 0;
        assert(bsp_battery_init() == ESP_OK);
    } else assert(result == ESP_OK);
    if (original_scenario == 8) assert(writes == 0 && profile_writes == 0);
    if (original_scenario == 10) assert(writes == 2 && profile_writes == 0);
    if (original_scenario == 0 || original_scenario == 9 || original_scenario == 12)
        assert(profile_writes == 80 && verified_bytes == 80);
    if (original_scenario == 13) assert(soc_attempts == 3);
    assert(memcmp(registers + 0x10, expected_profile, 80) == 0);
    assert(registers[0x0b] == 0x94 && registers[8] == 0);
    unsigned previous_writes = writes;
    assert(bsp_battery_init() == ESP_OK && writes == previous_writes);
    calculate_soc = false;
    assert(bsp_battery_soc() == 63);
    registers[4] = 0xfe;
    assert(bsp_battery_soc() == -1);
    registers[2] = 0x31; registers[3] = 0x40;
    assert(bsp_battery_mv() == 3940); // runtime voltage fallback remains usable
    registers[4] = 0xff;
    assert(bsp_battery_soc() == -1);
    registers[4] = 100;
    assert(bsp_battery_soc() == 100);
    registers[4] = 0;
    assert(bsp_battery_soc() == 0);
    registers[2] = 0; registers[3] = 0;
    assert(bsp_battery_mv() == -1);
    registers[2] = 0x3f; registers[3] = 0xff;
    assert(bsp_battery_mv() == -1);
    read_failure = true;
    assert(bsp_battery_soc() == -1 && bsp_battery_mv() == -1);
    printf("BSP battery scenario %d: PASS\n", original_scenario);
    return 0;
}
