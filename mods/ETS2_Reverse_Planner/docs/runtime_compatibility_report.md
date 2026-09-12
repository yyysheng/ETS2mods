# Reverse Planner / Reverse Posture Runtime Compatibility

## Outcome

The Reverse Posture Assistant v0.10.9 runtime remains an independent,
unmodified plugin. Reverse Planner no longer clones and installs its four-hook
set. It installs only the vehicle-render-dispatch hook required to submit its
two green tractor-rear traces.

## Failure mechanism corrected

The first Reverse Planner prototype installed the same four native hooks as
the legacy runtime. Disabling the legacy DLL avoided double hooking but also
removed its blue/orange prediction renderer. Loading both copies without a
lifetime policy would introduce a second risk: one plugin could restore a
target prologue or unload code still referenced by the other plugin's
trampoline.

The sidecar runtime now uses this lifecycle:

1. Install one MinHook on `vehicle_render_dispatch`.
2. Preserve and call the trampoline first, so the previously installed
   renderer remains in the chain.
3. Pin the Reverse Planner module with
   `GET_MODULE_HANDLE_EX_FLAG_PIN` after hook installation succeeds.
4. At SCS telemetry shutdown, atomically disable planner rendering and leave a
   cheap pass-through detour resident until process exit.
5. Never call `MH_ALL_HOOKS`, so the planner cannot operate on hooks owned by
   another runtime.

The module is pinned only after a successful hook. Any failure before that
point rolls back the planner hook and leaves telemetry-only behavior.

## Offline verification performed

`tools/TestRuntimeCompatibility.ps1` performs two complementary suites.

### Native hook-chain harness

Two separate DLLs, each with its own statically linked MinHook copy, hook the
same exported x64 function. The harness verifies:

| Load order | Both active | Second dormant | Both dormant |
| --- | ---: | ---: | ---: |
| Legacy then Planner | 1112 | 112 | 12 |
| Planner then Legacy | 1112 | 1012 | 12 |

Both orders passed. Each hook was invoked exactly once while both were active.

### Actual telemetry DLL harness

The real `ETS2ReverseEntityRuntime.dll` and
`ETS2ReversePlannerRuntime.dll` are loaded against a mock SCS Telemetry 1.01
host. Both load orders are crossed with forward and reverse shutdown order.
All four combinations passed, registering 394 channels and eight event
callbacks per run. The non-game executable intentionally causes both native
backends to fail closed into telemetry-only mode, so this suite validates DLL
coexistence and telemetry lifecycle but does not claim an ETS2 render result.

Static assertions in the script also verify that the legacy source retains its
original hook sites while the generated planner source contains exactly one
`MH_CreateHook` call and the module-pin/pass-through safeguards.

Stage 0 and Stage 1A regression executables pass after this change, including
obstacle contours, tractor/trailer self-contour rejection, and the
tractor-only twin rear-track invariant. The green test asset uses the separate
`/model/reverse_planer/` namespace and a unique automat material path, so it
does not override the legacy `/model/reverse_assist/` resources.

## Remaining boundary

Offline tests establish compatible chaining and lifecycle behavior, not final
render correctness inside ETS2 1.60.1.7. The next step is a controlled game
test with both DLLs enabled. Acceptance requires both the legacy blue/orange
trajectory and the new green twin tractor-rear traces to appear in Reverse,
with no crash or hook-installation error during startup and shutdown.

The first controlled game test exposed two separate pre-render failures. The
legacy runtime had already patched the enabled-hook prologues, so the planner's
copied compatibility check rejected the otherwise exact executable and stayed
in telemetry-only mode. The planner now accepts only the exact immutable
on-disk executable SHA-256 in that situation; unknown executables still need
the complete original signature fallback. The green test package also used an
invalid long manifest unit name and was rejected by the SII parser. Its unit is
now the conventional `.package_name`. Neither correction uses or depends on
ReShade.

The fallback signature reader now also reads the immutable PE file on disk,
instead of the process image whose render-dispatch prologue may already have
been patched by the legacy sidecar. The fallback gates only the seven functions
the planner actually calls or hooks; unrelated legacy trailer hooks no longer
reject it. Therefore an ETS2 hotfix that changes only the executable hash,
while preserving those verified RVAs and signatures, can still use the planner
in either DLL load order. A build that moves or changes any required function
continues to fail closed; it requires a newly verified profile rather than an
unsafe nearest-signature guess. Runtime state is found
through Windows' redirected Documents known-folder API, with `USERPROFILE` as
fallback, so reboot and Documents-folder redirection do not change the seed
location.

## Dynamic planner correction

The first game feedback also identified a static-plan/reference-frame defect.
The runtime now replans the complete remaining route every 100 ms while Reverse
is active and forces an immediate replan on each Reverse re-entry; leaving
Reverse clears the published frames. The live first sample is overwritten with
the measured rear-axle/hitch pose, guaranteeing that the green and legacy blue
origins coincide.

Geometry is reconstructed from the same wheel and hook telemetry calibration as
the legacy runtime: effective tractor axles, wheelbase, body extents, hook
offset, trailer equivalent axle, axle-to-hitch distance, rear overhang and
width. This removes the former fixed truck/trailer length and telemetry-origin
assumptions. Only the tractor rear twin traces are published, as required; no
ReShade Add-on path is involved.

## Bounded parking terminal region

The parking target is no longer treated as one exact mathematical pose after
the strict target fails. The planner checks a fixed, ordered set of at most
12 nearby terminal poses: -0.30 to +0.70 m along the parking-frame axis, up to 0.35 m
lateral relief, and up to 6 degrees heading relief toward the live trailer.
Each checkpoint is limited to a 4-by-4 cubic search and one physical shooting
seed, with immediate return on success. The wider display-only no-X corridor
was independently tightened to 6 m (or 0.60 trailer length), 1.0 m lateral,
15 degrees frame heading and 25 degrees articulation.

The frozen mid-route live pose that previously changed from a valid green
route to X now passes the first half-heading-relief checkpoint in deterministic
replay. Stage 0, all Stage 1A cases, both hook orders, and all four real-DLL
load/shutdown combinations pass. The deployed planner DLL SHA-256 is
`0F3721034BA64DAD88E40B95A4775C0B3E748498D2101494E508E5BF53D4A492`.

## Steering-independent route planning

Measured steering is completely excluded from the full-route request at every
speed and remains only as the control error used by the cab-side arrows. The
temporary `truck.speed` subscription used by the superseded low-speed branch
has therefore been removed. Arrow filtering uses a 1-degree straight deadband,
smoothly reaches the full side indication at 36 degrees, and saturates there.
The tractor-to-trailer articulation hard limit is 70 degrees; values strictly
above it still fail closed.

Zero-solution planning inputs are cached while tractor, trailer, target and
key trailer geometry remain within tight equivalence tolerances. The one-second
X timer continues without rerunning the worst-case search. Cab-arrow frames are
stored as a replaceable suffix and refreshed at every telemetry frame using
the latest cab pose and steering error, independently of the 100 ms full-route
planning cadence.
