<p align="right">
  <a href="standby-power-decision.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Standby power decision

## Decision

Replace the backlight-only standby policy with idle I2S pause, event-driven UI work, and ESP-IDF automatic light sleep with BLE modem sleep. There is no previous numbered ADR; this supersedes only the backlight-only power behavior in the hardware guide. BLE V1, brightness levels, idle deadlines, the three ADC keys, and the protected Recovery contract remain valid.

## Alternatives and risks

Keep 20 ms ADC scanning and the main crystal during light sleep. After 20 seconds of inactivity, request a 90–120 ms connection interval. The central negotiates the final value. Restore the 15 ms audio link before starting a capture from the slow link. On battery, 15 minutes of inactivity triggers deep sleep, which ends the BLE connection. GPIO0 wakes the device on a function-key press after the ADC releases the pad. Wake boots the application and advertises again.

Fully powering down and reopening ES8311 repeats a clipped startup transient at recording start on the tested board. Retain codec bias and filter state during light standby while pausing I2S/DMA. Prime one partial I2S slot word at the first 16-bit mono read after restart (62.5 µs at 16 kHz). During deep sleep, suspend the codec, display and fuel gauge. Current, first audio after GPIO wake and reconnection latency require device measurements.

## Acceptance

The [contract](standby-power-contract.md) and [plan](standby-power-plan.md) define the gates. Automated checks must pass; hardware acceptance remains a separate, explicitly reported result.
