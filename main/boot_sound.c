#include "boot_sound.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "boot_sound";

// 20 ms per chunk at the microphone's existing format. Six chunks cover
// 120 ms, longer than the BSP's six 240-frame DMA descriptors (90 ms).
#define CHUNK_SAMPLES 320
#define DRAIN_CHUNKS 6
#define BOOT_VOLUME 55

esp_err_t boot_sound_play(const uint8_t *pcm, size_t bytes,
                          bool (*recording_requested)(void))
{
    bsp_audio_set_volume(0);
    if (!pcm || bytes == 0 || bytes % sizeof(int16_t) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (recording_requested && recording_requested()) return ESP_OK;

    int16_t chunk[CHUNK_SAMPLES];
    esp_err_t result = ESP_OK;
    size_t offset = 0;
    bool interrupted = false;
    bsp_audio_set_volume(BOOT_VOLUME);
    ESP_LOGI(TAG, "Playing boot sound (%u bytes, volume %u)",
             (unsigned)bytes, BOOT_VOLUME);
    while (offset < bytes) {
        if (recording_requested && recording_requested()) {
            interrupted = true;
            break;
        }
        size_t amount = bytes - offset;
        if (amount > sizeof(chunk)) amount = sizeof(chunk);
        memcpy(chunk, pcm + offset, amount);
        result = bsp_audio_write(chunk, amount);
        if (result != ESP_OK) break;
        offset += amount;
    }

    // Let the natural tail leave DMA before muting. A voice request instead
    // mutes immediately, so no queued startup samples play over the user.
    if (interrupted || result != ESP_OK) bsp_audio_set_volume(0);
    memset(chunk, 0, sizeof(chunk));
    if (result == ESP_OK) {
        for (unsigned i = 0; i < DRAIN_CHUNKS; ++i) {
            if (recording_requested && recording_requested()) {
                interrupted = true;
                bsp_audio_set_volume(0);
            }
            result = bsp_audio_write(chunk, sizeof(chunk));
            if (result != ESP_OK) break;
        }
    }
    bsp_audio_set_volume(0);

    // Drop microphone samples collected while the speaker was playing. The
    // same worker owns playback and capture, so these never reach the host.
    if (result == ESP_OK) {
        for (unsigned i = 0; i < DRAIN_CHUNKS; ++i) {
            result = bsp_audio_read(chunk, sizeof(chunk));
            if (result != ESP_OK) break;
        }
    }
    ESP_LOGI(TAG, "Boot sound %s (%u/%u bytes): %s",
             interrupted ? "interrupted for recording" : "finished",
             (unsigned)offset, (unsigned)bytes, esp_err_to_name(result));
    return result;
}
