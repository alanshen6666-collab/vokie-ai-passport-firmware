#include "boot_sound.h"
#include "bsp_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t source[1282]; // Two full chunks and one sample: exercise the tail.
static size_t consumed;
static unsigned writes, reads, silent_writes, volume;
static unsigned fail_write, fail_read, interrupt_after;
static bool cancel;

void bsp_audio_set_volume(uint8_t percent) { volume = percent; }

esp_err_t bsp_audio_write(const void *pcm, size_t bytes)
{
    assert(bytes > 0 && bytes <= 640 && bytes % 2 == 0);
    if (++writes == fail_write) return ESP_FAIL;
    const uint8_t *p = pcm;
    if (p[0] != 0) {
        assert(volume == 55);
        assert(consumed + bytes <= sizeof(source));
        assert(memcmp(pcm, source + consumed, bytes) == 0);
        consumed += bytes;
    } else {
        for (size_t i = 0; i < bytes; ++i) assert(p[i] == 0);
        ++silent_writes;
    }
    return ESP_OK;
}

esp_err_t bsp_audio_read(void *pcm, size_t bytes)
{
    assert(volume == 0); // Startup capture is discarded only after muting.
    assert(bytes == 640);
    if (++reads == fail_read) return ESP_FAIL;
    memset(pcm, 0x22, bytes);
    return ESP_OK;
}

static bool recording_requested(void)
{
    return cancel && writes >= interrupt_after;
}

static void reset(void)
{
    memset(source, 0x11, sizeof(source));
    consumed = writes = reads = silent_writes = 0;
    fail_write = fail_read = interrupt_after = 0;
    cancel = false;
    volume = 80;
}

int main(void)
{
    reset();
    assert(boot_sound_play(source, sizeof(source), recording_requested) == ESP_OK);
    assert(consumed == sizeof(source) && silent_writes == 6 && reads == 6);
    assert(volume == 0);

    reset(); cancel = true;
    assert(boot_sound_play(source, sizeof(source), recording_requested) == ESP_OK);
    assert(writes == 0 && reads == 0 && volume == 0);

    reset(); cancel = true; interrupt_after = 1;
    assert(boot_sound_play(source, sizeof(source), recording_requested) == ESP_OK);
    assert(consumed == 640 && silent_writes == 6 && reads == 6 && volume == 0);

    // Fail during the clip and during silence; never leave the speaker active.
    for (unsigned failure = 1; failure <= 9; ++failure) {
        reset(); fail_write = failure;
        assert(boot_sound_play(source, sizeof(source), NULL) == ESP_FAIL);
        assert(volume == 0 && reads == 0);
    }
    reset(); fail_read = 1;
    assert(boot_sound_play(source, sizeof(source), NULL) == ESP_FAIL);
    assert(volume == 0 && reads == 1);

    reset();
    assert(boot_sound_play(NULL, 2, NULL) == ESP_ERR_INVALID_ARG);
    assert(boot_sound_play(source, 0, NULL) == ESP_ERR_INVALID_ARG);
    assert(boot_sound_play(source, 1, NULL) == ESP_ERR_INVALID_ARG);
    assert(writes == 0 && reads == 0 && volume == 0);
    puts("Boot sound tests: PASS");
    return 0;
}
