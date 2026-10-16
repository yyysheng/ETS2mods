# ETS2 Reverse Posture Assistant v0.11.2

This compatibility update adds an exact native profile for the installed
Euro Truck Simulator 2 1.61.1.9 Steam public executable. It retains the
1.61.1.0, 1.61.1.1 and 1.60.x profiles, the v0.10.10 prediction changes and
the independent Reverse Planner boundary.

## Changes

- Adds a SHA-256-bound `BuildProfile` for ETS2 1.61.1.9.
- Relocates and verifies the four enabled native hooks, including the vehicle
  render dispatch call sites, for the 1.61.1.9 executable.
- Adds exact profile-specific signatures and entry points for model ownership
  transfer, model parameter initialization, scene-node parenting and transform
  updates.
- Keeps unknown or mismatched executable builds in safe telemetry-only mode;
  no partial native hook installation is attempted.
- Keeps the v0.10.10 tyre-contact ground-plane fit so unequal suspension height
  on level ground does not create a false guide slope.

## Reverse Planner compatibility

Reverse Planner remains a separate mod and was not modified. This update keeps
`ETS2ReverseEntityRuntime.dll` and `/model/reverse_assist/` separate from the
planner's DLL and `/model/reverse_planer/` resources.

## Downloads and upgrade

- `Full.zip`: standalone mod plus DLL; run `Install-Full.bat`.
- `Runtime_for_Workshop.zip`: DLL installer for Workshop subscribers; run
  `Install-Runtime-Only.bat`.
- `Upgrade_from_v0.6.0.zip`: migration installer for the old release; run
  `Upgrade-From-v0.6.0.bat`.

Exit ETS2 before installation. Update both the mod content and runtime DLL,
then restart. Disable older local copies and do not enable standalone and
Workshop copies together.

## Validation

The installed executable hash, all four enabled hook signatures, four model
helper signatures and relocated render-dispatch call sites passed offline
checks. `BuildProfileTests`, `KinematicsTests`, the Release x64 runtime build
and release package checks passed.

The installed 1.61.1.9 executable has SHA-256
`B70C2357CC56EB5834EE47FEA3C60B2F2D4591383FF2AC479CFD3CD254301B8C`.

## 中文

v0.11.2 为本机 Euro Truck Simulator 2 1.61.1.9 Steam 正式版新增精确原生适配，
并保留 1.61.1.0、1.61.1.1 与 1.60.x 配置、v0.10.10 预测算法及独立 Reverse Planner。

- 新增按可执行文件 SHA-256 精确匹配的 1.61.1.9 `BuildProfile`。
- 重新定位并离线校验四个原生 Hook，以及车辆渲染分发函数内的调用点。
- 为模型所有权转移、参数初始化、场景节点父子关系和姿态变换分别配置并校验入口签名。
- 未知或签名不匹配的构建会安全降级为仅遥测，不安装部分原生 Hook。
- 保留轮胎接地几何坡度拟合，避免平地悬挂高低差产生虚假坡度。
- Reverse Planner 是独立 mod，本次未修改其 DLL、资源、Hook 或状态机。

本次已通过本机可执行文件哈希/签名离线校验、`BuildProfileTests`、`KinematicsTests`、
Release x64 编译及版本包检查。
游戏内视觉效果与退出稳定性测试由用户进行；目前不宣称已完成游戏内验收。
