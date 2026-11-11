# ETS2 Reverse Posture Assistant v1.62.1

This release adds exact native compatibility with the installed Steam public
Euro Truck Simulator 2 1.62.2.1 Windows x64 executable. It retains the exact
1.62.1.0 profile and supported 1.61.x and 1.60.x profiles. Mod versions remain
in the game-aligned `v1.62.x` series.

## Changes

- Added a SHA-256-bound `BuildProfile` for ETS2 1.62.2.1.
- Added and verified exact RVAs and byte signatures for all four enabled native
  hooks and four active model-lifecycle helpers.
- Verified the vehicle render dispatch structure and trailer render slots in
  the installed executable.
- Retained the exact 1.62.1.0 profile and all previously supported 1.61.x and
  1.60.x profiles.
- Kept unknown or mismatched game builds in telemetry-only mode without
  partial native hook installation.
- Left the independent Reverse Planner mod unchanged.

## Supported builds

- ETS2 1.62.2.1 Steam public, exact executable hash profile.
- ETS2 1.62.1.0 Steam public, exact executable hash profile.
- ETS2 1.61.1.0, 1.61.1.1, and 1.61.1.9 Steam public profiles.
- Supported ETS2 1.60.x builds.
- Windows x64.

The installed 1.62.2.1 executable identifies as commit
`4c8b31398d9e255afb851981f4bb9eaee7c3791b` and has SHA-256
`8F17D3505C2E6C25A93F38A1119FFBBCCFBA6A3BE2EDDA3771E0186276AD9434`.

Other 1.62 builds safely keep telemetry active and skip native hooks until a
matching profile is added.

## Packages

- `ETS2_Reverse_Posture_Assistant_v1.62.1_Full.zip`: mod and native runtime.
- `ETS2_Reverse_Posture_Assistant_v1.62.1_Runtime_for_Workshop.zip`: runtime
  installer for Workshop subscribers.
- `ETS2_Reverse_Posture_Assistant_v1.62.1_Upgrade_from_v0.6.0.zip`: migration
  installer for the v0.6.0 runtime.

Exit ETS2 before installation. Update both the mod content and runtime DLL.
Disable older local copies and do not enable local and Workshop copies together.

## 中文

本版本为本机 Steam 正式版 Euro Truck Simulator 2 1.62.2.1 新增精确原生适配，并保留既有 1.62.1.0、
1.61.x 与 1.60.x 配置。模组版本号沿用与游戏版本一致的 `v1.62.x` 系列。

- 新增按 SHA-256 精确匹配的 ETS2 1.62.2.1 `BuildProfile`。
- 为四个启用的原生 Hook 及四个活动模型生命周期辅助函数验证了精确 RVA 与字节签名。
- 验证了安装版可执行文件中的车辆渲染分发结构与挂车渲染调用槽。
- 保留精确匹配的 1.62.1.0 配置及所有既有 1.61.x 与 1.60.x 配置。
- 未匹配构建安全降级至仅遥测模式，不安装部分原生 Hook。
- 独立 Reverse Planner 模组未修改。

本机 1.62.2.1 可执行文件的提交标识为
`4c8b31398d9e255afb851981f4bb9eaee7c3791b`，SHA-256 为
`8F17D3505C2E6C25A93F38A1119FFBBCCFBA6A3BE2EDDA3771E0186276AD9434`。

其他未匹配的 1.62 构建会保留遥测并跳过原生 Hook，直到为其加入匹配配置。

安装前请退出游戏，并同时更新模组资源与运行组件 DLL。停用旧的本地副本，不要同时启用本地版和工坊版。
