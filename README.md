# ETS2 Reverse Posture Assistant

An in-cab reverse posture and trajectory assistant for **Euro Truck Simulator 2 1.60**.

When reverse gear is selected, the supported factory infotainment screen switches from its normal page to a live 2D maneuver view. It predicts the tractor and connected trailer posture from steering and articulation telemetry, and overlays nearby road, yard, and terrain outlines within a 30 m working area. It does not change the player camera and does not reuse mirror or camera feeds.

![ETS2 Reverse Posture Assistant](assets/workshop_cover.jpg)

## Features

- Live tractor reverse trajectory.
- Trailer articulation and future posture prediction.
- Nearby map and terrain outlines with a performance-focused 30 m range.
- Automatic display only while reverse gear is selected.
- No forced camera movement and no mirror-camera conflicts.
- Support for 14 original ETS2 1.60 infotainment layouts from DAF, Iveco, MAN, Renault Trucks, Scania, and Volvo.
- Gamepad-independent rendering.

## Installation

### Full standalone package

1. Download `ETS2_Reverse_Posture_Assistant_v0.6.0_Full.zip` from [Releases](https://github.com/yyysheng/ETS2mods/releases/latest).
2. Extract it and run `Install-Full.bat`.
3. Enable **ETS2 Reverse Posture Assistant** in the ETS2 Mod Manager.

### Steam Workshop package

1. Subscribe to the Workshop item.
2. Download `ETS2_Reverse_Posture_Assistant_v0.6.0_Runtime_for_Workshop.zip` from [Releases](https://github.com/yyysheng/ETS2mods/releases/latest).
3. Extract it and run `Install-Runtime-Only.bat`.

The Workshop cannot install the required ReShade add-on and telemetry runtime into the game directory, so the GitHub runtime package is required.

## Supported game version

- Euro Truck Simulator 2 `1.60.*`
- Windows x64 / DirectX 11

## 中文说明

**欧卡 2 倒车姿态助手**适用于 Euro Truck Simulator 2 1.60。挂入倒挡后，支持的原版娱乐屏会自动切换到 2D 倒车姿态画面，根据方向盘角度、车头姿态和挂车铰接数据预测未来轨迹，并显示 30 m 范围内的道路、货场与地形轮廓。

主要特点：

- 实时预测车头和挂车倒车轨迹。
- 仅在倒挡时显示，不影响原车娱乐屏的正常页面。
- 不切换玩家视角，不调用前视镜、后视镜或摄像机画面。
- 适配 DAF、Iveco、MAN、Renault Trucks、Scania、Volvo 共 14 个原版娱乐屏布局。
- 保持较低性能开销，不依赖手柄输入。

完整安装请下载 Release 中的 `Full` 包；通过 Steam 创意工坊订阅时，还需要安装 Release 中的 `Runtime_for_Workshop` 运行组件。

## Credits

- Project author: [yyysheng](https://github.com/yyysheng)
- ReShade add-on API: [ReShade](https://reshade.me/)
- ETS2 telemetry bridge: SCS SDK telemetry plug-in
- Map parsing: TsMap / TruckLib ecosystem

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for license details.

Source build instructions are available in [BUILDING.md](BUILDING.md).
