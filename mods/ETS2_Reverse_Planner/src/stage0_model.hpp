#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stage0
{
constexpr double pi = 3.14159265358979323846;
constexpr double stop_speed_epsilon_mps = 0.05;
constexpr double local_scan_radius_m = 50.0;

struct Vec2 { double x{}; double y{}; };
struct Pose2 { Vec2 position{}; double heading_rad{}; };

inline double wrap_angle(double value)
{
    while (value > pi) value -= 2.0 * pi;
    while (value <= -pi) value += 2.0 * pi;
    return value;
}

inline Vec2 world_to_local(const Vec2 &world, const Pose2 &frame)
{
    const double dx = world.x - frame.position.x;
    const double dz = world.y - frame.position.y;
    const double c = std::cos(frame.heading_rad);
    const double s = std::sin(frame.heading_rad);
    // ETS2's horizontal world axes are handed opposite to the planner frame:
    // planner +X is vehicle-right and planner +Y is vehicle-front.
    return {-c * dx + s * dz, s * dx + c * dz};
}

inline Vec2 local_to_world(const Vec2 &local, const Pose2 &frame)
{
    const double c = std::cos(frame.heading_rad);
    const double s = std::sin(frame.heading_rad);
    return {frame.position.x - c * local.x + s * local.y,
            frame.position.y + s * local.x + c * local.y};
}

inline double world_heading_to_local(double world_heading, const Pose2 &frame)
{
    // world_to_local contains a handedness reflection, so the relative angle
    // changes sign compared with an ordinary rotation-only transform.
    return wrap_angle(frame.heading_rad-world_heading);
}

inline bool activation_valid(double speed_mps, int displayed_gear)
{
    return std::abs(speed_mps) <= stop_speed_epsilon_mps && displayed_gear < 0;
}

struct Bounds2
{
    double width{};
    double length{};
};

struct TrailerTargetGeometry
{
    Vec2 center{};
    Vec2 kingpin{};
    double heading_rad{};
    double coupling_approach_heading_rad{};
    Bounds2 bounds{};
    bool kingpin_is_heuristic{true};
};

inline TrailerTargetGeometry estimate_trailer_target(
    Vec2 center, double heading_rad, Bounds2 bounds,
    double kingpin_from_front_m = 1.6)
{
    // Prism vehicle +Z points forward.  The kingpin is behind the front face;
    // this remains HEURISTIC until model hook geometry or job data proves it.
    const double forward_from_center =
        std::max(0.0, bounds.length * 0.5 - kingpin_from_front_m);
    const Vec2 forward{std::sin(heading_rad), std::cos(heading_rad)};
    return {center,
            {center.x + forward.x * forward_from_center,
             center.y + forward.y * forward_from_center},
            wrap_angle(heading_rad),
            wrap_angle(heading_rad + pi),
            bounds,
            true};
}
}
