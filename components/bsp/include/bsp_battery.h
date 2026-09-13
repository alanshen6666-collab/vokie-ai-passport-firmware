// components/bsp/include/bsp_battery.h
// CellWise CW2017 电量计:I2C 0x63,与 ES8311 共用总线。
// 使用 FoloToy 官方 PR #38 提供的原装 520 mAh 电池 profile。
// 更换电芯必须使用匹配的厂商 profile 并重新验证充放电曲线。
#pragma once

#include "esp_err.h"

// 初始化。内部会调 bsp_i2c_init()(幂等)。启动工作任务之前串行调用，
// 对比 UPDATE_FLAG 与完整 profile，不匹配时休眠、写入、读回校验并激活。
// 以 100 ms 间隔等待有效 SOC，最多 50 次，另加 I2C 事务耗时。
// 不可从按键回调或 LVGL 任务调用。ESP_OK 表示 profile 和首次 SOC 就绪。
// 写入/模式校验/等待失败会释放设备句柄，允许之后重试。
// 芯片不应答时返回 ESP_ERR_NOT_FOUND —— 上层可据此在 UI 上标记该项不可用。
esp_err_t bsp_battery_init(void);

// 剩余电量百分比 0..100；读取失败、未就绪或无有效 profile 时返回 -1。
int bsp_battery_soc(void);

// 电池电压 mV；读取失败或超出 CW2017 的 2500..4900 mV 测量范围时返回 -1。
// 即使 SOC 不可用仍可读取；两种读取都可能阻塞 100 ms，且不得与 init 并发。
int bsp_battery_mv(void);
