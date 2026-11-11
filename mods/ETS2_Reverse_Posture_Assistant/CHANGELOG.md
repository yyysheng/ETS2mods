# Changelog

## 1.62.1 - 2026-11-11

- Added an exact SHA-256-bound native profile for ETS2 1.62.2.1 Steam public.
- Verified the four enabled hook signatures, four active model-lifecycle helper
  signatures, and the vehicle render dispatch structure in the installed binary.
- Retained the exact ETS2 1.62.1.0 profile and supported 1.61.x and 1.60.x builds.

## 1.62.0 - 2026-11-06

- Added an exact SHA-256-bound native profile for ETS2 1.62.1.0 Steam public.
- Added profile-specific checks and RVAs for the four hooks and four active
  model lifecycle helpers used by the native world-entity runtime.
- Kept unknown 1.62 builds fail-closed in telemetry-only mode and retained the
  supported 1.61.x and 1.60.x profiles.
- Started the game-aligned mod version scheme: releases use `v1.62.x` from this
  version onward.

## 0.11.2 - 2026-10-16

- Added an exact ETS2 1.61.1.9 Steam executable profile with relocated hooks,
  model ownership/parameter helpers and scene-node helper signatures.
- Kept all 1.61 profiles hash-bound and fail-closed to telemetry-only mode on
  any active hook or helper mismatch.
- Retained the v0.10.10 trajectory and tyre-ground geometry; left the separate
  Reverse Planner mod untouched.
- Retained the exact-build fail-closed behavior and telemetry-only fallback.

## 0.11.1 - 2026-09-25

- Added the exact ETS2 1.61.1.1 Steam executable profile and its relocated
  native hook and scene-node helper entries.
- Retained the verified 1.61.1.0 and 1.60 profiles plus the v0.10.10
  trajectory and tyre-ground geometry.
- Left the independent Reverse Planner mod untouched.

## 0.11.0 - 2026-09-17

- Added the exact ETS2 1.61.1.0 Steam executable profile and relocated four
  native hooks plus model creation helpers.
- Kept the 1.60 profiles and 0.10.10 trajectory and tyre-ground geometry.
- Added 1.61 package compatibility; the 1.61.1.0 public build passed in-game acceptance.
- Left the independent Reverse Planner mod untouched.

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
