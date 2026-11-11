# ETS2 Reverse Posture Assistant v1.62.2.1

This mod release uses version v1.62.2.1, exactly matching the installed Steam
public Euro Truck Simulator 2 1.62.2.1 Windows x64 build. Exact native profiles
for earlier builds remain available.

## Changes

- Added a SHA-256-bound BuildProfile for ETS2 1.62.2.1.
- Verified exact RVAs and byte signatures for all four enabled native hooks and
  four active model-lifecycle helpers.
- Verified the vehicle render dispatch structure and trailer render slots in
  the installed executable.
- Retained exact profiles for 1.62.1.0, 1.61.1.0, 1.61.1.1, 1.61.1.9 and
  1.60.1.7.
- Other 1.60 builds use native rendering only if all enabled hook addresses and
  signatures match the verified 1.60.1.7 layout.
- Unmatched builds stay in telemetry-only mode without partial native hooks.
- Left the independent Reverse Planner mod unchanged.

## Supported builds

- ETS2 1.62.2.1 Steam public, exact executable hash profile.
- ETS2 1.62.1.0 Steam public, exact executable hash profile.
- ETS2 1.61.1.0, 1.61.1.1 and 1.61.1.9 Steam public, exact profiles.
- ETS2 1.60.1.7 Steam public, exact executable hash profile.
- Other ETS2 1.60 builds only when all enabled hooks match the verified layout.
- Windows x64.

The installed 1.62.2.1 executable identifies as commit
4c8b31398d9e255afb851981f4bb9eaee7c3791b and has SHA-256
8F17D3505C2E6C25A93F38A1119FFBBCCFBA6A3BE2EDDA3771E0186276AD9434.

Other 1.62 and 1.61 builds safely retain telemetry and skip native hooks until
an exact profile is added.

## Packages

- ETS2_Reverse_Posture_Assistant_v1.62.2.1_Full.zip: mod and native runtime.
- ETS2_Reverse_Posture_Assistant_v1.62.2.1_Runtime_for_Workshop.zip: runtime
  installer for Workshop subscribers.
- ETS2_Reverse_Posture_Assistant_v1.62.2.1_Upgrade_from_v0.6.0.zip: migration
  installer for the v0.6.0 runtime.

Exit ETS2 before installation. Update both the mod content and runtime DLL.
Disable older local copies and do not enable local and Workshop copies together.

## 中文

本次模组版本为 v1.62.2.1，与本机 Steam 正式版 Euro Truck Simulator 2
1.62.2.1 Windows x64 构建完全一致，并保留既有精确原生适配配置。

- 新增按 SHA-256 精确匹配的 ETS2 1.62.2.1 BuildProfile。
- 为四个启用的原生 Hook 与四个活动模型生命周期辅助函数验证精确 RVA 和字节签名。
- 验证安装版可执行文件中的车辆渲染分发结构与挂车渲染调用槽。
- 保留 1.62.1.0、1.61.1.0、1.61.1.1、1.61.1.9 与 1.60.1.7 精确配置。
- 其他 1.60 构建仅在所有启用 Hook 地址和签名均匹配已验证布局时启用原生绘制。
- 未匹配的 1.62 与 1.61 构建安全降级至仅遥测模式，不安装部分原生 Hook。
- 独立 Reverse Planner 模组未修改。

本机 1.62.2.1 可执行文件的提交标识为
4c8b31398d9e255afb851981f4bb9eaee7c3791b，SHA-256 为
8F17D3505C2E6C25A93F38A1119FFBBCCFBA6A3BE2EDDA3771E0186276AD9434。

## Packages

- ETS2_Reverse_Posture_Assistant_v1.62.2.1_Full.zip：模组资源与运行组件。
- ETS2_Reverse_Posture_Assistant_v1.62.2.1_Runtime_for_Workshop.zip：工坊订阅者使用的运行组件。
- ETS2_Reverse_Posture_Assistant_v1.62.2.1_Upgrade_from_v0.6.0.zip：从 v0.6.0 运行组件升级的安装包。

安装前请退出 ETS2，并同时更新模组资源与运行组件 DLL。请停用旧的本地副本，不要同时启用本地版与工坊版。
