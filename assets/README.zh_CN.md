<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## Vokie 标题字体

- 来源：[Barlow Condensed Bold](https://github.com/google/fonts/blob/60824dce48f7dd28fe7d65559f2da1f6e04e585b/ofl/barlowcondensed/BarlowCondensed-Bold.ttf)，固定到 Google Fonts 提交 `60824dce48f7dd28fe7d65559f2da1f6e04e585b`。
- 作者与许可：Copyright 2017 The Barlow Project Authors；SIL Open Font License 1.1，完整许可保留在 [Barlow-OFL.txt](../LICENSES/Barlow-OFL.txt)。
- 文件：[源 TTF](fonts/BarlowCondensed-Bold.ttf)、[生成的 LVGL 字体](fonts/vokie_title_barlow_condensed_bold_22.c) 与[声明](fonts/vokie_title_font.h)。
- 仅包含 `Vokie Power` 所用字符：空格、`P`、`V`、`e`、`i`、`k`、`o`、`r`、`w`。标题若需要其他字符，须重新生成子集。
- 集成：`main/CMakeLists.txt` 编译生成的 C 文件。TTF 仅用作源素材，不嵌入固件；字形采用 4 位抗锯齿、未压缩常量表，不引入运行时 TTF 解析器或字形解压缓冲区。

在仓库根目录使用 `lv_font_conv` 1.5.3 重新生成：

```bash
lv_font_conv --size 22 --bpp 4 --format lvgl \
  --font assets/fonts/BarlowCondensed-Bold.ttf --symbols 'Vokie Power' \
  --no-compress --no-prefilter --lv-include lvgl.h \
  --lv-font-name vokie_title_barlow_condensed_bold_22 \
  -o assets/fonts/vokie_title_barlow_condensed_bold_22.c
```
