# Changelog

## 0.10.10 - 2026-09-11

- Replaced chassis-pitch/roll guide height with a grounded-tyre plane fitted
  from wheel positions, radii and live suspension deflection.
- Added actual front-wheel steering, lateral fifth-wheel offset and independent
  trailer equivalent-axle steering to the short-horizon predictor.
- Debounced liftable-axle support topology for 400 ms and commits wheelbase,
  hitch distance, tyre plane and body envelope atomically.
- Preserved the independent Reverse Planner DLL, hook ownership and model
  namespace; no Reverse Planner source or asset is modified by this release.

## 0.10.9 - 2026-09-05

- Restored the base-game trailer guide marker by removing the loading model override.
- Removed native marker visibility/placement writes, including pause-time suppression.
- Removed the test marker override from model generation; prediction models stay independent.

## 0.10.8 - 2026-08-10

- Added multi-entry `BuildProfile` compatibility selection.
- Restricted compatibility signature checks to the four hooks actually enabled.
- Added exact-profile and signature-compatible-profile tests.
- Added per-profile, per-hook, and telemetry-only fallback diagnostics.
- Made failed compatibility and hook installation degrade without partial hooks.

## 0.6.0 - 2026-07-18

- Renamed the project to ETS2 Reverse Posture Assistant.
- Added 14 original ETS2 1.60 infotainment layouts.
- Added live tractor and multi-trailer posture prediction.
- Added nearby road, yard, and terrain outlines within a 30 m working area.
- Removed experimental runtime vehicle detection.
- Removed all camera and mirror-feed dependencies.
- Reduced reverse-mode rendering overhead.
