# Building from source

The native entity build targets the exact Euro Truck Simulator 2 `1.60.1.7` Windows x64 executable.

## Requirements

- Visual Studio 2026 with Desktop development with C++ and the v145 toolset.
- .NET Framework 4.7.2 developer pack.
- SCS SDK telemetry headers in `third_party/scs-sdk-plugin/scs_sdk/include`.
- MinHook source files and headers in `third_party/reshade/deps/minhook` (only MinHook is used; ReShade itself is not linked or loaded).
- SCS Conversion Tools 2.21 for the collisionless PMD/PMG frame assets.

## Components

1. Build `src/entity_runtime/ETS2ReverseEntityRuntime.vcxproj` as `Release|x64`.
2. Run `tools/GenerateEntityModels.ps1`, then convert the generated PIM/PIT files with SCS Conversion Tools.
3. Copy the resulting PMD/PMG and automat material into `src/mod`; include the generated DDS/TOBJ but no PMC.
4. Package `src/mod` as a ZIP-compatible `.scs` archive and place the DLL in `bin/win_x64/plugins`.

The compiled telemetry plug-in and packaged `.scs` archive are distributed in GitHub Releases rather than committed to this repository.

## 中文

新运行时严格面向 ETS2 1.60.1.7、Windows x64。它不依赖 ReShade、摄像头、深度缓冲或渲染数据；输入来自 SCS 遥测，输出是无 PMC 碰撞文件的 Prism3D 世界实体。
