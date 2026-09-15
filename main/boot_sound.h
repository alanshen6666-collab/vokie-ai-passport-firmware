#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Play borrowed 16 kHz, 16-bit mono PCM once on the audio worker, after the
// codec is opened and before microphone capture. Uses a bounded stack buffer;
// never changes the codec format or microphone gain. The callback interrupts
// playback when recording is requested. Always silence the output on exit.
esp_err_t boot_sound_play(const uint8_t *pcm, size_t bytes,
                          bool (*recording_requested)(void));
