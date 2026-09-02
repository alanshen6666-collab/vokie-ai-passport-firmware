<p align="right">
  <a href="coding-conventions.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Coding Conventions

- Write C with four-space indentation and K&R braces, following neighboring files. Use `snake_case`, `BSP_*` public constants, `s_` file-local state, `bsp_` public BSP APIs, and `demo_<feature>_<action>` demo entry points. Prefer `static` for internal symbols.
- Keep UI text and default documentation in English. Explanatory source comments may use Chinese while retaining established English technical terms.
- The baseline enables only LVGL Montserrat 14 and 20, which do not contain CJK glyphs. Chinese UTF-8 text therefore renders as missing-glyph boxes; changing source-file encoding does not fix it. Before adding Chinese UI text, compile and select a CJK font that covers every displayed character, prefer a glyph subset over a full font, configure a suitable fallback for mixed-language text, budget Flash and internal RAM, and verify the result on the device.
- Put reusable hardware behavior in `components/bsp`; keep Vokie product behavior, BLE transport, and status UI in `main`.
- Document the BLE V1 wire contract, host lifecycle, button semantics, and status transitions whenever changing the Vokie application.
- Document non-trivial functions, state, ownership, blocking behavior, task context, initialization order, failure values, register choices, timing, synchronization, and hardware-specific constants. Explain why, not merely what.
- Add or update tests with code changes. If automation is not practical, record the test gap and exact manual validation path.
- If adding a cache, define expiration and cleanup unless durable retention is explicitly justified.
- The ESP32-C3 has no PSRAM. Review internal RAM and largest-contiguous-block impact before increasing LVGL buffers, audio allocations, network state, or task stacks.
- **Watch power consumption.** This is a wearable powered by a small battery; keep it efficient. The current Vokie firmware dims and turns off the LCD backlight on inactivity but does not provide a light/deep-sleep application mode. Do not describe backlight control as MCU sleep. See the hardware guidance in [`../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).
