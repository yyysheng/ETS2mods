# Building from source

The published release is built for Euro Truck Simulator 2 1.60 on Windows x64 / DirectX 11.

## Requirements

- Visual Studio 2026 with Desktop development with C++ and the v145 toolset.
- .NET Framework 4.7.2 developer pack.
- ReShade 6.7.3 source headers in `third_party/reshade/include`.
- A compatible `TsMap.dll` in `src/environment/bin`.

## Components

1. Build `src/runtime/ETS2ReverseScreenRuntime.vcxproj` as `Release|x64`. The ReShade add-on is written to `build/runtime/ETS2ReverseScreenRuntime.addon64`.
2. Build `src/environment/EnvironmentService.csproj` in Release mode. Copy its output and runtime dependencies beside the add-on.
3. Run `tools/GenerateReversePostureLayouts.ps1` to regenerate the supported original dashboard layouts when the base game UI definitions change.
4. Package the contents of `src/mod` as an uncompressed or ZIP-compatible `.scs` archive.

The large generated terrain index and third-party runtime binaries are distributed in GitHub Releases rather than committed to this repository.

## 中文

公开源码面向 ETS2 1.60、Windows x64 和 DirectX 11。编译前需将 ReShade 6.7.3 头文件放入 `third_party/reshade/include`，并将兼容的 `TsMap.dll` 放入 `src/environment/bin`。生成后的大型地形索引及第三方运行库不纳入 Git 仓库，只随 GitHub Release 发布。
