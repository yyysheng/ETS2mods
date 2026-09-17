# ETS2 Reverse Posture Assistant v0.11.0

See [v0.11.0 release notes](RELEASE_NOTES_v0.11.0.md). The following is the previous v0.10.10 release history.

## Previous v0.10.10 release

This release improves the five-metre short-horizon tractor/trailer prediction
and fixes false guide slope caused by unequal suspension height on level ground.

## Changes

- Fits guide pitch and roll from grounded tyre geometry using configured wheel
  positions/radii plus live suspension deflection. Chassis attitude is retained
  only as a fail-safe when wheel data is unavailable.
- Uses the measured effective front-wheel angle rather than relying only on the
  generic normalized steering-to-angle conversion.
- Includes both longitudinal and lateral fifth-wheel offsets in rigid hitch
  propagation.
- Preserves the independent steering angle of the grounded trailer equivalent
  axle, including Ackermann tangent averaging.
- Holds the previous stable wheel topology during lift/lower animation and
  atomically commits wheelbase, axle-to-hook distance, ground plane and body
  envelope after 400 ms of stability.
- Keeps the v0.10.9 restoration of the original game trailer marker and the
  verified four-hook compatibility/fail-closed behavior.

## Reverse Planner compatibility

Reverse Planner remains a separate mod and was not modified. v0.10.10 retains
the existing `ETS2ReverseEntityRuntime.dll` name, `/model/reverse_assist/`
resources and four legacy hook sites. It does not use the planner's
`ETS2ReversePlannerRuntime.dll`, `/model/reverse_planer/` resources or add a
second planner hook. Both DLL load orders and both telemetry shutdown orders
are covered by the existing compatibility suite.

## Downloads and upgrade

- `Full.zip`: standalone mod plus DLL; run `Install-Full.bat`.
- `Runtime_for_Workshop.zip`: DLL installer for Workshop subscribers; run
  `Install-Runtime-Only.bat`.
- `Upgrade_from_v0.6.0.zip`: migration installer for the old release; run
  `Upgrade-From-v0.6.0.bat`.

Exit ETS2 before installation. Update both the mod content and runtime DLL,
then restart. Disable older local copies and do not enable standalone and
Workshop copies together.

## 中文

v0.10.10 优化固定 5 米的车头/单挂短程倒车外推，并修复平地悬挂高低不同时轨迹线被错误绘制成坡面的故障。

- 坡度由接地轮的配置位置、轮胎半径及实时悬挂位移拟合；只有轮胎数据不可用时才安全退回车体姿态。
- 使用实际前轮转角，并把鞍座相对后轴的纵向、横向偏移都纳入刚性挂点传播。
- 保留挂车接地等效轴的独立转角，左右轮先平均转角正切值。
- 提升/落轴动画期间冻结上一组稳定轮组；接地掩码稳定 400 ms 后一次性提交轴距、挂点距离、地面平面及车体包络。
- 保留 v0.10.9 的原游戏挂车指引框修复及四 Hook 兼容性安全降级。
- Reverse Planner 是独立 mod，本次没有修改其源码、DLL、Hook 或绿色引导资源。

本版已完成 Release x64 构建、BuildProfileTests、KinematicsTests、模组内容检查及 Reverse Planner 双 DLL 兼容回归。游戏内视觉验收仍需在 ETS2 1.60.1.7 中完成。
