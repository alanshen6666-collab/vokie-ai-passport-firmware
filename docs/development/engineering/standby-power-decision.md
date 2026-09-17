<p align="right">
  <a href="standby-power-decision.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Standby power decision

## Decision

Replace the backlight-only standby policy with idle audio shutdown, event-driven UI work, and ESP-IDF automatic light sleep with BLE modem sleep. There is no previous numbered ADR; this supersedes only the backlight-only power behavior in the hardware guide. BLE V1, brightness levels, idle deadlines, the three ADC keys, and the protected Recovery contract remain valid.

## Alternatives and risks

Deep sleep would disconnect BLE and change wake behavior; it is excluded. GPIO wake for the ADC ladder requires board measurements and is excluded. Keep periodic ADC scanning at 20 ms with debounce retuned to preserve the original 10 ms qualification period as closely as this scan allows (one sample period). Keep 160 MHz available for work and use 40 MHz when idle. Use the main crystal for BLE sleep; do not assume an external 32 kHz crystal.

Audio reopening can affect the first sample and introduce a pop. Hold a no-light-sleep lock while audio is open and while the backlight is lit. Keep LCD controller power and BLE connection parameters unchanged in this patch. Current savings and wake latency require device measurements.

## Acceptance

The [contract](standby-power-contract.md) and [plan](standby-power-plan.md) define the gates. Automated checks must pass; hardware acceptance remains a separate, explicitly reported result.
