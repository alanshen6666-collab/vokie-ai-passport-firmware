<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Vokie Plugin

This standalone Plugin connects a FoloToy AI Passport to Vokie on Apple
Silicon macOS (`darwin/arm64`) over Bluetooth Low Energy. It is shipped with
the firmware source so developers can inspect, modify, and import the desktop
integration together with compatible firmware.

Current Vokie releases include a built-in AI Passport connection and do not
require this Plugin for normal use. Import this package when using a compatible
older Vokie release or when developing custom AI Passport behavior. Before a
custom Plugin claims the device, forget its built-in connection in Vokie's
external-device control center so two CoreBluetooth clients do not compete for
the same peripheral.

## Install and build

Import the [`vokie-plugin/`](./) directory from Vokie Settings -> Plugins. The
checked-in helper is an arm64 development binary. Rebuild it after changing the
Swift source:

```sh
node vokie-plugin/scripts/build-helper.mjs
```

The build requires Xcode Command Line Tools and writes
`vokie-plugin/assets/bin/ai-passport-helper`. The script does not explicitly
sign the helper. On Apple Silicon, `swiftc` may emit the linker signature needed
to execute an arm64 binary; the script does not replace it with an application
or distribution identity. The Bluetooth permission prompt is owned by Vokie.

For local Worker tests, set `VOKIE_AI_PASSPORT_HELPER_PATH` to a test helper.
Vokie supplies `VOKIE_PLUGIN_WS_URL`, `VOKIE_PLUGIN_ID`, and
`VOKIE_PLUGIN_TOKEN` when it launches the Worker.

## Behavior

Click the top `UP` button once to start recording and click it again to submit.
The Worker decodes independent 20 ms, 16 kHz mono IMA ADPCM frames, opens the
standard Vokie `ptt` session, and maps the other physical buttons to Enter,
delete, clear, and cancel commands. The full BLE wire contract is documented in
the repository's [AI Passport BLE V1 protocol](../docs/ai-passport-ble-protocol.md).

The Plugin settings page discovers nearby Passports but does not connect to the
first result automatically. Selecting a device persists its CoreBluetooth UUID
in Vokie's per-Plugin configuration:

```json
{ "preferredDeviceId": "123E4567-E89B-42D3-A456-426614174000" }
```

Forgetting the device clears that preference and releases the current BLE
connection. UUID pinning provides stable selection, not peer authentication or
BLE pairing. Protocol V1 should not be used for sensitive audio where a nearby
device or host could impersonate a peer.

## Package layout

```text
vokie.plugin.json          Stable Plugin identity and capabilities
worker/                    Vokie WebSocket lifecycle, BLE protocol, ADPCM decode
helper/                    CoreBluetooth helper source and Info.plist
scripts/build-helper.mjs   Reproducible arm64 helper build
assets/bin/                Executable helper used by the Worker
ui/                        Sandboxed settings and device-selection page
```

The Worker communicates with Vokie only through the authenticated local Plugin
WebSocket and never writes captured audio to disk.
