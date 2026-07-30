# Retired 6g rendering prototype

Only `reverse_kinematics.hpp` and its offline tests remain authoritative.

`world_runtime_probe.cpp` and `ETS2ReverseGroundGuideProbe.vcxproj` are retained solely as rejected
prototype evidence. They must not be packaged, installed, or used by the replacement. The active
architecture is `../entity_runtime`: official telemetry input plus normal collisionless Prism3D
world entities, with no camera or rendering-data access.
