<p align="right">
  <a href="standby-power-contract.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Standby power contract

## Ownership and interfaces

The audio worker is the sole runtime owner of codec open/read/write/suspend. `bsp_audio_suspend(void)` closes the stream and releases its sleep lock; repeating it is harmless. `bsp_audio_set_format()` reopens a suspended stream even with the same format. Failure cleanup must stop both I2S channels and allow retry. No codec operation runs in button callbacks.

`bsp_lvgl_set_paused(bool paused)` is called with the LVGL lock held. It pauses display refresh and animation timers and the port tick while off, and restores elapsed time on resume. The LVGL timer handler must remain enabled: disabling it in LVGL 9.5 returns a 1 ms delay and creates a polling loop. Battery updates cannot resume the display.

UI producers update a protected state snapshot and notify the worker; only that worker changes LVGL objects and backlight after initialization. Repeated identical host states do not reset the idle deadline. Brightness stays 65/38/18/0 percent, with 3-second dim and 20-second off deadlines; recording and processing retain the existing lit-screen behavior.

## Compatibility and failure policy

BLE V1, ADPCM framing, 16 kHz capture, boot sound priority, ADC key mapping, partitions and Recovery remain unchanged. Task notifications supplement state checks so an event arriving during audio shutdown cannot be lost. A failed audio start reports an error and returns to idle. Pausing LVGL must be reversible and checked for errors. All PM locks are balanced across repeated transitions.

USB connection monitoring holds a no-light-sleep lock while native USB Serial/JTAG is attached; unplugging permits battery standby again. This prevents console loss during startup and screen-off.
