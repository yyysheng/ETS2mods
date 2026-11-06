# ETS2 Reverse Posture Assistant v1.62.0

This release adds exact native compatibility with the installed Steam public
Euro Truck Simulator 2 1.62.1.0 Windows x64 executable. It retains the
supported 1.61.x and 1.60.x profiles. Starting with this release, mod versions
follow the game version series and use the `v1.62.x` format.

## Changes

- Added a SHA-256-bound `BuildProfile` for ETS2 1.62.1.0.
- Added exact RVAs and byte signatures for the four enabled native hooks.
- Added exact signatures and RVAs for model ownership transfer, model parameter
  initialization, scene-node parenting, and scene-node transform updates.
- Generalized profile-specific helper selection for 1.61 and 1.62 builds.
- Kept unknown or mismatched game builds in telemetry-only mode without partial
  native hook installation.
- Retained all previously supported 1.61.x and 1.60.x profiles and the existing
  reverse prediction behavior.
- Left the independent Reverse Planner mod unchanged.

## Supported builds

- ETS2 1.62.1.0 Steam public, exact executable hash profile.
- ETS2 1.61.1.0, 1.61.1.1, and 1.61.1.9 Steam public profiles.
- Supported ETS2 1.60.x builds.
- Windows x64.

Other 1.62 builds safely keep telemetry active and skip native hooks until a
matching profile is added.

The installed 1.62.1.0 executable identifies as commit
`1afd6ae638406ad3953c6c8940f96d0e25a3eeae` and has SHA-256
`A938CB388121F9B884A2E1B0F048C230A7BC19638EF6D641A70775F8C7FEA956`.

## Packages

- `ETS2_Reverse_Posture_Assistant_v1.62.0_Full.zip`: mod and native runtime.
- `ETS2_Reverse_Posture_Assistant_v1.62.0_Runtime_for_Workshop.zip`: runtime
  installer for Workshop subscribers.
- `ETS2_Reverse_Posture_Assistant_v1.62.0_Upgrade_from_v0.6.0.zip`: migration
  installer for the v0.6.0 runtime.

Exit ETS2 before installation. Update both the mod content and runtime DLL.
Disable older local copies and do not enable local and Workshop copies together.

## 中文

本版本为本机 Steam 正式版 Euro Truck Simulator 2 1.62.1.0 新增精确原生适配，
并保留已支持的 1.61.x 与 1.60.x 配置。从本版本开始，模组版本号与游戏版本系列一致，
采用 `v1.62.x` 命名。

- 新增按 SHA-256 精确匹配的 ETS2 1.62.1.0 `BuildProfile`。
- 新增四个原生 Hook、模型所有权转移、参数初始化、场景节点父子关系和姿态变换的
  精确 RVA 与字节签名。
- 统一 1.61 与 1.62 的配置专属辅助函数选择方式。
- 未知或签名不匹配的游戏构建会安全降级为仅遥测模式，不安装部分原生 Hook。
- 保留既有 1.61.x 与 1.60.x 配置以及倒车预测功能。
- 独立 Reverse Planner 模组未在本次更新。

已识别的本机游戏版本为 1.62.1.0，提交标识为
`1afd6ae638406ad3953c6c8940f96d0e25a3eeae`，可执行文件 SHA-256 为
`A938CB388121F9B884A2E1B0F048C230A7BC19638EF6D641A70775F8C7FEA956`。
其他 1.62 构建会保留遥测并跳过原生 Hook，待加入匹配配置后再启用。

ETS2 1.62.1.0 实机测试已通过。
