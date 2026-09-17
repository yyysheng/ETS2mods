# Building from source

The native entity build targets verified Euro Truck Simulator 2 `1.60.x` and exact `1.61.1.0` Windows x64 executables through `src/entity_runtime/build_profiles.hpp`.

## Requirements

- Visual Studio 2026 with Desktop development with C++ and the v145 toolset.
- .NET Framework 4.7.2 developer pack.
- SCS SDK telemetry headers in `third_party/scs-sdk-plugin/scs_sdk/include`.
- MinHook source files and headers in `third_party/reshade/deps/minhook` (only MinHook is used; ReShade itself is not linked or loaded).
- SCS Conversion Tools 2.21 for the collisionless PMD/PMG frame assets.

## Components

1. Build `src/entity_runtime/ETS2ReverseEntityRuntime.vcxproj` as `Release|x64`.
2. Build and run `src/entity_runtime/BuildProfileTests.vcxproj` as `Release|x64`.
3. Run `tools/GenerateEntityModels.ps1`, then convert the generated PIM/PIT files with SCS Conversion Tools.
4. Copy the resulting PMD/PMG and automat material into `src/mod`; include the generated DDS/TOBJ but no PMC.
5. Run `tools/PackageMod.ps1` to validate and package `src/mod`; place the DLL in `bin/win_x64/plugins`. Never package base-game `model/symbol` overrides.

Each exact build adds one hash-bound `BuildProfile`. A signature-compatible profile may be used only when every enabled hook retains its verified RVA and complete signature. Inactive lifecycle helpers must not become compatibility gates. Any missing profile or enabled-hook mismatch leaves the plug-in in telemetry-only mode without installing MinHook targets.

The compiled telemetry plug-in and packaged `.scs` archive are distributed in GitHub Releases rather than committed to this repository.

## 中文

新运行时通过多版本 `BuildProfile` 面向经验证的 ETS2 1.60.x 和精确的 1.61.1.0 Windows x64 构建。不匹配时跳过全部原生 Hook 并安全降级到仅遥测模式。
