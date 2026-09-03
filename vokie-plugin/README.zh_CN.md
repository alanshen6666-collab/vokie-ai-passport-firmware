<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# AI Passport Vokie Plugin

这个独立 Plugin 在 Apple Silicon macOS（`darwin/arm64`）上通过 Bluetooth Low
Energy 将 FoloToy AI Passport 连接到 Vokie。它与固件源码一起交付，开发者可以在同一
仓库中查看、修改并导入桌面端集成代码，配合兼容固件实现自定义功能。

当前版本的 Vokie 已内置 AI Passport 连接，普通使用无需安装此 Plugin。使用兼容的旧版
Vokie，或开发自定义 AI Passport 行为时，再导入此包。自定义 Plugin 连接设备前，应先在
Vokie 的外部设备控制中心忘记内置连接，避免两个 CoreBluetooth 客户端争用同一个外设。

## 安装与构建

在 Vokie 设置 -> Plugin 中导入 [`vokie-plugin/`](./) 目录。仓库内已包含用于开发的
arm64 helper；修改 Swift 源码后请重新构建：

```sh
node vokie-plugin/scripts/build-helper.mjs
```

构建需要 Xcode Command Line Tools，输出到
`vokie-plugin/assets/bin/ai-passport-helper`。脚本不会显式签名 helper。在 Apple
Silicon 上，`swiftc` 可能生成 arm64 可执行文件运行所需的 linker signature；脚本不会
将其替换为应用或分发签名身份。Bluetooth 权限提示由 Vokie 应用负责。

本地测试 Worker 时，可用 `VOKIE_AI_PASSPORT_HELPER_PATH` 指向测试 helper。Vokie
启动 Worker 时会提供 `VOKIE_PLUGIN_WS_URL`、`VOKIE_PLUGIN_ID` 和
`VOKIE_PLUGIN_TOKEN`。

## 行为

单击顶部 `UP` 键开始录音，再单击一次结束并提交。Worker 解码独立的 20 ms、16 kHz
单声道 IMA ADPCM 帧，创建标准 Vokie `ptt` 会话，并把其他实体按键映射为回车、删除、
清空和取消命令。完整 BLE 线协议见仓库中的
[AI Passport BLE V1 协议](../docs/ai-passport-ble-protocol.zh_CN.md)。

Plugin 设置页会发现附近的 Passport，但不会自动连接第一个结果。选择设备后，Vokie 会
在该 Plugin 的独立配置中保存 CoreBluetooth UUID：

```json
{ "preferredDeviceId": "123E4567-E89B-42D3-A456-426614174000" }
```

忘记设备会清除该选择并释放当前 BLE 连接。UUID 固定只提供稳定设备选择，不等同于对端
身份认证或 BLE 配对。在附近设备或主机可能冒充对端、且音频内容敏感的环境中，不应直接
使用协议 V1。

## 包结构

```text
vokie.plugin.json          固定的 Plugin 身份与能力声明
worker/                    Vokie WebSocket 生命周期、BLE 协议与 ADPCM 解码
helper/                    CoreBluetooth helper 源码与 Info.plist
scripts/build-helper.mjs   可复现的 arm64 helper 构建脚本
assets/bin/                Worker 使用的 helper 可执行文件
ui/                        沙箱化设置与设备选择页面
```

Worker 只通过带认证的本地 Plugin WebSocket 与 Vokie 通信，不会把录音音频写入磁盘。
