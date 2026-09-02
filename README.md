<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Vokie AI Passport Firmware

Open-source firmware that connects the FoloToy AI Passport to Vokie as a
Bluetooth Low Energy voice-input device.

The firmware runs on the ESP32-C3 and uses Vokie as its required desktop host.
It captures microphone audio, encodes 20 ms frames as IMA ADPCM, and sends them
to Vokie over Bluetooth Low Energy for speech recognition, AI refinement, and
text insertion.

> **Vokie is required for normal use.** Install the latest Vokie desktop app
> from **[Vokie.com](https://vokie.com/download.html)** before using the device.
> The device can boot without Vokie, but it cannot complete voice recognition,
> AI refinement, or text insertion on its own.

> This is an independent derivative of
> [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), not an official
> FoloToy release. The upstream source and this derivative code are distributed
> under the MIT License. Product names and brand assets are subject to the
> separate terms described under [License and attribution](#license-and-attribution).

## Features

- Click-to-toggle voice capture from the top `UP` button.
- 16 kHz, 16-bit, mono microphone capture through the ES8311 codec.
- Independent 20 ms IMA ADPCM frames transported over BLE notifications.
- Host-controlled `READY`, `THINKING`, `SENT`, and error status display.
- Physical controls for send, delete, clear, and cancellation.
- Three-stage backlight policy: 65% active, 18% after 3 seconds, off after
  20 seconds; processing uses 38%.
- Preserves the upstream protected device-identity and permanent-Recovery
  partition layout.

## Requirements

### Hardware and build

- FoloToy AI Passport with ESP32-C3, 8 MB Flash, ST7789P3 display, and ES8311
  audio codec.
- ESP-IDF **5.5.3** with the ESP32-C3 toolchain.
- Managed Component versions pinned by [`dependencies.lock`](dependencies.lock).

### Required Vokie desktop app

Vokie is a required runtime dependency for the supported voice-input workflow.
Before using the device:

1. Download and install Vokie from **[Vokie.com](https://vokie.com/download.html)**.
2. Use a Vokie build that includes the **AI Passport Plugin**. The current
   first-party plugin supports macOS on Apple Silicon (`darwin/arm64`).
3. Start Vokie and enable the AI Passport Plugin before connecting the device.

The plugin connects over CoreBluetooth, reassembles and decodes audio, opens
Vokie recording sessions, forwards status, and maps device buttons to editor
commands. Although the firmware has no compile-time dependency on Vokie, the
device alone does not provide speech recognition, AI refinement, or text
insertion.

The public [AI Passport BLE V1 protocol](docs/ai-passport-ble-protocol.md) is
documented for integration and development, but third-party hosts are not the
currently supported end-user path.

## Controls

| Control | Idle | During capture or result processing |
| --- | --- | --- |
| `UP` click | Start voice capture | Stop capture and submit |
| `DOWN` click | Send Enter | Send Enter |
| `OK` click | Delete one character | Cancel the active request |
| `OK` long press | Clear input | Cancel the active request |

The button action rail appears briefly after a physical key action. Any key
wakes the backlight immediately.

## Build and flash

Clone the repository, activate exactly ESP-IDF 5.5.3, then run the shared
validation entry point:

```sh
git clone https://github.com/alanshen6666-collab/vokie-ai-passport-firmware.git
cd vokie-ai-passport-firmware
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version
./tools/validate.sh --firmware
```

The verified merged image is written to:

```text
build/FoloToy-AI-Passport-full.bin
```

For a provisioned device, prefer the supported mini-program installation path
or segmented `idf.py flash`; this avoids overwriting the protected `cardid` and
Recovery partitions. See the
[build guide](docs/development/engineering/build-and-test.md) and
[Recovery compatibility contract](docs/development/engineering/ble-recovery-compatibility.md)
before flashing a merged image at offset `0x0`.

For incremental development on a connected board:

```sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

A successful build is not a substitute for on-device BLE, audio, and button
validation.

## Protocol and security

The complete service UUIDs, messages, framing, audio format, lifecycle, and host
requirements are documented in
[`docs/ai-passport-ble-protocol.md`](docs/ai-passport-ble-protocol.md).

Protocol V1 provides stable device selection, but it does **not** provide BLE
pairing, peer authentication, or application-layer encryption. Do not use it for
sensitive audio in environments where a nearby device or host could impersonate
a peer.

## Project layout

```text
main/                    Vokie BLE peripheral, audio transport, and status UI
components/bsp/          FoloToy AI Passport board-support package
docs/                    Protocol, build, hardware, and contribution documents
tests/                   Host-runnable validation tests
tools/                   Local and CI validation scripts
sdkconfig.defaults       Reproducible ESP32-C3 defaults
partitions.csv           App plus protected identity/Recovery layout
dependencies.lock        Pinned ESP-IDF Managed Components
```

## License and attribution

- The software is distributed under the [MIT License](LICENSE), retaining
  `Copyright (c) 2026 FoloToy` from the upstream project.
- This repository preserves upstream Git history and identifies the upstream
  source above.
- The Vokie name and symbol, including
  [`main/vokie_symbol_asset.h`](main/vokie_symbol_asset.h), are **not** licensed
  under MIT. They are governed by
  [`LICENSES/Vokie-Brand-Asset.txt`](LICENSES/Vokie-Brand-Asset.txt).
- Third-party components retain their own terms; see
  [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md).
- Use of “FoloToy AI Passport” describes hardware compatibility and does not
  imply endorsement by FoloToy.
