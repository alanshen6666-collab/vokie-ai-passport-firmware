<p align="right">
  <strong>简体中文</strong> · <a href="fork-guide.md">English</a>
</p>

# Fork 工作流

本仓库以 `main` 作为公开的 Vokie AI Passport 固件基线。fork 专属的产品改动应放在 `feature/*` 分支，使 fork 可以持续同步 `main`，避免把本地产品改动混入公共基线。

## 目录职责

```text
README.md              fork 产品介绍与集成要求
docs/                  协议、硬件、工程与协作文档
components/bsp/        稳定的板级 API 与硬件实现
main/                  Vokie BLE 外设、音频传输与状态界面
assets/                可复用源素材
skills/                可复用 AI agent 技能
tests/                 可脱离硬件运行的测试
sdkconfig.defaults     可复现的 ESP32-C3 默认配置
```

## 推荐流程

1. Fork `alanshen6666-collab/vokie-ai-passport-firmware`。
2. 让 fork 的 `main` 与公共仓库 `main` 保持同步。
3. 从最新 `main` 创建短生命周期的 `feature/*` 或 `fix/*` 分支。
4. 使用 ESP-IDF 5.5.3 运行 `./tools/validate.sh`，通过后再提交 Pull Request。
5. 通过评审后的 Pull Request 合并，不直接在 `main` 开发。

本独立仓库不会自动与 FoloToy 同步。下游 fork 如需更新，应显式 fetch 本仓库，检查差异后，通过 fork 自己的 Pull Request 流程合并公共 `main`。

## 上游归属

本固件衍生自 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)，并保留其 MIT 许可证和 Git 历史。适用于硬件基线的通用改进可以另行提交给 FoloToy；Vokie 专属固件、协议、界面和品牌素材保留在本仓库。

## 文档与素材

默认 `.md` 路径使用英文，简体中文使用配对的 `.zh_CN.md` 文件，并在文件顶部提供双向语言链接。产品专属设计说明放在 `docs/`，可复用二进制或源素材放在 `assets/`。

Vokie 名称和图形标识不属于 MIT 授权范围。fork 只能按照 [`LICENSES/Vokie-Brand-Asset.txt`](../LICENSES/Vokie-Brand-Asset.txt) 原样再分发该图形；替换、提取或用于其他品牌需要另行取得许可。
