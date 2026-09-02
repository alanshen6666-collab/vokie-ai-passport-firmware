<p align="right">
  <strong>简体中文</strong> · <a href="ai-guide.md">English</a>
</p>

# AI Agent 开发指南（AI Agent Development Guide）

> 定位：面向 AI 编程助手（Claude Code / Codex / Cursor / Cline 等），人类开发者可忽略。
> 本文档集中说明“AI 如何在本仓库工作”。所有任务只强制先读 `AGENTS.zh_CN.md`；涉及代码开发时再读本文档，并按路由加载相关硬件或工程说明。

## 1. 开始开发前：建立上下文

开始开发前，按以下顺序建立上下文：

1. 阅读 `AGENTS.zh_CN.md`，根据其中的任务路由只加载当前修改所需文档；不要默认读取全部 README 或完整硬件指南。
2. 执行 `git status --short --branch`，保留用户已有改动。
3. 阅读需求会触及的 `components/bsp/include/*.h` 及其实现，不根据芯片或开发板的常见配置猜测本板行为。
4. 如果确实需要历史示例，先显式添加 FoloToy 上游 remote，再查看 `folotoy/demo/*`；不要假设本仓库的 `origin` 存在这些分支。
5. 将需求拆成输入、输出、状态、并发任务、持久化、内存预算和失败降级，再决定修改 `main` 还是扩展 `components/bsp`。
6. 迭代时运行最小相关测试，交付前运行 `./tools/validate.sh`；所有依赖屏幕、按键、音频、电池或时序的结论均保留真机验收项。

## 2. 事实来源优先级（Source-of-truth priority）

发生冲突时，使用以下优先级：

```text
产品规格 / 实机测量
    > components/bsp/include/bsp_pins.h
    > BSP 公开头文件与实现
    > docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md
    > README 与上游历史示例
```

任务所需板卡版本、接线、极性、寄存器或 GPIO 分配未在这些来源中定义时，直接询问用户，不能用其他 ESP32-C3 开发板的参数补全答案。

## 3. 应用与 BSP 的边界

```text
Natural-language requirement
  └─ main/                         Pages, state machines, animation, app tasks, assets
      └─ components/bsp/include/  Stable board-level APIs
          └─ components/bsp/src/  GPIO, buses, devices, and driver details
              └─ bsp_pins.h       Single source of truth for pins and hardware parameters
```

新增 Vokie 固件功能时，通常扩展 `main/vokie_ble.c`、`main/ui_status.c` 或新建具有明确接口的应用模块。必须同时记录 BLE 线协议兼容性、主机生命周期、按键语义和状态变化；新增源文件要同步加入 `main/CMakeLists.txt`。

只有多个应用都会使用的硬件能力才进入 `components/bsp`。BSP API 需要说明阻塞性、线程上下文、内存所有权、失败值和初始化顺序；引脚或 I2C 地址只能加入 `bsp_pins.h`。

## 4. 运行时不可破坏的规则（Runtime invariants）

- LVGL 不是线程安全的；非 LVGL 上下文操作 `lv_*` 对象必须持有 `bsp_lvgl_lock()`。
- 按键回调只派发轻量事件；录音、播放、存储和其他慢操作放到工作任务。
- 页面退出时先停止可能访问 UI 的任务或定时器，再删除 screen 并清空对象指针。
- 除非需求明确变更，否则保留 Vokie 按键契约：`UP` 开始/停止采集，`DOWN` 发送回车，`OK` 删除或取消，空闲时 `OK` 长按清空输入。
- 除非产品需求变更，否则保留 `ui_status` 的视觉状态和背光策略。状态更新必须运行在 LVGL 上下文中，不要把历史菜单或 demo 页面重新引入当前应用。
- 如果功能需要显示电量，使用 BSP 电池 API；读值不可用时优雅降级，不要在应用层复制板级常量。
- 新图片、字体、网络栈、音频缓存、LVGL buffer 或任务栈都要评估内部 RAM；总空闲堆足够不代表存在足够大的连续内存块。
- 可测试的状态机、协议、计时和布局计算应与 ESP-IDF/LVGL 分离，优先加入主机逻辑测试。

## 5. 素材放置（Material placement）

当开发者通过你提交可复用素材（图片、字库、音频或类似的工程素材）时，默认保存到仓库根目录 [`assets/`](../../assets/README.zh_CN.md)，以便开发及后续复用。将其放入对应的子目录（`assets/images/`、`assets/fonts/`、`assets/music/`），并在 [`assets/` README](../../assets/README.zh_CN.md) 中记录放置路径、命名规则、集成方式与来源/授权。二进制素材不得与 Markdown 文档混放。应用或经验归档记录（封面、手册、摘要）属于 `reference/<username>/`，不放入 `assets/`；除非开发者明确指定其它位置，否则不要偏离 `assets/`。

## 6. 验收与交付格式

`./tools/validate.sh` 是完整自动门禁，但不是硬件验收。agent 的最终交付应明确区分：

```text
Build: PASS / FAIL / NOT RUN
Host tests: PASS / FAIL / NOT RUN
Device tests: PASS / FAIL / NOT RUN
Unverified: 仍需板卡、仪器或用户确认的事项
```

上板验收矩阵按修改类型（引脚、LCD、ADC、codec、I2C、DMA 等）见 [AI 硬件开发指南 §构建与验证](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#构建与验证)，本文档不重复完整验收清单。真机结果要与"编译通过"分开记录。

## 7. 相关文档

- 构建与验证命令：[build-and-test.zh_CN.md](engineering/build-and-test.zh_CN.md)
- 代码约定：[coding-conventions.zh_CN.md](engineering/coding-conventions.zh_CN.md)
- 硬件指南与验收矩阵：[AI 硬件开发指南](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)
- 全部文档索引：[docs/README.zh_CN.md](../README.zh_CN.md)
