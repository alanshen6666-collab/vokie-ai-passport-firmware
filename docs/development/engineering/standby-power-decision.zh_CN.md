<p align="right">
  <strong>简体中文</strong> · <a href="standby-power-decision.md">English</a>
</p>

# 待机功耗决策

## 决策

将仅关闭背光的待机策略替换为闲置音频关闭、事件驱动 UI 和 ESP-IDF 自动轻睡眠及 BLE Modem-sleep。没有既有编号 ADR；本决策仅取代硬件指南中的背光待机行为。BLE V1、亮度、空闲超时、三键及 Recovery 契约保持有效。

## 替代方案与风险

深睡眠会断开 BLE 并改变唤醒行为，本次不采用。ADC 电阻分压按键的 GPIO 唤醒需要实测，本次保留 20 ms 扫描，去抖改为一个扫描周期，尽量接近原来的 10 ms 确认时间。工作时保留 160 MHz，闲置时使用 40 MHz；BLE 休眠使用主晶振，不假定存在外部 32 kHz 晶振。

音频重开可能影响首字或产生爆音。音频打开及背光亮起期间持有禁止轻睡眠锁。本次不改变 LCD 控制器供电或 BLE 连接参数。节电幅度和唤醒延迟须真机测量。

## 验收

[契约](standby-power-contract.zh_CN.md)和[计划](standby-power-plan.zh_CN.md)定义验收门。自动检查必须通过，硬件验收独立报告。
