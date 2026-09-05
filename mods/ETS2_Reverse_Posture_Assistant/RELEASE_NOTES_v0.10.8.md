# ETS2 Reverse Posture Assistant v0.10.8

Hotfix based on tag `reverse-posture-assistant-v0.10.7` at commit
`6f2f7764fc72e9c66c0da961aae3f8b959d38222`.

## Compatibility hotfix

- Selects runtime offsets and signatures through multiple `BuildProfile` entries.
- Keeps the exact ETS2 1.60.1.7 Steam public executable profile.
- Accepts byte-compatible 1.60 rebuilds only when all four enabled hook RVAs and
  signatures match the verified layout.
- Ignores inactive lifecycle helper signatures instead of rejecting a compatible
  executable for code that this release does not hook.
- Logs the executable digest, selected profile, match method, enabled hook count,
  each verified hook RVA, and the reason for any fallback.
- On an unknown or mismatched build, installs no native hooks and continues in
  telemetry-only mode.

## Verification

Build and run `BuildProfileTests.vcxproj`, `KinematicsTests.vcxproj`, and then
build `ETS2ReverseEntityRuntime.vcxproj` in `Release|x64`.
