<p align="right">
  <strong>简体中文</strong> · <a href="standby-power-decision.md">English</a>
</p>

# 待机功耗决策

## 决策

将仅关闭背光的待机策略替换为闲置 I2S 暂停、事件驱动 UI 和 ESP-IDF 自动轻睡眠及 BLE Modem-sleep。没有既有编号 ADR；本决策仅取代硬件指南中的背光待机行为。BLE V1、亮度、空闲超时、三键及 Recovery 契约保持有效。

## 替代方案与风险

浅睡保留 20 ms 的 ADC 扫描及主晶振。空闲 20 秒后请求 90–120 ms 的连接间隔，最终值由中心设备协商。低频待机后先恢复 15 ms 的音频链路，再开始录音。电池供电时空闲 15 分钟后进入深睡并断开 BLE，ADC 释放 GPIO0 后，按功能键可以唤醒。唤醒时重新启动应用并发出广播。

实测板卡在 ES8311 完全关闭再打开后，会在录音开头出现削波瞬态。浅睡待机时保留 codec 偏置及滤波器状态，暂停 I2S/DMA。恢复后的首次 16-bit 单声道读取先读走一个不完整 I2S 槽采样（16 kHz 下为 62.5 微秒）。深睡时暂停 codec、屏幕和电量计。实际电流、GPIO 唤醒后首段音频和重连耗时需要实测。

## 验收

[契约](standby-power-contract.zh_CN.md)和[计划](standby-power-plan.zh_CN.md)定义验收门。自动检查必须通过，硬件验收独立报告。
