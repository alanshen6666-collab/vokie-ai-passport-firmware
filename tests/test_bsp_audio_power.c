#include "bsp_audio.h"
#include "bsp_i2c.h"
#include "esp_codec_dev.h"
#include "esp_pm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int tx, rx, token, locks, opens, closes, volume, prime_reads;
static bool first_rx_word, fail_prime;
static bool fail_open, fail_tx, fail_rx, fail_stop_tx, fail_stop_rx, codec_on;
static int dac_power, dac_mute;
static bool fail_mute_write, fail_power_write, fail_register_read;

static bool speaker_off(void) {
    return (dac_mute & 0x60) == 0x60 && (dac_power & 0x02) != 0;
}
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
    if (c == &rx) first_rx_word = true;
    *c=1; return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t c) {
    assert(*c == 1 && locks == 1);
    // The board's amplifier is always on: attenuation alone cannot protect
    // the speaker when its DAC loses the shared I2S clock.
    if (codec_on) assert(speaker_off());
    if ((c == &tx && fail_stop_tx) || (c == &rx && fail_stop_rx)) return ESP_FAIL;
    *c=0; return ESP_OK;
}
esp_err_t esp_pm_lock_create(int type, int arg, const char *name, esp_pm_lock_handle_t *out) {
    (void)arg; (void)name; assert(type == ESP_PM_NO_LIGHT_SLEEP); *out=&token; return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t l) { (void)l; assert(locks++ == 0); return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t l) { (void)l; assert(locks-- == 1 && !tx && !rx); return ESP_OK; }
int esp_codec_dev_open(esp_codec_dev_handle_t d, esp_codec_dev_sample_info_t *fs) {
    (void)d; assert(locks == 1 && tx && rx); assert(fs->channel_mask == 1);
    ++opens; codec_on=true;
    dac_power=0x40; dac_mute=0x15; // Unrelated register bits must survive.
    // Model the driver's partial-open failure: I2S and internal flags are live.
    return fail_open ? -1 : 0;
}
int esp_codec_dev_close(esp_codec_dev_handle_t d) { (void)d; assert(locks == 1); tx=rx=0; codec_on=false; ++closes; return 0; }
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t d, float gain) { (void)d; assert(gain == 30.0f); return 0; }
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t d, int v) { (void)d; volume=v; return 0; }
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t d, bool muted) {
    (void)d;
    // The pinned codec wrapper does not propagate an underlying I2C error.
    if (!fail_mute_write) dac_mute=(dac_mute & ~0x60) | (muted ? 0x60 : 0);
    return 0;
}
int esp_codec_dev_read_reg(esp_codec_dev_handle_t d, int reg, int *value) {
    (void)d;
    if (fail_register_read) return -1;
    assert(reg == 0x12 || reg == 0x31);
    *value = reg == 0x12 ? dac_power : dac_mute;
    return 0;
}
int esp_codec_dev_write_reg(esp_codec_dev_handle_t d, int reg, int value) {
    (void)d; assert(reg == 0x12);
    if (fail_power_write) return -1;
    dac_power=value; return 0;
}
int esp_codec_dev_read(esp_codec_dev_handle_t d, void *p, int n) {
    (void)d; assert(tx && rx && codec_on && locks == 1);
    if (first_rx_word) {
        assert(n == 2); // Consume exactly one invalid mono word after restart.
        if (fail_prime) return -1;
        memset(p, 0x80, n); first_rx_word=false; ++prime_reads;
    } else { memset(p, 1, n); }
    return 0;
}
int esp_codec_dev_write(esp_codec_dev_handle_t d, void *p, int n) { (void)d; (void)p; (void)n; assert(tx && rx && locks == 1); return 0; }

static void paused(void) {
    char pcm[8];
    assert(codec_on && !tx && !rx && !locks);
    assert(speaker_off());
    assert(bsp_audio_read(pcm, sizeof(pcm)) == ESP_ERR_INVALID_STATE);
    assert(bsp_audio_write(pcm, sizeof(pcm)) == ESP_ERR_INVALID_STATE);
    assert(bsp_audio_pause() == ESP_OK);
}
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
        assert(speaker_off());
        assert(opens == old+1 && locks == 1);
        assert(bsp_audio_set_format(16000,16,1) == ESP_OK && opens == old+1);
        assert(bsp_audio_read(pcm,sizeof(pcm)) == ESP_OK);
        assert(bsp_audio_suspend() == ESP_OK && volume == 0);
        int closed=closes; idle(); assert(closes == closed);
    }
    // Standby must resume without resetting codec bias/filter state, while
    // allowing light sleep and rejecting reads of old DMA when paused.
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
    // Boot playback can enable the DAC only while both clocks are running.
    bsp_audio_set_volume(55);
    assert(volume == 55 && dac_power == 0x40 && dac_mute == 0x15);
    bsp_audio_set_volume(0);
    assert(volume == 0 && speaker_off());
    assert(dac_power == 0x42 && dac_mute == 0x75);
    bsp_audio_set_volume(55);
    assert(bsp_audio_pause() == ESP_OK); paused();
    bsp_audio_set_volume(55); // Never re-enable an unclocked output.
    assert(speaker_off());
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK && speaker_off());

    // A false-success mute must not permit clock removal. Both read and
    // power failures retain the live stream/lock and can be retried.
    bsp_audio_set_volume(55); fail_mute_write=true;
    assert(bsp_audio_pause() == ESP_FAIL && tx && rx && locks == 1);
    fail_mute_write=false;
    assert(bsp_audio_pause() == ESP_OK); paused();
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
    bsp_audio_set_volume(55); fail_power_write=true;
    assert(bsp_audio_pause() == ESP_FAIL && tx && rx && locks == 1);
    fail_power_write=false;
    assert(bsp_audio_pause() == ESP_OK); paused();
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
    fail_register_read=true;
    assert(bsp_audio_pause() == ESP_FAIL && tx && rx && locks == 1);
    fail_register_read=false;
    assert(bsp_audio_pause() == ESP_OK); paused();
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
    int warm_opens=opens, warm_closes=closes;
    for (int i=0; i<30; ++i) {
        assert(bsp_audio_pause() == ESP_OK && volume == 0); paused();
        assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
        assert(opens == warm_opens && closes == warm_closes);
        char pcm[640]; int primed=prime_reads;
        assert(bsp_audio_read(pcm,sizeof(pcm)) == ESP_OK);
        assert(prime_reads == primed+1);
        for (unsigned j=0;j<sizeof(pcm);j++) assert(pcm[j] == 1);
        assert(bsp_audio_read(pcm,sizeof(pcm)) == ESP_OK && prime_reads == primed+1);
    }
    assert(bsp_audio_pause() == ESP_OK);
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
    char retry_pcm[640]; fail_prime=true;
    assert(bsp_audio_read(retry_pcm,sizeof(retry_pcm)) == ESP_FAIL);
    fail_prime=false;
    assert(bsp_audio_read(retry_pcm,sizeof(retry_pcm)) == ESP_OK);
    for (unsigned j=0;j<sizeof(retry_pcm);j++) assert(retry_pcm[j] == 1);
    // A partially stopped duplex stream must keep its lock and support retry.
    fail_stop_rx=true;
    assert(bsp_audio_pause() == ESP_FAIL && tx && rx && locks == 1);
    fail_stop_rx=false; fail_stop_tx=true;
    assert(bsp_audio_pause() == ESP_FAIL && tx && !rx && locks == 1);
    fail_stop_tx=false;
    assert(bsp_audio_pause() == ESP_OK); paused();
    // Failure starting either channel leaves a retryable paused codec.
    fail_tx=true;
    assert(bsp_audio_set_format(16000,16,1) == ESP_FAIL); paused();
    fail_tx=false; fail_rx=true;
    assert(bsp_audio_set_format(16000,16,1) == ESP_FAIL); paused();
    // A failed cleanup must retain the lock until the live TX can stop.
    fail_stop_tx=true;
    assert(bsp_audio_set_format(16000,16,1) == ESP_FAIL && locks == 1 && tx);
    fail_rx=false; fail_stop_tx=false;
    assert(bsp_audio_pause() == ESP_OK); paused();
    assert(bsp_audio_set_format(16000,16,1) == ESP_OK);
    assert(opens == warm_opens && closes == warm_closes);
    // Format changes really close/reopen, including from paused state.
    assert(bsp_audio_pause() == ESP_OK);
    assert(bsp_audio_set_format(8000,16,1) == ESP_OK);
    assert(opens == warm_opens+1 && closes == warm_closes+1);
    assert(bsp_audio_pause() == ESP_OK);
    assert(bsp_audio_suspend() == ESP_OK); idle();
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
