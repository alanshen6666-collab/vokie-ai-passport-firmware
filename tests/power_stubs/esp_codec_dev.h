#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "driver/i2s_std.h"
typedef void *esp_codec_dev_handle_t;
typedef int audio_codec_ctrl_if_t;
typedef int audio_codec_data_if_t;
typedef int audio_codec_if_t;
typedef struct { int port, addr; void *bus_handle; } audio_codec_i2c_cfg_t;
typedef struct { int port; i2s_chan_handle_t tx_handle, rx_handle; } audio_codec_i2s_cfg_t;
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if; void *gpio_if;
    int codec_mode, pa_pin;
    bool pa_reverted, master_mode, use_mclk, no_dac_ref;
    struct { float pa_voltage, codec_dac_voltage; } hw_gain;
} es8311_codec_cfg_t;
typedef struct { int dev_type; const audio_codec_if_t *codec_if; const audio_codec_data_if_t *data_if; } esp_codec_dev_cfg_t;
typedef struct { int bits_per_sample, channel, channel_mask, sample_rate, mclk_multiple; } esp_codec_dev_sample_info_t;
#define ESP_CODEC_DEV_WORK_MODE_BOTH 3
#define ESP_CODEC_DEV_TYPE_IN_OUT 3
#define ESP_CODEC_DEV_MAKE_CHANNEL_MASK(n) (1 << (n))
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *);
const audio_codec_data_if_t *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *);
void *audio_codec_new_gpio(void);
const audio_codec_if_t *es8311_codec_new(const es8311_codec_cfg_t *);
esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t *);
int esp_codec_dev_open(esp_codec_dev_handle_t, esp_codec_dev_sample_info_t *);
int esp_codec_dev_close(esp_codec_dev_handle_t);
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t, float);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t, int);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t, bool);
int esp_codec_dev_read_reg(esp_codec_dev_handle_t, int, int *);
int esp_codec_dev_write_reg(esp_codec_dev_handle_t, int, int);
int esp_codec_dev_read(esp_codec_dev_handle_t, void *, int);
int esp_codec_dev_write(esp_codec_dev_handle_t, void *, int);
