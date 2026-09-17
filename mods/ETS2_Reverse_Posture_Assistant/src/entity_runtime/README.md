# Native entity runtime

This is the replacement for the discarded ReShade/6g drawing prototype.

- Input is limited to official SCS telemetry channels.
- It does not inspect cameras, render passes, depth buffers, constant buffers, or graphics APIs.
- Prediction uses the unchanged `reverse_kinematics.hpp`.
- Output consists of normal Prism3D model entities with world transforms.
- The supplied model assets intentionally have no `.pmc`, so they do not participate in collision.
- Native access is selected through `BuildProfile` entries. Exact 1.61.1.0 and
  1.60.1.7 Steam public hashes and the byte-compatible 1.60 hook layout are covered.
- Compatibility validation checks only the four hooks compiled as enabled.
  Any required mismatch skips hook installation and retains telemetry-only mode.

The runtime loads:

- `/model/reverse_assist/tractor_box.pmd`
- `/model/reverse_assist/trailer_box.pmd`

Both assets are simple low-profile emissive frame meshes. They are hidden below the world whenever
the game is paused or reverse gear is not selected.
