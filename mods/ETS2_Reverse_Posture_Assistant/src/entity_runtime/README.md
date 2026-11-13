# Native entity runtime

This is the replacement for the discarded ReShade/6g drawing prototype.

- Input is limited to official SCS telemetry channels.
- It does not inspect cameras, render passes, depth buffers, constant buffers, or graphics APIs.
- Prediction uses the unchanged `reverse_kinematics.hpp`.
- Output consists of normal Prism3D model entities with world transforms.
- The supplied model assets intentionally have no `.pmc`, so they do not participate in collision.
- Native access targets the exact ETS2 1.62.3.2 Steam x64 executable.
- A matching SHA-256, all four hook signatures and all four model-lifecycle
  helper signatures are required for native hook installation. Other builds
  stay in telemetry-only mode.

The runtime loads:

- `/model/reverse_assist/tractor_box.pmd`
- `/model/reverse_assist/trailer_box.pmd`

Both assets are simple low-profile emissive frame meshes. They are hidden below the world whenever
the game is paused or reverse gear is not selected.
