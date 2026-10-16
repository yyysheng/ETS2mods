# Building from source

The native entity build targets verified Euro Truck Simulator 2 `1.60.x`, exact
`1.61.1.0`, exact `1.61.1.1`, and exact `1.61.1.9` Windows x64 executables through
`src/entity_runtime/build_profiles.hpp`.

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

Each exact build adds one hash-bound `BuildProfile`. Every profile checks all enabled hooks at their verified RVAs. The 1.61 profiles additionally check the four active model-lifecycle helper signatures and use profile-specific helper RVAs. Inactive helpers such as the unused model-activation entry are not compatibility gates. Any missing profile or active hook/helper mismatch leaves the plug-in in telemetry-only mode without installing MinHook targets.

The compiled telemetry plug-in and packaged `.scs` archive are distributed in GitHub Releases rather than committed to this repository.

## 中文

新运行时通过多版本 `BuildProfile` 面向经验证的 ETS2 1.60.x、1.61.1.0、
1.61.1.1 和 1.61.1.9 Windows x64 构建。1.61 配置还会校验模型所有权、参数初始化、
父节点和姿态变换辅助函数入口；任一活动 Hook 或辅助函数不匹配时均跳过全部原生 Hook，
安全降级到仅遥测模式。1.61.1.9 使用独立的精确构建配置。
