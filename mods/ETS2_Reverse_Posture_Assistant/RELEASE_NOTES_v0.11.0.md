# ETS2 Reverse Posture Assistant v0.11.0

This release targets Euro Truck Simulator 2 1.61.x, with native rendering
verified for the 1.61.1.0 Steam public executable. It retains the 1.60.x profiles and the
v0.10.10 tractor/trailer prediction and tyre-ground slope improvements.

## Changes

- Adds an exact SHA-256-bound `BuildProfile` for ETS2 1.61.1.0.
- Relocates and verifies the four enabled native hooks, including the vehicle
  render dispatcher that calls the tractor and trailer draw methods.
- Relocates and checks model ownership, parameter initialization, parent and
  transform helpers used by the 1.61 rendering path.
- Preserves the original game trailer marker, collisionless world-space lines,
  five-metre swept-area prediction and safe telemetry-only fallback for
  unrecognized game builds.
- Keeps the v0.10.10 ground plane fitted from tyre contact geometry so unequal
  suspension height on level ground does not create a false road slope.

## Reverse Planner compatibility

Reverse Planner remains a separate mod and was not modified. v0.11.0 keeps
`ETS2ReverseEntityRuntime.dll` and `/model/reverse_assist/` resources separate
from the planner's DLL and `/model/reverse_planer/` resources. Both plug-ins can
register telemetry together. The currently installed Reverse Planner runtime
still declines native hooks on ETS2 1.61.1.0; its green route guide requires
its own update.

## Downloads and upgrade

- `Full.zip`: standalone mod plus DLL; run `Install-Full.bat`.
- `Runtime_for_Workshop.zip`: DLL installer for Workshop subscribers; run
  `Install-Runtime-Only.bat`.
- `Upgrade_from_v0.6.0.zip`: migration installer for the old release; run
  `Upgrade-From-v0.6.0.bat`.

Exit ETS2 before installation. Update both the mod content and runtime DLL,
then restart. Disable older local copies and do not enable standalone and
Workshop copies together. The 1.61.1.0 public executable tested for this
release has SHA-256
`4DCB548CAAD924254A60AF7C3BD1DB69DCAF42F7ADF19B5BB5D77EBA2814AF21`.

## 中文

v0.11.0 对应 Euro Truck Simulator 2 1.61.x，原生绘制已验证 1.61.1.0 Steam 正式版，并保留 1.60.x 兼容配置、v0.10.10 的车头与挂车短程预测和轮胎接地坡度算法。

- 新增按游戏可执行文件 SHA-256 精确匹配的 1.61.1.0 `BuildProfile`。
- 重新定位并校验四个原生 Hook，车辆绘制入口对应车头与挂车的实际绘制调用。
- 更新 1.61 模型所有权、参数初始化、父节点与变换辅助函数入口。
- 保留原游戏挂车标记、无碰撞世界线条、固定 5 米扫掠预测，以及未知构建仅遥测的安全降级。
- 保留轮胎接地几何坡度拟合，避免平地前后悬挂高低不同导致轨迹线出现虚假的坡度。
- Reverse Planner 是独立 mod，本次没有修改其 DLL、资源或 Hook；它当前在 1.61.1.0 下的绿色路线仍需单独适配。

本版已完成 Release x64 构建、BuildProfileTests、KinematicsTests、模组包检查及双 DLL 遥测兼容回归，并经游戏内人工验收。
