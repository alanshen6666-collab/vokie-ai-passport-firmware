<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# Vokie AI Passport 固件

这是将 FoloToy AI Passport 连接到 Vokie、作为 Bluetooth Low Energy 语音输入设备使用的开源固件。

固件运行在 ESP32-C3 上，并把 Vokie 作为必需的桌面端宿主。设备负责采集麦克风音频、把 20 ms 音频帧编码为 IMA ADPCM，并通过 Bluetooth Low Energy 发送给 Vokie，由 Vokie 完成语音识别、AI 润色和文字输入。

> **正常使用本设备必须安装 Vokie。** 请先前往 **[Vokie.com](https://vokie.com/download.html)** 下载并安装最新版 Vokie 桌面应用。没有 Vokie 时，设备可以启动，但不能独立完成语音识别、AI 润色或文字输入。

> 本项目是 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的独立衍生版本，不是 FoloToy 官方发布。上游源码及本项目衍生代码按 MIT License 分发；产品名称和品牌素材适用[许可与归属](#许可与归属)中的独立条款。

## 功能

- 使用顶部 `UP` 键单击切换开始/结束语音采集。
- 通过 ES8311 采集 16 kHz、16-bit、单声道音频。
- 将每个 20 ms 音频帧独立编码为 IMA ADPCM，并通过 BLE Notification 传输。
- 由主机控制显示 `READY`、`THINKING`、`SENT` 和错误状态。
- 三个实体按键支持发送、删除、清空和取消操作。
- 三级背光策略：活动时 65%，3 秒后降至 18%，20 秒后关闭；处理状态使用 38%。
- 保留上游的设备身份保护分区和永久 Recovery 分区布局。
- 随固件提供匹配的独立 Vokie Plugin 源码，支持自定义开发和基于 Plugin 的旧版 Vokie。

## 依赖

### 硬件与构建

- FoloToy AI Passport：ESP32-C3、8 MB Flash、ST7789P3 屏幕、ES8311 音频 Codec。
- ESP-IDF **5.5.3** 及 ESP32-C3 工具链。
- [`dependencies.lock`](dependencies.lock) 锁定的 Managed Component 版本。

### 必需安装 Vokie 桌面应用

Vokie 是当前受支持语音输入链路的强运行时依赖。使用设备前必须：

1. 前往 **[Vokie.com](https://vokie.com/download.html)** 下载并安装 Vokie。
2. 在当前 Apple Silicon Vokie 版本中，打开外部设备控制中心，通过内置连接使用 AI Passport。
3. 使用兼容的旧版 Vokie 或开发自定义集成时，在设置 -> Plugin 中导入仓库的 [`vokie-plugin/`](vokie-plugin/) 目录，再启用并配置它。

两种集成都通过 CoreBluetooth 连接设备、重组和解码音频、创建 Vokie 录音会话、回传状态，并把设备按键映射为编辑命令。独立 Plugin 以可修改源码形式提供，便于实现自定义行为。使用它连接已被内置集成选中的设备前，请先忘记内置连接，避免两个客户端争用同一个 BLE 外设。虽然固件编译不依赖 Vokie，但设备自身不提供语音识别、AI 润色和文字输入能力。

公开的 [AI Passport BLE V1 协议](docs/ai-passport-ble-protocol.zh_CN.md)用于集成与开发；第三方宿主不是当前面向普通用户支持的使用路径。

## 按键

| 按键 | 空闲时 | 采集或结果处理期间 |
| --- | --- | --- |
| `UP` 单击 | 开始语音采集 | 停止采集并提交 |
| `DOWN` 单击 | 发送回车 | 发送回车 |
| `OK` 单击 | 删除一个字符 | 取消当前请求 |
| `OK` 长按 | 清空输入 | 取消当前请求 |

实体键操作后会短暂显示按键提示。按任意实体键会立即唤醒背光。

## 构建与烧录

克隆仓库并激活严格匹配的 ESP-IDF 5.5.3，然后运行统一验证入口：

```sh
git clone https://github.com/alanshen6666-collab/vokie-ai-passport-firmware.git
cd vokie-ai-passport-firmware
source <ESP-IDF-v5.5.3-路径>/export.sh
idf.py --version
./tools/validate.sh --firmware
```

验证后的合并镜像位于：

```text
build/FoloToy-AI-Passport-full.bin
```

对于已经写入设备身份的设备，优先使用受支持的小程序安装方式或分段执行 `idf.py flash`，避免覆盖受保护的 `cardid` 和 Recovery 分区。把合并镜像写入 `0x0` 前，请先阅读[构建指南](docs/development/engineering/build-and-test.zh_CN.md)与 [Recovery 兼容契约](docs/development/engineering/ble-recovery-compatibility.zh_CN.md)。

连接开发板进行增量开发时可执行：

```sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

构建成功不等于 BLE、音频和实体按键已经通过真机验收。

## 协议与安全

完整的 Service UUID、消息、分片、音频格式、生命周期和主机要求见 [`docs/ai-passport-ble-protocol.zh_CN.md`](docs/ai-passport-ble-protocol.zh_CN.md)。

协议 V1 可以稳定选择设备，但**不提供** BLE 配对、对端身份认证或应用层加密。在附近设备或主机可能冒充对端、且音频内容敏感的环境中，不应直接使用该协议。

## 项目结构

```text
main/                    Vokie BLE 外设、音频传输与状态界面
vokie-plugin/            可导入、可修改的 Vokie 桌面 Plugin
components/bsp/          FoloToy AI Passport 板级支持包
docs/                    协议、构建、硬件与协作文档
tests/                   可脱离硬件运行的验证测试
tools/                   本地与 CI 共用的验证脚本
sdkconfig.defaults       可复现的 ESP32-C3 默认配置
partitions.csv           应用与设备身份/Recovery 保护分区
dependencies.lock        ESP-IDF Managed Component 依赖锁文件
```

## 许可与归属

- 软件按 [MIT License](LICENSE) 分发，并保留上游项目的 `Copyright (c) 2026 FoloToy`。
- 本仓库保留完整上游 Git 历史，并在前文注明上游来源。
- Vokie 名称和图形标识（包括 [`main/vokie_symbol_asset.h`](main/vokie_symbol_asset.h)）**不属于** MIT 授权范围，适用 [`LICENSES/Vokie-Brand-Asset.txt`](LICENSES/Vokie-Brand-Asset.txt) 的独立条款。
- 第三方组件保留各自许可，详见 [`docs/THIRD_PARTY_NOTICES.zh_CN.md`](docs/THIRD_PARTY_NOTICES.zh_CN.md)。
- “FoloToy AI Passport”仅用于说明硬件兼容性，不表示本项目获得 FoloToy 官方背书。
