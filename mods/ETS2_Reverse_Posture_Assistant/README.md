# ETS2 Reverse Posture Assistant

A world-space reverse posture and trajectory assistant for verified
**Euro Truck Simulator 2 1.61.x and 1.60.x** Windows x64 builds. Native rendering
on 1.61 is verified for the 1.61.1.0 Steam public executable.

[Repository folder](https://github.com/yyysheng/ETS2mods/tree/main/mods/ETS2_Reverse_Posture_Assistant) | [Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3776052935) | [GitHub release v0.11.0](https://github.com/yyysheng/ETS2mods/releases/tag/reverse-posture-assistant-v0.11.0) | [Previous v0.10.10 release](https://github.com/yyysheng/ETS2mods/releases/tag/reverse-posture-assistant-v0.10.10)

When reverse gear is selected, the plug-in predicts tractor and trailer posture from official
telemetry and creates collisionless frame models directly in the game world. The frames are normal
Prism3D entities, so every game camera can see the same objects. No camera feed, render pass, depth
buffer, constant buffer, or post-process overlay is read or modified.

![ETS2 Reverse Posture Assistant](assets/workshop_cover.jpg)

## Features

- Live tractor reverse trajectory.
- Trailer articulation and future posture prediction.
- Separate blue tractor and orange trailer swept-area boundaries over a fixed 5 m path.
- Ground-contact-aware virtual axles for liftable and steerable tractor/trailer axles.
- Ground slope fitted from tyre contact geometry instead of suspension-sensitive
  chassis pitch and roll.
- Collisionless low-profile line entities placed on the predicted ground path.
- Automatic display only while reverse gear is selected.
- Visible through normal game rendering from exterior, interior, free, and mirror views.
- Multiple `BuildProfile` compatibility entries for the exact 1.61.1.0 and
  1.60.1.7 Steam executables plus the verified 1.60 hook layout.
- Only the four hooks enabled by this release are signature-checked. A missing or
  changed enabled hook keeps telemetry available but skips all native hooks.
- No ReShade, `dxgi.dll`, `d3d11.dll`, or graphics API dependency.

## v0.11.0 update

Adds an exact ETS2 1.61.1.0 native profile and preserves the v0.10.10 tyre
slope and articulated prediction changes. See
[release notes](RELEASE_NOTES_v0.11.0.md).

## Installation

### Full standalone package

1. Download the `ETS2_Reverse_Posture_Assistant_v0.11.0_Full.zip` release package.
2. Extract it and run `Install-Full.bat`.
3. Enable **ETS2 Reverse Posture Assistant** in the ETS2 Mod Manager.

### Steam Workshop package

1. Subscribe to the [Steam Workshop item](https://steamcommunity.com/sharedfiles/filedetails/?id=3776052935).
2. Download the `ETS2_Reverse_Posture_Assistant_v0.11.0_Runtime_for_Workshop.zip` release package.
3. Extract it and run `Install-Runtime-Only.bat`.

The Workshop cannot install the telemetry plug-in into the game directory, so the GitHub runtime package is required.

### Upgrade from v0.6.0

1. Download `ETS2_Reverse_Posture_Assistant_v0.11.0_Upgrade_from_v0.6.0.zip`.
2. Exit ETS2, extract the package, and run `Upgrade-From-v0.6.0.bat`.
3. The installer backs up and disables the old Reverse Assistant runtime, then installs v0.11.0.

The upgrade does not modify or remove `dxgi.dll`, `d3d11.dll`, or ambiguous shared
libraries. This allows Snowymoon, ReShade, and other graphics mods to keep their own proxy DLL.

## Supported game version

- Euro Truck Simulator 2 `1.61.x`; native rendering is verified for the exact
  `1.61.1.0` Steam public executable hash. Other 1.61 builds safely fall back
  to telemetry-only mode until verified.
- Euro Truck Simulator 2 `1.60.x` when an exact or enabled-hook-compatible `BuildProfile` matches
- Windows x64

## 中文说明

**欧卡 2 倒车姿态助手** v0.11.0 对应 Euro Truck Simulator 2 1.61.x，原生绘制已验证 1.61.1.0 Steam 正式版精确构建，并保留 1.60.x 的多版本 `BuildProfile`。其他未验证的 1.61 构建会跳过原生 Hook，保留遥测注册并写入诊断日志。

主要特点：

- 实时预测车头和挂车倒车轨迹。
- 蓝色车头扫掠边界与橙色挂车扫掠边界分别显示，固定预测 5 米。
- 根据车轮实际接地和转向状态适配提升桥、随动桥以及不同轴距车型。
- 坡度改由接地轮胎连线拟合，不再把前后或左右悬挂高度差误判为道路坡度。
- 使用实际前轮角、横向鞍座偏移与挂车等效轴转角进行短程铰接外推。
- 与独立 Reverse Planner 并存，不改其 DLL、Hook 或绿色引导资源。
- 规划路线 mod 自身目前尚未适配 1.61；需要单独更新才能在 1.61 显示绿色路线。
- 仅在倒挡时显示，不影响原车娱乐屏。
- 不抓取摄像头、渲染过程、深度或常量缓冲数据。
- 框线是 Prism3D 世界实体，各种正常视角看到的是同一组对象。
- 模型不含 PMC 碰撞文件，不参与车辆碰撞。
- 不安装、不替换也不删除 `dxgi.dll` 或 `d3d11.dll`。

完整安装请下载 Release 中的 `Full` 包；通过 Steam 创意工坊订阅时，还需要安装 `Runtime_for_Workshop`。从 0.6.0 升级请选择 `Upgrade_from_v0.6.0`，它会备份并停用旧版倒车助手运行组件，但保留其他模组可能使用的 `dxgi.dll`。

## Credits

- Project author: [yyysheng](https://github.com/yyysheng)
- Telemetry API: SCS SDK

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for license details.

Source build instructions are available in [BUILDING.md](BUILDING.md).
