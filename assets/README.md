<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Vokie title font

- Source: [Barlow Condensed Bold](https://github.com/google/fonts/blob/60824dce48f7dd28fe7d65559f2da1f6e04e585b/ofl/barlowcondensed/BarlowCondensed-Bold.ttf), pinned to Google Fonts commit `60824dce48f7dd28fe7d65559f2da1f6e04e585b`.
- Author/license: Copyright 2017 The Barlow Project Authors; SIL Open Font License 1.1, retained in [Barlow-OFL.txt](../LICENSES/Barlow-OFL.txt).
- Files: [source TTF](fonts/BarlowCondensed-Bold.ttf), [generated LVGL font](fonts/vokie_title_barlow_condensed_bold_22.c), and [declaration](fonts/vokie_title_font.h).
- Only the characters in `Vokie Power` are included: space, `P`, `V`, `e`, `i`, `k`, `o`, `r`, and `w`. A title change that needs other characters must regenerate this subset.
- Integration: `main/CMakeLists.txt` compiles the generated C file. The TTF is a source asset and is not embedded in firmware. Glyphs use 4-bit antialiasing and constant uncompressed tables; there is no runtime TTF parser or glyph-decompression buffer.

Regenerate from the repository root with `lv_font_conv` version 1.5.3:

```bash
lv_font_conv --size 22 --bpp 4 --format lvgl \
  --font assets/fonts/BarlowCondensed-Bold.ttf --symbols 'Vokie Power' \
  --no-compress --no-prefilter --lv-include lvgl.h \
  --lv-font-name vokie_title_barlow_condensed_bold_22 \
  -o assets/fonts/vokie_title_barlow_condensed_bold_22.c
```
