<p align="right">
  <a href="standby-power-contract.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Standby power contract

## Ownership and interfaces

The audio worker is the sole runtime owner of codec open/read/write/pause/suspend. `bsp_audio_pause(void)` stops both I2S channels and releases the audio sleep lock while retaining ES8311 bias/filter state. `bsp_audio_set_format()` resumes fresh DMA for the same format without resetting the codec; the first 16-bit mono read primes one partial I2S slot word (62.5 us at 16 kHz) before returning the complete requested frame. `bsp_audio_suspend()` fully closes the codec for teardown or a format change. Lifecycle calls are idempotent and serialized outside button callbacks. A partial pause retains its lock until all live channels stop; a failed resume supports retry. Retained analog power has a current cost that requires measurement.

`bsp_lvgl_set_paused(bool paused)` is called with the LVGL lock held. It pauses display refresh and animation timers and the port tick while off, and restores elapsed time on resume. The LVGL timer handler must remain enabled: disabling it in LVGL 9.5 returns a 1 ms delay and creates a polling loop. Battery updates cannot resume the display.

UI producers update a protected state snapshot and notify the worker; only that worker changes LVGL objects and backlight after initialization. Repeated identical host states do not reset the idle deadline. Brightness stays 65/38/18/0 percent, with 3-second dim and 20-second off deadlines; recording and processing retain the existing lit-screen behavior.

## Compatibility and failure policy

BLE V1, ADPCM framing, 16 kHz capture, boot sound priority, ADC key mapping, partitions and Recovery remain unchanged. Task notifications supplement state checks so an event arriving during audio pause cannot be lost. A failed audio start reports an error and returns to idle. Pausing LVGL must be reversible and checked for errors. All PM locks are balanced across repeated transitions.

USB standby polling (`main/usb_standby.c`) holds a no-light-sleep lock plus an 80 MHz APB-frequency floor while the native USB Serial/JTAG bus is active, with a five-second release grace for short host-side interruptions; an unplugged or long-suspended bus permits battery standby again. A failed audio pause retries or reports an error and returns to idle; it must not abort the device. Host suspend/replug behavior requires separate hardware acceptance.
