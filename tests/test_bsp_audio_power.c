#include "bsp_audio.h"
#include "bsp_i2c.h"
#include "esp_codec_dev.h"
#include "esp_pm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tx, rx, token, locks, opens, closes, volume;
static bool fail_open, fail_tx, fail_rx, codec_on;
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
void *bsp_i2c_bus(void) { return &token; }
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t *c) { (void)c; return &token; }
const audio_codec_data_if_t *audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t *c) { (void)c; return &token; }
void *audio_codec_new_gpio(void) { return &token; }
const audio_codec_if_t *es8311_codec_new(const es8311_codec_cfg_t *c) { assert(c->no_dac_ref); return &token; }
esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t *c) { (void)c; return &token; }
esp_err_t i2s_new_channel(const i2s_chan_config_t *c, i2s_chan_handle_t *t, i2s_chan_handle_t *r) { (void)c; *t=&tx; *r=&rx; return ESP_OK; }
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t c, const i2s_std_config_t *cfg) { (void)cfg; *c=0; return ESP_OK; }
esp_err_t i2s_channel_enable(i2s_chan_handle_t c) {
    assert(locks == 1 && *c == 0);
    if ((c == &tx && fail_tx) || (c == &rx && fail_rx)) return ESP_FAIL;
    *c=1; return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t c) { assert(*c == 1); *c=0; return ESP_OK; }
esp_err_t esp_pm_lock_create(int type, int arg, const char *name, esp_pm_lock_handle_t *out) {
    (void)arg; (void)name; assert(type == ESP_PM_NO_LIGHT_SLEEP); *out=&token; return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t l) { (void)l; assert(locks++ == 0); return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t l) { (void)l; assert(locks-- == 1 && !tx && !rx && !codec_on); return ESP_OK; }
int esp_codec_dev_open(esp_codec_dev_handle_t d, esp_codec_dev_sample_info_t *fs) {
    (void)d; assert(locks == 1 && tx && rx); assert(fs->channel_mask == 1);
    ++opens; codec_on=true;
    // Model the driver's partial-open failure: I2S and internal flags are live.
    return fail_open ? -1 : 0;
}
int esp_codec_dev_close(esp_codec_dev_handle_t d) { (void)d; assert(locks == 1); tx=rx=0; codec_on=false; ++closes; return 0; }
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t d, float gain) { (void)d; assert(gain == 30.0f); return 0; }
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t d, int v) { (void)d; volume=v; return 0; }
int esp_codec_dev_read(esp_codec_dev_handle_t d, void *p, int n) { (void)d; assert(tx && rx && codec_on && locks == 1); memset(p, 1, n); return 0; }
int esp_codec_dev_write(esp_codec_dev_handle_t d, void *p, int n) { (void)d; (void)p; (void)n; assert(tx && rx && locks == 1); return 0; }

static void idle(void) {
    char pcm[8];
    assert(!tx && !rx && !codec_on && !locks);
    assert(bsp_audio_read(pcm, sizeof(pcm)) == ESP_ERR_INVALID_STATE);
    assert(bsp_audio_write(pcm, sizeof(pcm)) == ESP_ERR_INVALID_STATE);
    assert(bsp_audio_suspend() == ESP_OK);
}
int main(void) {
    assert(bsp_audio_set_format(16000,16,1) == ESP_ERR_INVALID_STATE);
    assert(bsp_audio_init() == ESP_OK);
    idle();
    for (int i=0; i<30; ++i) {
        int old=opens; char pcm[8];
        assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
        assert(opens == old+1 && locks == 1);
        assert(bsp_audio_set_format(16000,16,1) == ESP_OK && opens == old+1);
        assert(bsp_audio_read(pcm,sizeof(pcm)) == ESP_OK);
        assert(bsp_audio_suspend() == ESP_OK && volume == 0);
        int closed=closes; idle(); assert(closes == closed);
    }
    fail_open=true;
    assert(bsp_audio_set_format(16000,16,1) == ESP_FAIL); idle();
    fail_open=false; fail_tx=true;
    assert(bsp_audio_set_format(16000,16,1) == ESP_FAIL); idle();
    fail_tx=false; fail_rx=true;
    assert(bsp_audio_set_format(16000,16,1) == ESP_FAIL); idle();
    fail_rx=false;
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
    assert(bsp_audio_set_format(8000,16,1) == ESP_OK && locks == 1);
    assert(bsp_audio_suspend() == ESP_OK); idle();
    puts("Audio power lifecycle tests: PASS");
}
