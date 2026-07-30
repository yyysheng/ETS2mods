# ETS2 Reverse Posture Assistant v0.10.7

World-entity release for Euro Truck Simulator 2 1.60.1.7 on Windows x64.

## Highlights

- Creates collisionless prediction lines as Prism3D world entities; no camera, mirror, render-pass, depth-buffer, or post-process data is captured.
- Displays two blue tractor swept-area boundaries and two orange trailer swept-area boundaries over a fixed 5 m reverse path.
- Uses articulation, actual wheel steering, ground contact, lift state, and adaptive virtual axles for different tractor and trailer wheelbases.
- Supports liftable and steerable tractor/trailer axles, short trailers, multi-axle tractors, slopes, and large articulation angles.
- Shows the prediction only in reverse gear and releases the world objects during pause and shutdown.
- Does not install, replace, rename, or remove `dxgi.dll` or `d3d11.dll`, avoiding the v0.6.0 proxy-DLL conflict with Snowymoon, ReShade, and other graphics mods.

## Downloads

- `ETS2_Reverse_Posture_Assistant_v0.10.7_Full.zip`: complete standalone installation including the `.scs` mod and telemetry runtime.
- `ETS2_Reverse_Posture_Assistant_v0.10.7_Runtime_for_Workshop.zip`: telemetry runtime only; use this after subscribing on Steam Workshop.
- `ETS2_Reverse_Posture_Assistant_v0.10.7_Upgrade_from_v0.6.0.zip`: migration package for v0.6.0 users. It backs up and disables only the old Reverse Assistant runtime, installs v0.10.7, and leaves shared proxy DLLs and ambiguous third-party libraries untouched.

## 中文

这是适用于 Euro Truck Simulator 2 1.60.1.7（Windows x64）的世界实体版本。挂入倒挡后，插件通过官方遥测数据计算固定 5 米的倒车横扫范围，并在游戏世界中显示两条蓝色车头边界和两条橙色挂车边界。轨迹会考虑车头与挂车夹角、实际车轮转角、接地状态、提升桥、随动转向桥、不同轴距和坡面姿态。

本版本不抓取摄像头、后视镜或渲染数据，不再依赖图形代理 DLL。安装与升级过程均不会修改、替换、重命名或删除 `dxgi.dll` 和 `d3d11.dll`，因此可以规避 0.6.0 与雪月、ReShade 等同样使用图形代理 DLL 的模组之间的冲突。

- 独立安装请选择 `Full` 包。
- 通过 Steam 创意工坊订阅后请选择 `Runtime_for_Workshop` 包。
- 已安装 0.6.0 的用户请选择 `Upgrade_from_v0.6.0` 包。

## Checksums (SHA-256)

- Full: `AB6FAF93926BD08DC180E7BF059B38D48FA906E1BD2AC6096BD2F5455FE513C8`
- Runtime for Workshop: `0308C8D3FF5E9D5C761DE2E2D4BB5D385C92A610DEB820076A6B0641FF625E20`
- Upgrade from v0.6.0: `143749F00974A28194033E3297991D6B39F7E8420C4CC0600D0C771C5C6DBA5F`
