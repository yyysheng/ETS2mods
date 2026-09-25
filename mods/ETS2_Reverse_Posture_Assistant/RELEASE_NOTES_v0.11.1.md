# ETS2 Reverse Posture Assistant v0.11.1

This compatibility update adds native rendering support for the installed
Euro Truck Simulator 2 1.61.1.1 Steam public executable. It retains the
verified 1.61.1.0 and 1.60.x profiles, the v0.10.10 prediction changes and
the independent Reverse Planner boundary.

## Changes

- Adds an exact SHA-256-bound `BuildProfile` for ETS2 1.61.1.1.
- Relocates and verifies the four enabled native hooks for the 1.61.1.1
  executable.
- Updates the 1.61.1.1 scene-node parent and transform helper entries while
  retaining the unchanged model transfer and parameter-init helpers.
- Keeps unknown executable builds in safe telemetry-only mode; no partial
  native hook installation is attempted.
- Keeps the v0.10.10 tyre-contact ground-plane fit so unequal suspension height
  on level ground does not create a false guide slope.

## Reverse Planner compatibility

Reverse Planner remains a separate mod and was not modified. This release keeps
`ETS2ReverseEntityRuntime.dll` and `/model/reverse_assist/` separate from the
planner's DLL and `/model/reverse_planer/` resources. Both plug-ins may register
telemetry together; the planner's 1.61 native route guide requires its own
separate update.

## Downloads and upgrade

- `Full.zip`: standalone mod plus DLL; run `Install-Full.bat`.
- `Runtime_for_Workshop.zip`: DLL installer for Workshop subscribers; run
  `Install-Runtime-Only.bat`.
- `Upgrade_from_v0.6.0.zip`: migration installer for the old release; run
  `Upgrade-From-v0.6.0.bat`.

Exit ETS2 before installation. Update both the mod content and runtime DLL,
then restart. Disable older local copies and do not enable standalone and
Workshop copies together.

The verified 1.61.1.1 public executable has SHA-256
`2014BFCC850A06108F4CA3C233A5BB6D31D7D83346C5D449F94C8E0675E5199A`.

## 中文

v0.11.1 离线新增 Euro Truck Simulator 2 1.61.1.1 Steam 正式版精确适配，保留
1.61.1.0 与 1.60.x 配置、v0.10.10 的车头/挂车短程预测及轮胎接地坡度算法。

- 新增按游戏可执行文件 SHA-256 精确匹配的 1.61.1.1 `BuildProfile`。
- 重新定位并校验 1.61.1.1 的四个原生 Hook。
- 更新 1.61.1.1 的模型父节点和变换辅助函数入口；模型所有权转移与参数初始化入口保持不变。
- 未识别的游戏构建继续安全降级为仅遥测，不安装部分原生 Hook。
- 保留轮胎接地几何坡度拟合，避免平地前后悬挂高低不同导致轨迹线出现虚假的坡度。
- Reverse Planner 是独立 mod，本次没有修改其 DLL、资源、Hook 或状态机。

本版完成离线可执行文件签名校验、BuildProfileTests、Release x64 构建和模组包检查；游戏内人工测试由用户进行。
