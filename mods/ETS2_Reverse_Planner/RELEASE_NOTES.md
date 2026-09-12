# v0.1.0-beta

Hotfix: stale task targets left behind by job or scene changes are now rejected and deleted outside the 50 m telemetry radius. Curve sampling and stationary/moving replanning are bounded to prevent severe stuttering and game hangs.

Initial public beta of ReversePlanner for verified Euro Truck Simulator 2 1.60.x Windows x64 builds.

- Locks the unique active game-native task parking frame.
- Continuously replans an attached trailer's reverse route.
- Draws two green tractor-tail tracks: 7 m solid, then dashed.
- Displays continuously updated cab-side steering arrows and a delayed unreachable X.
- Adapts to measured tractor, fifth-wheel and trailer axle geometry.
- Coexists with Reverse Posture Assistant through a cooperative hook sidecar.
- Does not use ReShade or rendered-image capture.

This beta is a driver aid, not an autonomous or collision-free parking system. Detached tractor-to-trailer guidance is intentionally disabled.
