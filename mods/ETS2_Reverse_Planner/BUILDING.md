# Building from source

## Requirements

- Visual Studio 2026 with Desktop development with C++ and the v145 toolset.
- SCS telemetry SDK headers under `third_party/scs-sdk-plugin/scs_sdk/include`.
- MinHook headers and source under `third_party/minhook`.
- PowerShell 7 or Windows PowerShell 5.1.

## Build

1. Run `tools/GenerateGameRuntime.ps1`.
2. Build `src/game_runtime/ETS2ReversePlannerRuntime.vcxproj` as `Release|x64`.
3. Build and run `src/Stage0Tests.vcxproj` and `src/Stage1ATests.vcxproj` as `Release|x64`.
4. Run `tools/PackageRelease.ps1` to create the `.scs`, Full and Runtime-for-Workshop archives.

The checked-in `src/upstream` files are a pinned, auditable baseline extracted from the compatible Reverse Posture Assistant runtime. ReversePlanner generates its independent runtime from that baseline; it does not modify the installed assistant DLL. Third-party SDK and MinHook source are not committed.
