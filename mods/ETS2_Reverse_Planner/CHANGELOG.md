# Changelog

## Unreleased

- Reject and delete captured task targets that fall outside the 50 m local telemetry radius, preventing absolute coordinates from surviving a scene or job change.
- Bound curve sampling and route length before articulated-state propagation so malformed or stale targets cannot create multi-second frame-end work.
- Cache successful and failed stationary poses, replan only after meaningful pose movement, and reduce the full-route cadence to 5 Hz while retaining per-frame cab-arrow updates.

## v0.1.0-beta

- Initial public beta release for ETS2 1.60.x.
- Added native task-parking-frame identification and attached-trailer route planning.
- Added solid/dashed twin green tractor-tail tracks, cab-side steering arrows and red-X failure state.
- Added wheel/hook geometry adaptation, steerable/liftable trailer axle handling and bounded physical fallback planning.
- Added cooperative loading and shutdown compatibility with Reverse Posture Assistant v0.10.10.
