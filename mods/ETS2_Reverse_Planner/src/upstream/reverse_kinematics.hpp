#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace reverse_assist
{
constexpr double pi = 3.14159265358979323846;

struct Pose
{
    double x = 0.0;
    double z = 0.0;
    double heading = 0.0;
};

struct TrailerSpec
{
    double heading = 0.0;
    double axle_to_hitch = 7.2;
    double rear_overhang = 3.0;
    double width = 2.55;
    double axle_steering = 0.0;
};

struct WheelSpec
{
    double x = 0.0;
    double z = 0.0;
    bool steerable = false;
    bool steerable_known = false;
    bool on_ground = true;
    bool on_ground_known = false;
    double steering_angle = 0.0;
    bool steering_angle_known = false;
};

struct TractorSpec
{
    double wheelbase = 3.8;
    double length = 7.0;
    double width = 2.55;
    double center_from_rear = -2.4;
    double rear_axle_z = 0.0;
    double hook_from_rear = 0.0;
};

inline TractorSpec estimate_tractor_spec(
    const std::vector<WheelSpec> &wheels,
    double hook_z = std::numeric_limits<double>::quiet_NaN())
{
    TractorSpec result;
    if (wheels.size() < 2) return result;

    double min_x = wheels.front().x;
    double max_x = wheels.front().x;
    double min_z = wheels.front().z;
    double max_z = wheels.front().z;
    double first_nonsteer_z =
        std::numeric_limits<double>::infinity();
    double steer_z_sum = 0.0;
    double rear_z_sum = 0.0;
    std::size_t steer_count = 0;
    std::size_t rear_count = 0;
    const bool have_grounded_wheel =
        std::any_of(wheels.begin(), wheels.end(),
                    [](const WheelSpec &wheel) {
                        return wheel.on_ground_known &&
                            wheel.on_ground;
                    });
    const auto is_active = [have_grounded_wheel](
                               const WheelSpec &wheel) {
        if (!have_grounded_wheel) return true;
        return !wheel.on_ground_known || wheel.on_ground;
    };
    for (const auto &wheel : wheels)
    {
        min_x = std::min(min_x, wheel.x);
        max_x = std::max(max_x, wheel.x);
        min_z = std::min(min_z, wheel.z);
        max_z = std::max(max_z, wheel.z);
        if (!is_active(wheel)) continue;
        if (wheel.steerable_known && !wheel.steerable)
            first_nonsteer_z =
                std::min(first_nonsteer_z, wheel.z);
    }
    for (const auto &wheel : wheels)
    {
        if (!is_active(wheel)) continue;
        // Multi-steer tractors may have two front steering axles, while some
        // chassis also steer a rearmost tag axle.  Only steering axles ahead
        // of the first fixed axle define the bicycle-model front axle.
        if (wheel.steerable_known && wheel.steerable &&
            (!std::isfinite(first_nonsteer_z) ||
             wheel.z < first_nonsteer_z - 0.20))
        {
            steer_z_sum += wheel.z;
            ++steer_count;
        }
        else if (wheel.steerable_known)
        {
            // A steering tag axle behind the first fixed axle still belongs
            // to the rear support group.  Its lift state determines whether
            // it contributes to the current virtual rear axle.
            rear_z_sum += wheel.z;
            ++rear_count;
        }
    }

    double front_axle_z = min_z;
    double effective_rear_axle_z =
        -std::numeric_limits<double>::infinity();
    if (steer_count > 0)
        front_axle_z = steer_z_sum /
            static_cast<double>(steer_count);
    if (rear_count > 0)
        effective_rear_axle_z = rear_z_sum /
            static_cast<double>(rear_count);
    else
    {
        for (const auto &wheel : wheels)
            if (is_active(wheel))
                effective_rear_axle_z =
                    std::max(effective_rear_axle_z, wheel.z);
    }
    if (!std::isfinite(effective_rear_axle_z))
        effective_rear_axle_z = max_z;

    result.rear_axle_z = effective_rear_axle_z;
    result.wheelbase = std::clamp(
        std::abs(effective_rear_axle_z - front_axle_z),
        2.4, 6.8);
    result.width = std::clamp(max_x - min_x + 0.45, 2.35, 2.75);

    // ETS2 does not expose the body bounding box.  Use wheel-derived axle
    // geometry plus conservative bumper overhangs.  Body bounds must use the
    // foremost and rearmost physical axles rather than the bicycle-model
    // axle centroids: otherwise an 8x4 chassis loses most of its rear bogie
    // and its swept-area curve starts inside the truck.
    constexpr double front_bumper_from_front_axle = 1.45;
    constexpr double rear_bumper_from_rear_axle = 0.90;
    const double body_front_z =
        min_z - front_bumper_from_front_axle;
    const double body_rear_z =
        max_z + rear_bumper_from_rear_axle;
    result.length = std::clamp(
        body_rear_z - body_front_z, 5.0, 10.5);
    result.center_from_rear =
        (body_front_z + body_rear_z) * 0.5 -
        effective_rear_axle_z;
    if (std::isfinite(hook_z))
        result.hook_from_rear = std::clamp(
            hook_z - effective_rear_axle_z, -1.5, 2.5);
    return result;
}

inline TrailerSpec estimate_trailer_spec(
    double heading,
    double hook_z,
    const std::vector<WheelSpec> &wheels)
{
    TrailerSpec result;
    result.heading = heading;
    if (wheels.empty()) return result;

    double min_x = wheels.front().x;
    double max_x = wheels.front().x;
    double min_z = wheels.front().z;
    double max_z = wheels.front().z;
    double equivalent_axle_z_sum = 0.0;
    double steering_tangent_sum = 0.0;
    std::size_t active_count = 0;
    const bool have_grounded_wheel =
        std::any_of(wheels.begin(), wheels.end(),
                    [](const WheelSpec &wheel) {
                        return wheel.on_ground_known &&
                            wheel.on_ground;
                    });
    const auto is_active = [have_grounded_wheel](
                               const WheelSpec &wheel) {
        if (!have_grounded_wheel) return true;
        return !wheel.on_ground_known || wheel.on_ground;
    };
    for (const auto &wheel : wheels)
    {
        min_x = std::min(min_x, wheel.x);
        max_x = std::max(max_x, wheel.x);
        min_z = std::min(min_z, wheel.z);
        max_z = std::max(max_z, wheel.z);
        if (!is_active(wheel)) continue;

        // The low-speed no-side-slip constraints of a multi-axle group can
        // be reduced to one equivalent axle by averaging their yaw-moment
        // arms and steering tangents.  The x*tan(delta) term also preserves
        // the first-order Ackermann effect when left/right wheel angles
        // differ.  Unknown steering falls back to a passive axle.
        const double steering_tangent =
            wheel.steering_angle_known
            ? std::tan(std::clamp(
                  wheel.steering_angle,
                  -60.0 * pi / 180.0,
                  60.0 * pi / 180.0))
            : 0.0;
        equivalent_axle_z_sum +=
            wheel.z + wheel.x * steering_tangent;
        steering_tangent_sum += steering_tangent;
        ++active_count;
    }
    if (active_count == 0) return result;
    const double effective_axle_z =
        equivalent_axle_z_sum /
        static_cast<double>(active_count);
    result.axle_to_hitch = std::clamp(
        std::abs(effective_axle_z - hook_z), 2.5, 15.0);
    // The configured wheel positions already describe the complete axle
    // group.  Measure the tail from its centroid to the rearmost axle and
    // add only the bumper allowance.  Scaling it from hitch distance made a
    // one-axle short trailer look like it had a two-metre rear overhang.
    constexpr double rear_bumper_from_rearmost_axle = 0.95;
    result.rear_overhang = std::clamp(
        (max_z - effective_axle_z) +
            rear_bumper_from_rearmost_axle,
        0.80, 4.0);
    result.width = std::clamp(max_x - min_x + 0.45, 2.35, 3.2);
    result.axle_steering = std::atan(
        steering_tangent_sum /
        static_cast<double>(active_count));
    return result;
}

enum class BodyKind
{
    tractor,
    trailer
};

struct GroundBox
{
    Pose pose;
    double length = 0.0;
    double width = 0.0;
    double opacity = 0.0;
    double prediction_distance = 0.0;
    BodyKind kind = BodyKind::tractor;
    std::size_t trailer_index = 0;
};

struct BoundaryPoint
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BoundaryPair
{
    BoundaryPoint left;
    BoundaryPoint right;
};

inline BoundaryPair reverse_leading_boundary(
    const GroundBox &body,
    double clearance = 0.18)
{
    // During reverse motion, +heading is the leading direction in this
    // predictor.  Both outline points therefore stay on the physical rear
    // edge for the entire horizon.  Never selecting the front corners is
    // important: support-point switching at high articulation makes a
    // boundary turn around even while the body motion remains continuous.
    const double half_length = body.length * 0.5;
    const double half_width =
        body.width * 0.5 + std::max(clearance, 0.0);
    const double rear_x =
        body.pose.x + std::sin(body.pose.heading) * half_length;
    const double rear_z =
        body.pose.z + std::cos(body.pose.heading) * half_length;
    const double lateral_x =
        std::cos(body.pose.heading) * half_width;
    const double lateral_z =
        -std::sin(body.pose.heading) * half_width;

    return {
        {rear_x - lateral_x, 0.0, rear_z - lateral_z},
        {rear_x + lateral_x, 0.0, rear_z + lateral_z}
    };
}

inline double body_plane_height(double local_x,
                                double local_z,
                                double pitch,
                                double roll)
{
    constexpr double maximum_slope = 35.0 * pi / 180.0;
    pitch = std::clamp(pitch, -maximum_slope, maximum_slope);
    roll = std::clamp(roll, -maximum_slope, maximum_slope);
    // Local +X is right and local +Z is backwards.  Positive pitch raises
    // the forward end, while positive roll raises the right side.
    return local_x * std::tan(roll) -
           local_z * std::tan(pitch);
}

struct Prediction
{
    std::vector<GroundBox> boxes;
    bool articulation_limited = false;
};

inline double normalize_angle(double angle)
{
    while (angle > pi) angle -= 2.0 * pi;
    while (angle < -pi) angle += 2.0 * pi;
    return angle;
}

inline Pose trailer_axle_from_hitch(const Pose &hitch, double trailer_heading, double axle_to_hitch)
{
    return {
        hitch.x + std::sin(trailer_heading) * axle_to_hitch,
        hitch.z + std::cos(trailer_heading) * axle_to_hitch,
        trailer_heading
    };
}

inline double trailer_heading_from_hitch_displacement(
    double previous_heading,
    const Pose &previous_hitch,
    const Pose &next_hitch,
    double axle_to_hitch,
    double axle_steering = 0.0)
{
    axle_to_hitch = std::max(axle_to_hitch, 0.5);
    axle_steering = std::clamp(
        axle_steering, -60.0 * pi / 180.0,
        60.0 * pi / 180.0);
    const double hitch_dx = next_hitch.x - previous_hitch.x;
    const double hitch_dz = next_hitch.z - previous_hitch.z;
    const double denominator =
        axle_to_hitch * std::max(std::cos(axle_steering), 0.25);

    // Solve the rolling/no-side-slip constraint at the axle with a midpoint
    // iteration.  Driving the update from the hitch's actual displacement
    // automatically includes an off-axle fifth wheel and remains continuous
    // through large articulation angles.  A steered trailer axle rolls in
    // body-heading + axle-steering direction.
    double estimate = previous_heading;
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const double midpoint_heading = normalize_angle(
            previous_heading +
            normalize_angle(estimate - previous_heading) * 0.5);
        const double rolling_heading =
            midpoint_heading + axle_steering;
        const double normal_x = std::cos(rolling_heading);
        const double normal_z = -std::sin(rolling_heading);
        const double heading_delta =
            -(hitch_dx * normal_x + hitch_dz * normal_z) /
            denominator;
        estimate = normalize_angle(
            previous_heading + heading_delta);
    }
    return estimate;
}

inline Prediction predict_reverse_boxes(double steering_input,
                                        double wheelbase,
                                        double tractor_length,
                                        double tractor_width,
                                        const std::vector<TrailerSpec> &trailers,
                                        double rear_axle_x = 0.0,
                                        double rear_axle_z = 0.0,
                                        double tractor_center_from_rear = -2.4,
                                        double distance = 12.0,
                                        double integration_step = 0.10,
                                        double box_spacing = 1.0,
                                        double tractor_hitch_from_rear = 0.0)
{
    Prediction result;
    wheelbase = std::clamp(wheelbase, 2.4, 6.8);
    tractor_length = std::clamp(tractor_length, 5.0, 10.5);
    tractor_width = std::clamp(tractor_width, 2.35, 2.75);
    distance = std::clamp(distance, 2.0, 24.0);
    integration_step = std::clamp(integration_step, 0.04, 0.25);
    box_spacing = std::clamp(box_spacing, integration_step, 3.0);

    const double steer_angle = std::clamp(steering_input, -1.0, 1.0) * 38.0 * pi / 180.0;
    const double curvature = std::tan(steer_angle) / wheelbase;

    Pose tractor{rear_axle_x, rear_axle_z, 0.0};
    std::vector<Pose> trailer_axles;
    trailer_axles.reserve(trailers.size());
    Pose hitch = tractor;
    hitch.x += std::sin(tractor.heading) *
        tractor_hitch_from_rear;
    hitch.z += std::cos(tractor.heading) *
        tractor_hitch_from_rear;
    for (const TrailerSpec &trailer : trailers)
    {
        Pose axle = trailer_axle_from_hitch(hitch, trailer.heading, trailer.axle_to_hitch);
        trailer_axles.push_back(axle);
        hitch = axle;
    }

    const auto emit_boxes = [&](double travelled, double opacity) {
        Pose tractor_body = tractor;
        tractor_body.x += std::sin(tractor.heading) * tractor_center_from_rear;
        tractor_body.z += std::cos(tractor.heading) * tractor_center_from_rear;
        result.boxes.push_back({tractor_body, tractor_length, tractor_width, opacity, travelled,
                                BodyKind::tractor, 0});
        for (std::size_t i = 0; i < trailers.size(); ++i)
        {
            Pose body = trailer_axles[i];
            const double body_length = trailers[i].axle_to_hitch + trailers[i].rear_overhang;
            const double center_from_axle = (trailers[i].rear_overhang - trailers[i].axle_to_hitch) * 0.5;
            body.x += std::sin(body.heading) * center_from_axle;
            body.z += std::cos(body.heading) * center_from_axle;
            result.boxes.push_back({body, body_length, trailers[i].width, opacity, travelled,
                                    BodyKind::trailer, i});
        }
    };

    emit_boxes(0.0, 0.42);
    double next_box = box_spacing;
    for (double travelled = integration_step; travelled <= distance + 1e-6; travelled += integration_step)
    {
        const Pose previous_tractor = tractor;
        const double heading_delta =
            -curvature * integration_step;
        const double midpoint_heading =
            previous_tractor.heading + heading_delta * 0.5;
        tractor.x += std::sin(midpoint_heading) *
            integration_step;
        tractor.z += std::cos(midpoint_heading) *
            integration_step;
        tractor.heading = normalize_angle(
            previous_tractor.heading + heading_delta);

        Pose previous_parent = previous_tractor;
        previous_parent.x +=
            std::sin(previous_tractor.heading) *
            tractor_hitch_from_rear;
        previous_parent.z +=
            std::cos(previous_tractor.heading) *
            tractor_hitch_from_rear;
        Pose next_parent = tractor;
        next_parent.x += std::sin(tractor.heading) *
            tractor_hitch_from_rear;
        next_parent.z += std::cos(tractor.heading) *
            tractor_hitch_from_rear;
        for (std::size_t i = 0; i < trailers.size(); ++i)
        {
            Pose &axle = trailer_axles[i];
            const Pose previous_axle = axle;
            const double next_heading =
                trailer_heading_from_hitch_displacement(
                    previous_axle.heading,
                    previous_parent,
                    next_parent,
                    trailers[i].axle_to_hitch,
                    trailers[i].axle_steering);
            axle = trailer_axle_from_hitch(
                next_parent, next_heading,
                trailers[i].axle_to_hitch);
            previous_parent = previous_axle;
            next_parent = axle;
        }

        if (travelled + integration_step * 0.5 >= next_box || travelled + integration_step > distance)
        {
            const double fade = 1.0 - std::clamp(travelled / distance, 0.0, 1.0);
            emit_boxes(travelled, 0.12 + fade * 0.20);
            next_box += box_spacing;
        }
    }

    return result;
}
}
