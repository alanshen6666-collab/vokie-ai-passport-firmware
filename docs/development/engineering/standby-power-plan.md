<p align="right">
  <a href="standby-power-plan.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Standby power implementation and acceptance

## Dependency graph and ownership

| Stage       | Depends on     | Files / owner                                                               | Acceptance                                                   |
| ----------- | -------------- | --------------------------------------------------------------------------- | ------------------------------------------------------------ |
| Contract    | None           | These three document pairs / primary agent                                  | Interfaces and limitations recorded                          |
| Audio       | Contract       | BSP audio and BLE audio worker / primary agent                              | Idle shutdown, repeated reopen and failed-open cleanup tests |
| Display     | Contract       | BSP display/LVGL, UI worker, idle policy / primary agent                    | Timing, duplicate states, pause/resume and wake tests        |
| Integration | Audio, Display | PM defaults, main startup, CMake, validation, documentation / primary agent | Complete repository gate and firmware image verification     |
| Device      | Integration    | Physical board and current meter / user-assisted                            | Battery-side current, BLE, audio, all buttons, USB console survival |

There are no delegated or parallel editing workers. The primary agent owns all changes and integrates serially. Existing uncommitted battery documentation is preserved.

## Validation and remaining risk

Run focused host tests during implementation, then `./tools/validate.sh` in ESP-IDF 5.5.3. The complete gate must retain the 3 MB app limit, protected offsets, and Recovery hook. Confirm generated PM and Bluetooth settings match the intended defaults.

On device, compare battery-side current for disconnected screen-off, connected screen-off, lit idle and recording. Test at least 20 sleep/wake/record/stop cycles, all three keys including long press, first-word capture, boot sound interruption, disconnect during capture, and reconnect while off. Verify the USB console survives host-side serial-port probing while attached and that battery light sleep returns within the release grace after unplugging. Observe without a USB debugger for final current readings. No flashing, current measurement, or physical acceptance is implied by a successful build.
