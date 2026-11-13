# Building from source

The native entity build targets ETS2 1.62.3.2 Steam Windows x64 via the exact executable profile in `src/entity_runtime/build_profiles.hpp`.

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

The runtime checks the executable SHA-256 and verifies all four hook and four lifecycle helper signatures before installing native hooks. A mismatch leaves the plug-in in telemetry-only mode.
