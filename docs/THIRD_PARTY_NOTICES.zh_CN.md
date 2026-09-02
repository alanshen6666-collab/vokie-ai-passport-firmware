<p align="right">
  <strong>简体中文</strong> · <a href="THIRD_PARTY_NOTICES.md">English</a>
</p>

# 第三方声明

本项目基于按 MIT License 分发的 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)，其版权声明为 `Copyright (c) 2026 FoloToy`。完整上游声明保留在本仓库的 [`LICENSE`](../LICENSE) 中。

ESP-IDF Managed Component 的版本锁定在 [`dependencies.lock`](../dependencies.lock)。ESP-IDF 下载的各组件源码包中包含权威许可证和版权文件；再次分发这些组件时必须保留相应文件。

| 组件 | 锁定版本 | 组件发行包中的主要许可证 |
| --- | ---: | --- |
| Espressif ESP-IDF | 5.5.3 | Apache License 2.0；ESP-IDF 另行记录的第三方例外除外 |
| `espressif/button` | 4.2.0 | Apache License 2.0 |
| `espressif/cmake_utilities` | 1.1.1 | Apache License 2.0 |
| `espressif/esp_codec_dev` | 1.6.2 | Apache License 2.0，并包含设备相关声明 |
| `espressif/esp_lvgl_port` | 2.9.0 | Apache License 2.0 |
| `lvgl/lvgl` | 9.5.0 | MIT License，并包含额外的内置第三方声明 |

本表仅为阅读便利，不能替代各依赖随附的许可证文件。再次分发依赖源码或编译后的固件时，应根据实际锁定版本附带适用的许可证、版权声明、NOTICE 和组件内第三方文件。

Vokie 名称与图形标识不是第三方软件依赖，也不在 MIT 授权范围内；其独立再分发条款见 [`LICENSES/Vokie-Brand-Asset.txt`](../LICENSES/Vokie-Brand-Asset.txt)。
