<p align="right">
  <a href="AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# FoloToy AI Passport Hardware Guide for Vokie Firmware

This is the board-level context for AI coding assistants and new developers. It records confirmed hardware facts, software architecture, invariants, extension points, and acceptance methods; it does not replace component datasheets.

> For firmware behavior, use `components/bsp/include/bsp_pins.h` and the BSP implementation as the source of truth. Do not copy assumptions from a generic ESP32-C3 board.

Document scope:

- Applicable target: the ESP32-C3 FoloToy AI Passport mapping used by this repository.
- Product specifications are in [specifications.md](specifications.md); firmware behavior follows `bsp_pins.h`, BSP implementations, `sdkconfig.defaults`, `partitions.csv`, and the Vokie application sources.
- Code audit date: 2026-09-02.

## 1. Before changing hardware-facing code

1. Read `AGENTS.md`, this guide, and the affected BSP header/implementation.
2. Run `git status --short --branch` and preserve unrelated changes.
3. Put reusable hardware behavior in `components/bsp`; keep Vokie product interaction, BLE transport, and status UI in `main`.
4. Keep pins, I2C addresses, and panel dimensions in `bsp_pins.h` only.
5. Keep hardware-facing changes within the product specification and explicit BSP definitions.

## 2. Board overview

The target is the ESP32-C3 FoloToy AI Passport with ESP-IDF 5.5.3. It has 8 MB Flash and no PSRAM; display, audio, radio, tasks, and DMA compete for internal RAM.

| Subsystem | Device or mode | Resource | Firmware support |
| --- | --- | --- | --- |
| MCU | ESP32-C3 | 8 MB Flash, no PSRAM | Configured |
| Display | ST7789P3, 240 × 320, RGB565 | SPI2, 40 MHz, mode 0 | Status UI |
| Backlight | LCD LED | GPIO21, LEDC 5 kHz/10 bit | Activity and timeout brightness policy |
| Buttons | UP/DOWN/OK resistor ladder | GPIO0 / ADC1_CH0 | PTT, send, delete, clear, and cancel events |
| Audio | ES8311 microphone capture | shared I2C + I2S0 full duplex | 16 kHz mono PCM for BLE transport |
| Battery | CW2017 fuel gauge | shared I2C0, address `0x63` | Status UI SOC percentage, or measured voltage when SOC is invalid |
| Wi-Fi | Not used by the current firmware | no Wi-Fi application path | Not compiled into the Vokie application |
| Bluetooth LE | NimBLE peripheral | Vokie BLE V1 service | Connectable advertising and notifications |
| Low power | Event-driven idle and automatic light sleep | ESP-IDF PM + BLE modem sleep | Audio/backlight sleep locks; ADC keys scanned every 20 ms; no deep sleep |
| Console | USB Serial/JTAG | native USB GPIO18/19 | Configured |

## 3. Pin map and resource ownership

This table describes the signals allocated by the current BSP and build configuration.

| GPIO | Function | Direction/peripheral | Notes |
| ---: | --- | --- | --- |
| 0 | three-button ADC node | ADC1_CH0 input | external 10 kΩ pull-up; boot-related pin |
| 1 | LCD CS | SPI output | ST7789P3 chip select |
| 2 | I2S DOUT | output | MCU to ES8311 |
| 3 | I2S WS | output | MCU is I2S master |
| 4 | I2S DIN | input | ES8311 to MCU |
| 5 | I2S BCLK | output | shared by TX/RX |
| 6 | I2S MCLK | output | required by codec configuration |
| 7 | I2C SCL | bidirectional open drain | ES8311 and CW2017 share I2C0 |
| 8 | LCD SCLK | SPI output | SPI2, 40 MHz, mode 0 |
| 9 | LCD MOSI | SPI output | no MISO; display cannot be read |
| 10 | I2C SDA | bidirectional open drain | internal pull-up enabled; suitable external pull-ups still expected |
| 18/19 | USB Serial/JTAG | USB | reserve for console |
| 20 | LCD DC | output | command/data select |
| 21 | backlight PWM | LEDC output | conflicts with common UART0 default TX |

LCD reset and amplifier enable are `-1`: display reset uses software reset, and the amplifier is treated as always enabled. A GPIO absent from this table is not automatically free.

### 3.1 Peripheral ownership and coexistence

| Resource | Owner | Sharing rule or conflict |
| --- | --- | --- |
| SPI2 | display BSP | Dedicated to the ST7789P3 in current firmware; no MISO is configured. |
| LEDC low-speed timer 0/channel 0 | backlight BSP | New PWM users must select a non-conflicting timer/channel and recheck clock changes. |
| ADC1 / channel 0 | button BSP | One oneshot unit is shared by button decoding and live-voltage reads; do not create a second ADC1 owner. |
| I2C0 | `bsp_i2c` | ES8311 and CW2017 share the single bus handle; clients must not recreate the bus. |
| I2S0 | audio BSP | TX and RX are full duplex and share MCLK/BCLK/WS. |
| USB Serial/JTAG | console configuration | GPIO18/19 are part of the selected console path. |
| Internal RAM/DMA | display, LVGL, audio, NimBLE, tasks | No PSRAM exists; total free heap and largest contiguous block both matter. |
| NVS | `app_main` and ESP-IDF services | Initialize without erasing provisioned identity data; preserve the protected partition contract. |
| NimBLE | `vokie_ble.c` | One connectable Vokie BLE V1 peripheral; audio and control notifications share one connection. |

GPIO0 is both the button ADC node and an ESP32-C3 boot-related pin. GPIO21 is the backlight output and conflicts with the commonly used UART0 TX mapping. Pin reassignment requires boot/programming-path review and on-device acceptance.

### 3.2 Product interfaces outside the BSP

- USB Type-C 2.0 accepts 5 V input; firmware flashing and logs use the ESP32-C3 USB Serial/JTAG interface.
- The dedicated power button controls hardware power and is separate from the three ADC function buttons exposed by the BSP.
- The NTAG213 is a passive NFC tag and has no MCU-facing BSP API.
- LCD reset uses the controller's software-reset path, and amplifier enable is not controlled by an MCU GPIO.

## 4. Architecture and lifecycle

```text
app_main
  ├─ shared I2C init
  ├─ display and LVGL init, then status UI
  └─ Vokie BLE V1 init
       ├─ button events
       ├─ microphone PCM capture
       └─ control/audio notifications
```

Display/LVGL is a hard dependency for the status UI. Audio and buttons are initialized by `vokie_ble_start()`; failure is logged and the application cannot provide voice input. The application initializes the optional CW2017 through the BSP before starting UI and audio workers; initialization failure does not stop startup. Public BSP APIs are under `components/bsp/include/`; most initialization is idempotent, but there is no universal BSP deinitialization API.

NimBLE, the Vokie BLE V1 service, and the audio worker use ESP-IDF directly rather than the BSP. The application maintains one connectable BLE session, requires both control and audio notifications to be subscribed, and sends 20 ms microphone frames only after the host is ready. Do not erase NVS to hide partition errors; the protected identity and Recovery layout is outside the community application image.

## 5. Display and LVGL

- Panel: ST7789P3, 240 × 320 portrait, RGB565, SPI2 MOSI-only at 40 MHz, mode 0.
- `BSP_LCD_INVERT_COLOR=1`; change inversion only after measurement with the replacement panel.
- Reset is software-only, gap is `(0, 0)`, X/Y mirroring is disabled, and LVGL rotation may override lower-level mirror settings.
- The vendor porch, power, and gamma sequence in `bsp_display.c` is panel-specific. Do not treat it as a universal ST7789 sequence.
- `swap_bytes=true` is required because LVGL emits little-endian RGB565 while SPI sends the high byte first.

The LVGL DMA buffer is one `240 × 20` RGB565 buffer, about 9.6 KB; the LVGL internal pool is 24 KB. Do not add large/double buffers without checking internal RAM, the largest contiguous heap block, and I2S DMA.

LVGL is not thread-safe. Timer callbacks in LVGL context may access objects directly. Button callbacks and worker tasks must use `bsp_lvgl_lock()`/`bsp_lvgl_unlock()`. Stop producers before deleting a page and clear static object pointers afterward.

## 6. ADC button ladder

GPIO0 has an external 10 kΩ pull-up to 3.3 V. UP, DOWN, and OK connect it to ground through 0 Ω, 1 kΩ, and 2.2 kΩ respectively.

| State | Nominal voltage | Current window |
| --- | ---: | ---: |
| UP | about 0 mV | `[0, 150)` mV |
| DOWN | about 300 mV | `[150, 447)` mV |
| OK | about 595 mV | `[447, 1900)` mV |
| Released | about 3300 mV | outside all windows |

Do not replace the external resistor with the inaccurate internal pull-up. The BSP creates one ADC1 oneshot unit and shares it with all button devices and voltage reads. Attenuation is `ADC_ATTEN_DB_12`. Callbacks originate in the button component task and must not block or perform heavy UI work.

Calibrate thresholds using multiple boards, charge levels, and reasonable temperatures; leave margin between measured distributions rather than relying only on divider theory.

## 7. Shared I2C

I2C0 uses SDA GPIO10 and SCL GPIO7. ES8311 is 7-bit address `0x18`; CW2017 is `0x63`. `bsp_i2c.c` exclusively owns the bus.

- Never create a second temporary bus on the same port for probing or a device.
- Scan with `i2c_master_probe()` on the existing bus. The scan covers `0x08` through `0x77`; success means the scan completed, not that a device was found.
- CW2017 runs at 100 kHz. ES8311 control is managed by `esp_codec_dev`.
- The codec control API expects an 8-bit address, so ES8311 receives `0x18 << 1`; do not copy that shift into 7-bit ESP-IDF APIs.

Troubleshoot in order: bus-init log, scan results for `0x18`/`0x63`, power/ground/wiring/pull-ups, address format, and accidental duplicate-bus creation.

## 8. ES8311 audio

The MCU is I2S master and the ES8311 is slave. I2S0 TX/RX shares MCLK GPIO6, BCLK GPIO5, and WS GPIO3; DOUT is GPIO2 and DIN is GPIO4. The Vokie application opens 16 kHz, 16-bit, mono PCM over a physically two-slot standard-I2S bus.

- Call `bsp_audio_set_format()` before PCM I/O.
- A format change must close and reopen `esp_codec_dev`; an already open device is not reconfigured.
- Preserve the I2S enable/disable sequence around close/open.
- Do not write ES8311 clock-divider registers after open; the driver derives them from sample rate and 256×fs MCLK.
- Keep `no_dac_ref=true` for mono microphone input; false can produce all-zero capture.
- Microphone analog gain is 30 dB; output volume is a separate 0–100% value.
- `bsp_audio_read/write` block and must not run in button callbacks or the LVGL task.
- I2S DMA uses six descriptors of 240 frames each.

The Vokie audio worker keeps one 320-sample PCM buffer and one 166-byte ADPCM buffer on its task stack, then streams each encoded 20 ms frame over BLE. Keep capture bounded and cancellable; do not replace this path with a large recording buffer or delete a task that may be blocked in codec I/O.

## 8.1. Standby power lifecycle

The audio worker owns stream open/read/write/pause/suspend. After the boot sound and each recording, `bsp_audio_pause()` stops both I2S channels and releases the audio no-light-sleep lock, while retaining ES8311 bias/filter state. The worker blocks on a task notification. Resuming the same format starts fresh DMA without closing/reopening the codec. The first 16-bit mono read primes one partial I2S slot word (62.5 us at 16 kHz), then returns the complete requested frame. This avoids the repeated ADC startup transient measured after full codec shutdown. `bsp_audio_suspend()` remains the full-close API for teardown and format changes. Failed transitions preserve the lock while any channel is live and allow retry. Retained codec analog power and the amplifier, whose enable is not wired to the MCU, contribute residual current; net savings and recovery after battery light sleep require measurement.

The UI worker uses event notifications and exact deadlines rather than a 200 ms poll. Identical host state/message updates do not reset the screen timeout. Screen-off pauses LVGL refresh, animation, and the 5 ms port tick; wake restores elapsed LVGL time. The timer handler stays enabled while its timers are paused, avoiding the LVGL 9.5 disabled-handler 1 ms loop. The port task can still wake at its 500 ms maximum interval. The LCD controller is not powered down.

Power management permits 40–160 MHz dynamic scaling and automatic light sleep with tickless idle. BLE modem sleep uses the main crystal, with the main-crystal light-sleep option enabled; an external 32 kHz crystal is not assumed. The backlight uses the XTAL clock and holds a no-light-sleep lock while lit, preventing PWM flicker. ADC keys remain sampled every 20 ms with one debounce tick; button timing and quick taps require board validation. BLE connection parameters are unchanged.

USB Serial/JTAG standby (`main/usb_standby.c`) polls the bus every two seconds and, while a host keeps it active, holds both a no-light-sleep lock and an 80 MHz APB-frequency lock (`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION=y` remains as the fast in-driver path). The APB floor keeps BBPLL powered for the USB PHY when dynamic scaling reaches the 40 MHz floor, and a five-second release grace retains the locks over brief SOF gaps. The cause of the earlier isolated disconnect is unconfirmed. A bus that stays silent or unplugged for five seconds permits battery light sleep again. Unplug USB to exercise battery standby; USB logging is not a valid light-sleep current measurement. Host suspend/replug behavior requires separate hardware acceptance.

Measure battery-side current with USB/debugging disconnected, for disconnected/off, connected/off, lit idle, and recording states. Verify first-word audio, boot interruption, at least 20 idle/record cycles, all three keys and long press, disconnect/reconnect, unchanged brightness deadlines, and no persistent PM locks after idle. A build or host test does not establish measured current savings.

## 9. CW2017 fuel gauge

Initialization uses the stock 520 mAh cell profile published in [FoloToy PR #38](https://github.com/FoloToy/ai-passport/pull/38), pinned to [commit `7eeb76db`](https://github.com/FoloToy/ai-passport/blob/7eeb76dbdf4038cda70817a1852b7737cdad36bc/components/bsp/src/bsp_battery.c). It reads VERSION and compares both UPDATE_FLAG and all 80 profile bytes. A missing flag or mismatch triggers the upstream sleep sequence, byte-by-byte upload, full readback verification, setting UPDATE_FLAG while preserving the alert threshold, and activation. A matching profile is not rewritten. Normal mode is checked before polling for the first valid SOC at 100 ms intervals, up to 50 attempts (plus I2C transaction time). Failure releases the device handle for a later retry.

Initialize serially before UI/audio workers start. Initialization and reads may block and must stay outside button callbacks and the LVGL task. The profile is specific to the standard-production 520 mAh cell; a replacement cell needs its matching vendor-generated profile and renewed charge/discharge validation. This updates only the CW2017 profile registers, not ESP32 NVS, identity, partition layout, or Recovery.

- SOC uses registers `0x04–0x05`; values above 100 are treated as not ready and return `-1`.
- Voltage uses the 14-bit value at `0x02–0x03`, converted as `raw × 312.5 µV`, and returned in mV. Reads outside the datasheet measurement range of 2500–4900 mV return `-1`.
- Transactions use a 100 ms timeout at 100 kHz.
- A missing device returns `ESP_ERR_NOT_FOUND`; the status UI continues with `--%`. A gauge absent at initialization is detected again on the next boot.

The status worker reads SOC immediately and then every 30 seconds, outside the LVGL lock. While lit, it updates the top-right battery icon and percentage under the lock. While off, it caches samples without updating LVGL; the latest sample appears on wake. The battery stays visible whenever the screen is lit and is independent of the VOICE/SEND/UNDO overlay. Startup shows all three hints and leader lines for three seconds, then fades them out even if the host becomes active. UP selects VOICE, DOWN selects SEND, and OK selects UNDO: only the selected label, its line, and its single-row background appear. Single hints occupy the original middle SEND row; their lines start there and retain the corresponding physical-key endpoint. The all-hints layout retains its original three rows and line paths. A different key replaces the selection immediately. Hints requested by a key remain visible during recording/processing and fade out after the last key has been idle for three seconds. The independent hardware power button has no application press callback, so it cannot reveal hints while running. A host state change cannot reveal hidden hints. Background battery updates neither reveal the hints nor reset idle timers or wake the backlight; screen dimming and sleep remain unchanged. Readings at 0–20% are red; the icon and text at 21–100% use subdued gray (`#646C78`). Invalid SOC clears the previous percentage and displays valid measured voltage (for example `3.94V`) with a neutral battery outline; only when both readings are unavailable does it show `--%`. Later valid SOC restores the percentage. Voltage is not converted to estimated SOC. The BSP exposes no charging-state API, so the UI does not indicate charging.

An I2C response and plausible voltage do not guarantee valid SOC: an empty profile can leave SOC out of range until the matching cell profile is installed. Immediately after installation, readings may still settle or temporarily use the voltage fallback. Validate full charge/discharge accuracy separately from initialization and UI behavior.

## 10. Flash, console, and memory

The current product and firmware baseline uses 8 MB Flash. `sdkconfig.defaults` fixes the image to 8 MB and disables automatic flash-size header rewriting. `partitions.csv` defines 24 KB NVS, 4 KB PHY data, one 3 MB factory application, protected `cardid` at `0x356000`, and permanent Recovery at `0x700000`. This is not an ESP-IDF dual-slot OTA layout: the factory-installed Recovery performs BLE installation and must remain at its fixed address. The bootloader enters it when UP/GPIO0 is held for five seconds. A detected non-8-MB device does not match this baseline; identify the board and flash part before changing the project default.

Do not erase a provisioned device or move/overlap the protected partitions.
Community firmware contains neither device identity nor a replacement Recovery
payload. See the [BLE compatibility contract](../development/engineering/ble-recovery-compatibility.md).

The console is USB Serial/JTAG. Do not switch to the UART0 default output without resolving its GPIO21 conflict with the backlight.

Review at least the 24 KB LVGL pool, 9.6 KB LCD DMA buffer, I2S DMA, NimBLE host/controller, the 320-sample audio worker buffers, task stacks, total free heap, and largest contiguous block when adding assets, networking, audio buffers, or double buffering.

## 11. Adding features

For reusable hardware capability, add `bsp_<feature>.h` and its implementation, keep constants in `bsp_pins.h`, update component CMake/dependencies, return `esp_err_t`, log actionable pin/address context, and document threading, blocking, ownership, initialization, and failure behavior.

For a Vokie application feature, extend the relevant module under `main/` and keep its public behavior documented. Preserve the BLE V1 wire format, host lifecycle, button contract, status states, and backlight policy unless the product requirement explicitly changes them. Keep blocking audio and BLE work in worker/task contexts and guard LVGL updates with the existing lock.

## 12. Development environment

Follow the canonical [environment bootstrap](../development/engineering/environment-setup.md)
for clean-machine installation, OS-specific prerequisites, and international or
mainland China download routes. Use ESP-IDF 5.5.3 outside the repository,
activate its `export.sh` in every terminal, and confirm the exact version.
Prefer `./tools/validate.sh --firmware` to build the verified merged image and
flash that image at `0x0`; use direct `idf.py build/flash` only for incremental
development.

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version
idf.py set-target esp32c3
idf.py reconfigure
idf.py build
```

The Component Manager resolves dependencies from `components/bsp/idf_component.yml`. Do not edit `managed_components/`. `dependencies.lock` is tracked and must remain reproducible under ESP-IDF 5.5.3. Generated `sdkconfig` does not automatically absorb every changed default; preserve intentional settings and use `idf.py set-target esp32c3` when configuration must be regenerated. Use `idf.py fullclean` only to remove stale build output.

For an intentional incremental flash, use the native USB Serial/JTAG port,
commonly `/dev/ttyACM0` on Linux:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

The actual port may differ. Check the cable, enumeration, permissions, power, and download mode before changing USB GPIOs or console configuration. Avoid running `idf.py` permanently as root.

## 13. Build and device validation

Run `./tools/validate.sh` for the complete automated gate. A successful build is the minimum automated result, not physical-device acceptance.

General board acceptance for this firmware:

- Stable USB Serial/JTAG logs without reboot loops, assertions, watchdogs, or persistent errors.
- I2C scan sees ES8311 at `0x18` and, when fitted, CW2017 at `0x63`.
- A host sees `Vokie Passport`, connects, subscribes to Control and Audio, and receives `hello`.
- `UP` starts/stops capture, `DOWN` sends Enter, and `OK` deletes, cancels, or clears according to the documented state.
- Non-zero microphone audio is captured at 16 kHz and arrives as reassembled IMA ADPCM frames.
- Backlight returns to the active level on a key action and follows the dim/off timeout policy.
- Repeated connect, record, submit, cancel, and disconnect cycles do not leak heap, tasks, timers, or objects.

| Change | Required physical observations |
| --- | --- |
| Pin/I2C | scan, all shared devices, boot straps, USB logs |
| LCD | color blocks, orientation, clipping, inversion, byte order, backlight levels |
| ADC/buttons | released and pressed mV, UP/DOWN/OK click/long events, margin across battery levels |
| Codec/I2S | non-zero 16 kHz capture, correct frame timing, format setup, disconnect/stop behavior |
| Battery | plausible SOC/voltage, 0/20/21/100%, voltage fallback and unknown display, read-failure recovery, missing-device startup, no overlap with key hints, unchanged backlight timeout, stable concurrent recording |
| Bluetooth LE | `Vokie Passport` advertising, connection, subscriptions, hello, notifications, reconnect |
| DMA/memory/UI | build memory report, runtime minimum heap/largest block, stable concurrent audio/display |

## 14. Troubleshooting

| Symptom | Check first |
| --- | --- |
| Backlight but no image | CS/DC/MOSI/SCLK, vendor sequence, software reset, display-on, SPI mode |
| Wrong colors | byte swap, RGB/BGR, inversion; change one variable at a time |
| Rotation change has no effect | LVGL rotation overriding lower-level mirror |
| Backlight or console failure | GPIO21 conflict with UART0 default TX |
| Button confusion | external 10 kΩ pull-up, measured voltage, thresholds, attenuation |
| `adc1 is already in use` | accidental second ADC1 oneshot unit |
| Both I2C devices disappear | accidental second I2C0 bus |
| Only ES8311 missing | address API shift and codec power |
| Audio speed/pitch wrong | close/open on format change, sample rate/MCLK, no manual clock writes |
| Recording is zero | `no_dac_ref`, DIN GPIO4, microphone path, gain |
| Recording allocation fails | no PSRAM; keep capture streaming and inspect largest block |
| Battery read fails | `0x63` response, invalid SOC, profile/startup delay |
| BLE host cannot connect | advertising fields, service UUID, MTU, single-connection state |
| BLE audio is missing | both notifications subscribed, host-ready message, MTU at least 185, audio worker state |
| Backlight does not wake | key-event touch timestamp, LVGL status task, GPIO21/LEDC ownership |
| I2S allocation fails after UI growth | competition among LCD/LVGL buffers and I2S DMA |
| Chinese text appears as boxes | Montserrat 14/20 has no CJK glyphs; compile and select a CJK subset, configure fallback for mixed text, and verify glyph coverage on the device |

## 15. Pre-delivery checklist

- [ ] No duplicated pins, addresses, dimensions, or board parameters.
- [ ] No unsupported capability presented as confirmed fact.
- [ ] No second I2C0 bus or ADC1 unit.
- [ ] Non-LVGL contexts lock LVGL access.
- [ ] Blocking hardware work stays out of button callbacks and the LVGL task.
- [ ] Page exit prevents background access to deleted objects.
- [ ] Audio format changes retain close/open and required codec settings.
- [ ] Memory review includes LVGL, LCD DMA, I2S DMA, task stacks, and largest block.
- [ ] Automated validation passed or the actual failure is reported.
- [ ] Build results and observed device results are reported separately.
- [ ] The diff contains only task-scoped changes and preserves user work.
