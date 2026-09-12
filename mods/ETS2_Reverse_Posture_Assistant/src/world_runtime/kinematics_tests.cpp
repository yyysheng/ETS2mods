#include "reverse_kinematics.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>

using namespace reverse_assist;

int main()
{
    const Prediction straight = predict_reverse_boxes(0.0, 3.8, 6.8, 2.5, {});
    assert(!straight.boxes.empty());
    assert(std::abs(straight.boxes.back().pose.x) < 1e-6);
    // The tractor body centre begins 2.4 m ahead of the rear-axle origin,
    // so a 12 m rear-axle prediction ends at about 9.6 m here.
    assert(straight.boxes.back().pose.z > 9.5);

    const Prediction right = predict_reverse_boxes(0.75, 3.8, 6.8, 2.5, {});
    assert(right.boxes.back().pose.x < -0.2);

    const std::vector<TrailerSpec> one_trailer{{0.0, 7.2, 3.0, 2.55}};
    const Prediction articulated = predict_reverse_boxes(-0.65, 3.8, 6.8, 2.5, one_trailer);
    assert(articulated.boxes.size() >= straight.boxes.size() * 2 - 2);
    bool found_trailer = false;
    for (const GroundBox &box : articulated.boxes)
        found_trailer = found_trailer || box.kind == BodyKind::trailer;
    assert(found_trailer);

    const Prediction tight_turn = predict_reverse_boxes(
        1.0, 3.8, 7.0, 2.55, one_trailer,
        0.0, 0.0, -2.4, 5.0, 0.05, 0.25);
    bool have_previous[2]{};
    BoundaryPoint previous_rear_left[2]{};
    BoundaryPoint previous_rear_right[2]{};
    double previous_distance[2]{};
    for (const GroundBox &box : tight_turn.boxes)
    {
        const auto kind_index =
            box.kind == BodyKind::tractor ? 0u : 1u;
        const BoundaryPair boundary =
            reverse_leading_boundary(box, 0.18);
        if (have_previous[kind_index])
        {
            assert(box.prediction_distance >
                   previous_distance[kind_index]);
            assert(std::hypot(
                       boundary.left.x -
                           previous_rear_left[kind_index].x,
                       boundary.left.z -
                           previous_rear_left[kind_index].z) <
                   0.36);
            assert(std::hypot(
                       boundary.right.x -
                           previous_rear_right[kind_index].x,
                       boundary.right.z -
                           previous_rear_right[kind_index].z) <
                   0.36);
        }

        const double longitudinal_x = std::sin(box.pose.heading);
        const double longitudinal_z = std::cos(box.pose.heading);
        const double left_projection =
            (boundary.left.x - box.pose.x) * longitudinal_x +
            (boundary.left.z - box.pose.z) * longitudinal_z;
        const double right_projection =
            (boundary.right.x - box.pose.x) * longitudinal_x +
            (boundary.right.z - box.pose.z) * longitudinal_z;
        // Both curves remain on the reverse-leading rear edge.  This
        // invariant catches the front/rear endpoint switching that used to
        // reverse an orange curve at high articulation.
        assert(std::abs(left_projection - box.length * 0.5) <
               1e-9);
        assert(std::abs(right_projection - box.length * 0.5) <
               1e-9);

        have_previous[kind_index] = true;
        previous_rear_left[kind_index] = boundary.left;
        previous_rear_right[kind_index] = boundary.right;
        previous_distance[kind_index] = box.prediction_distance;
    }
    assert(previous_distance[0] >= 4.99);
    assert(previous_distance[1] >= 4.99);

    const std::vector<TrailerSpec> beyond_limit{{
        100.0 * pi / 180.0, 7.2, 3.0, 2.55}};
    const Prediction limited =
        predict_reverse_boxes(1.0, 3.8, 6.8, 2.5, beyond_limit,
                              0.0, 0.0, -2.4, 5.0, 0.05, 0.25);
    assert(!limited.articulation_limited);
    assert(limited.boxes.size() > 2);
    double limited_distance[2]{};
    bool limited_have_previous[2]{};
    BoundaryPoint limited_previous_left[2]{};
    BoundaryPoint limited_previous_right[2]{};
    double limited_max_step[2]{};
    for (const GroundBox &box : limited.boxes)
    {
        const auto kind_index =
            box.kind == BodyKind::tractor ? 0u : 1u;
        const BoundaryPair boundary =
            reverse_leading_boundary(box, 0.18);
        if (limited_have_previous[kind_index])
        {
            limited_max_step[kind_index] =
                std::max(
                    limited_max_step[kind_index],
                    std::max(
                        std::hypot(
                            boundary.left.x -
                                limited_previous_left[kind_index].x,
                            boundary.left.z -
                                limited_previous_left[kind_index].z),
                        std::hypot(
                            boundary.right.x -
                                limited_previous_right[kind_index].x,
                            boundary.right.z -
                                limited_previous_right[kind_index].z)));
        }
        limited_have_previous[kind_index] = true;
        limited_previous_left[kind_index] = boundary.left;
        limited_previous_right[kind_index] = boundary.right;
        limited_distance[kind_index] =
            std::max(limited_distance[kind_index],
                     box.prediction_distance);
    }
    // Even beyond 90 degrees the continuous hitch-displacement model must
    // keep the requested fixed-distance preview without a hard articulation
    // cutoff or a discontinuous endpoint jump.
    assert(limited_distance[0] >= 4.99);
    assert(limited_distance[1] >= 4.99);
    assert(limited_max_step[0] < 0.36);
    assert(limited_max_step[1] < 0.36);

    const std::vector<WheelSpec> short_tractor_wheels{
        {-1.05, -1.8, true, true},
        { 1.05, -1.8, true, true},
        {-1.05,  2.0, false, true},
        { 1.05,  2.0, false, true}};
    const std::vector<WheelSpec> long_tractor_wheels{
        {-1.10, -2.6, true, true},
        { 1.10, -2.6, true, true},
        {-1.10, -1.4, true, true},
        { 1.10, -1.4, true, true},
        {-1.10,  2.4, false, true},
        { 1.10,  2.4, false, true},
        {-1.10,  3.6, false, true},
        { 1.10,  3.6, false, true},
        // A rear-steering tag axle must not be averaged into the front axle.
        {-1.10,  4.4, true, true},
        { 1.10,  4.4, true, true}};
    const TractorSpec short_tractor =
        estimate_tractor_spec(short_tractor_wheels, 1.5);
    const TractorSpec long_tractor =
        estimate_tractor_spec(long_tractor_wheels, 3.2);
    assert(std::abs(short_tractor.wheelbase - 3.8) < 1e-9);
    assert(std::abs(short_tractor.hook_from_rear + 0.5) < 1e-9);
    assert(long_tractor.wheelbase > short_tractor.wheelbase + 1.0);
    assert(long_tractor.length > short_tractor.length + 1.0);
    assert(long_tractor.width > short_tractor.width);
    const Prediction long_chassis_origin = predict_reverse_boxes(
        0.0,
        long_tractor.wheelbase,
        long_tractor.length,
        long_tractor.width,
        {},
        0.0,
        long_tractor.rear_axle_z,
        long_tractor.center_from_rear,
        5.0,
        0.05,
        0.25,
        long_tractor.hook_from_rear);
    const BoundaryPair long_chassis_start =
        reverse_leading_boundary(long_chassis_origin.boxes.front(), 0.0);
    // The first blue boundary must lie behind the physical rearmost axle,
    // not behind the centroid of the fixed axle group.
    assert(std::abs(long_chassis_start.left.z -
                    (4.4 + 0.90)) < 1e-9);

    const std::vector<WheelSpec> lifted_tag_tractor_wheels{
        {-1.10, -2.0, true,  true, true,  true},
        { 1.10, -2.0, true,  true, true,  true},
        {-1.10,  2.4, false, true, true,  true},
        { 1.10,  2.4, false, true, true,  true},
        {-1.10,  3.6, false, true, false, true},
        { 1.10,  3.6, false, true, false, true}};
    const TractorSpec lifted_tag_tractor =
        estimate_tractor_spec(lifted_tag_tractor_wheels, 2.8);
    assert(std::abs(lifted_tag_tractor.rear_axle_z - 2.4) <
           1e-9);
    assert(std::abs(lifted_tag_tractor.wheelbase - 4.4) <
           1e-9);
    const Prediction lifted_tag_origin = predict_reverse_boxes(
        0.0,
        lifted_tag_tractor.wheelbase,
        lifted_tag_tractor.length,
        lifted_tag_tractor.width,
        {},
        0.0,
        lifted_tag_tractor.rear_axle_z,
        lifted_tag_tractor.center_from_rear,
        5.0,
        0.05,
        0.25,
        lifted_tag_tractor.hook_from_rear);
    const BoundaryPair lifted_tag_start =
        reverse_leading_boundary(
            lifted_tag_origin.boxes.front(), 0.0);
    // The raised tag no longer moves the virtual pivot, while the body
    // envelope still covers its physical location.
    assert(std::abs(lifted_tag_start.left.z -
                    (3.6 + 0.90)) < 1e-9);

    const std::vector<WheelSpec> short_trailer_wheels{
        {-1.05, 1.8, false, false},
        { 1.05, 1.8, false, false},
        {-1.05, 2.8, false, false},
        { 1.05, 2.8, false, false}};
    const std::vector<WheelSpec> long_trailer_wheels{
        {-1.25, 2.8, false, false},
        { 1.25, 2.8, false, false},
        {-1.25, 4.0, false, false},
        { 1.25, 4.0, false, false},
        {-1.25, 5.2, false, false},
        { 1.25, 5.2, false, false}};
    const TrailerSpec short_trailer =
        estimate_trailer_spec(0.0, -3.0, short_trailer_wheels);
    const TrailerSpec long_trailer =
        estimate_trailer_spec(0.0, -6.0, long_trailer_wheels);
    assert(long_trailer.axle_to_hitch >
           short_trailer.axle_to_hitch + 3.0);
    assert(long_trailer.rear_overhang >
           short_trailer.rear_overhang);
    assert(long_trailer.width > short_trailer.width);
    assert(std::abs(short_trailer.rear_overhang - 1.45) < 1e-9);

    const std::vector<WheelSpec> lifted_trailer_wheels{
        {-1.25, 4.0, false, true, true,  true},
        { 1.25, 4.0, false, true, true,  true},
        {-1.25, 5.2, false, true, false, true},
        { 1.25, 5.2, false, true, false, true}};
    const TrailerSpec lifted_trailer =
        estimate_trailer_spec(
            0.0, -5.0, lifted_trailer_wheels);
    assert(std::abs(lifted_trailer.axle_to_hitch - 9.0) <
           1e-9);
    assert(std::abs(lifted_trailer.rear_overhang - 2.15) <
           1e-9);

    const std::vector<WheelSpec>
        lifted_steering_trailer_wheels{
        {-1.25, 4.0, false, true, true,  true, 0.0, true},
        { 1.25, 4.0, false, true, true,  true, 0.0, true},
        {-1.25, 5.2, true,  true, false, true,
         18.0 * pi / 180.0, true},
        { 1.25, 5.2, true,  true, false, true,
         18.0 * pi / 180.0, true}};
    const TrailerSpec lifted_steering_trailer =
        estimate_trailer_spec(
            0.0, -5.0,
            lifted_steering_trailer_wheels);
    // A lifted self-steering tag contributes to the physical tail envelope,
    // but neither moves the current virtual support axle nor steers it.
    assert(std::abs(
               lifted_steering_trailer.axle_to_hitch - 9.0) <
           1e-9);
    assert(std::abs(
               lifted_steering_trailer.axle_steering) < 1e-9);

    const double trailer_steering = 8.0 * pi / 180.0;
    const std::vector<WheelSpec> steerable_trailer_wheels{
        {-1.25, 4.0, true, true, true, true,
         trailer_steering, true},
        { 1.25, 4.0, true, true, true, true,
         trailer_steering, true},
        {-1.25, 5.0, true, true, true, true,
         trailer_steering, true},
        { 1.25, 5.0, true, true, true, true,
         trailer_steering, true}};
    const TrailerSpec steerable_trailer =
        estimate_trailer_spec(
            15.0 * pi / 180.0, -4.0,
            steerable_trailer_wheels);
    assert(std::abs(
               steerable_trailer.axle_steering -
               trailer_steering) < 1e-9);
    TrailerSpec passive_trailer = steerable_trailer;
    passive_trailer.axle_steering = 0.0;
    const Prediction passive_prediction =
        predict_reverse_boxes(
            0.6, 3.8, 7.0, 2.55,
            {passive_trailer},
            0.0, 0.0, -2.4,
            5.0, 0.05, 0.25);
    const Prediction steering_prediction =
        predict_reverse_boxes(
            0.6, 3.8, 7.0, 2.55,
            {steerable_trailer},
            0.0, 0.0, -2.4,
            5.0, 0.05, 0.25);
    const GroundBox *passive_last = nullptr;
    const GroundBox *steering_last = nullptr;
    for (const GroundBox &box : passive_prediction.boxes)
        if (box.kind == BodyKind::trailer)
            passive_last = &box;
    for (const GroundBox &box : steering_prediction.boxes)
        if (box.kind == BodyKind::trailer)
            steering_last = &box;
    assert(passive_last && steering_last);
    assert(std::abs(normalize_angle(
               passive_last->pose.heading -
               steering_last->pose.heading)) >
           0.01);

    // Exercise the continuous reverse solver across the large articulation
    // range where a fixed 0.9 m render bar previously crossed neighbouring
    // 0.25 m samples and looked like a folded trajectory.
    for (const double articulation_degrees :
         {-120.0, -90.0, -60.0, 60.0, 90.0, 120.0})
    {
        TrailerSpec stress_trailer = steerable_trailer;
        stress_trailer.heading =
            articulation_degrees * pi / 180.0;
        const Prediction stress = predict_reverse_boxes(
            1.0, 3.8, 7.0, 2.55,
            {stress_trailer},
            0.0, 0.0, -2.4,
            5.0, 0.05, 0.25);
        double stress_distance[2]{};
        bool stress_have_previous[2]{};
        BoundaryPoint stress_previous_left[2]{};
        BoundaryPoint stress_previous_right[2]{};
        for (const GroundBox &box : stress.boxes)
        {
            const auto kind_index =
                box.kind == BodyKind::tractor ? 0u : 1u;
            const BoundaryPair boundary =
                reverse_leading_boundary(box, 0.18);
            if (stress_have_previous[kind_index])
            {
                assert(std::hypot(
                           boundary.left.x -
                               stress_previous_left[kind_index].x,
                           boundary.left.z -
                               stress_previous_left[kind_index].z) <
                       0.36);
                assert(std::hypot(
                           boundary.right.x -
                               stress_previous_right[kind_index].x,
                           boundary.right.z -
                               stress_previous_right[kind_index].z) <
                       0.36);
            }
            stress_have_previous[kind_index] = true;
            stress_previous_left[kind_index] = boundary.left;
            stress_previous_right[kind_index] = boundary.right;
            stress_distance[kind_index] =
                std::max(stress_distance[kind_index],
                         box.prediction_distance);
        }
        assert(!stress.articulation_limited);
        assert(stress_distance[0] >= 4.99);
        assert(stress_distance[1] >= 4.99);
    }

    assert(std::abs(body_plane_height(4.0, 5.0, 0.0, 0.0)) <
           1e-12);
    assert(body_plane_height(0.0, 5.0, 5.0 * pi / 180.0, 0.0) <
           0.0);
    assert(body_plane_height(2.0, 0.0, 0.0, 5.0 * pi / 180.0) >
           0.0);

    // The road plane must follow grounded tyre centres, not chassis attitude.
    // A 5-degree body pitch cancelled by unequal front/rear suspension is a
    // level road and must produce a level guide.
    const double suspension_pitch = 5.0 * pi / 180.0;
    std::vector<WheelSpec> level_suspension_wheels;
    for (const double z : {-2.0, 2.0})
        for (const double x : {-1.0, 1.0})
        {
            WheelSpec wheel;
            wheel.x = x;
            wheel.z = z;
            wheel.on_ground = true;
            wheel.on_ground_known = true;
            wheel.y = 0.50;
            wheel.radius = 0.50;
            wheel.radius_known = true;
            wheel.suspension_deflection =
                z * std::tan(suspension_pitch);
            wheel.suspension_deflection_known = true;
            level_suspension_wheels.push_back(wheel);
        }
    const auto level_tyre_plane = estimate_ground_plane(
        level_suspension_wheels, suspension_pitch, 0.0);
    assert(level_tyre_plane.wheel_fitted);
    assert(std::abs(level_tyre_plane.slope_right) < 1e-10);
    assert(std::abs(level_tyre_plane.slope_rearward) < 1e-10);

    // The same estimator retains genuine grade after removing body/suspension
    // separation. A lifted non-contact wheel must not contaminate the fit.
    auto graded_wheels = level_suspension_wheels;
    constexpr double real_grade = 0.08;
    for (auto &wheel : graded_wheels)
        wheel.suspension_deflection += real_grade * wheel.z;
    WheelSpec lifted_outlier = graded_wheels.front();
    lifted_outlier.z = 7.0;
    lifted_outlier.suspension_deflection = 8.0;
    lifted_outlier.on_ground = false;
    graded_wheels.push_back(lifted_outlier);
    const auto graded_tyre_plane = estimate_ground_plane(
        graded_wheels, suspension_pitch, 0.0);
    assert(std::abs(graded_tyre_plane.slope_rearward - real_grade) <
           1e-10);

    // ETS2 changes lift masks one tyre at a time. Intermediate masks are held
    // until the final support topology has remained stable for 400 ms.
    StableWheelTopologyFilter<TrailerSpec> topology_filter;
    WheelTopologySnapshot<TrailerSpec> eight_wheels;
    eight_wheels.grounded_mask = 0xff;
    eight_wheels.geometry.axle_to_hitch = 8.57329;
    assert(topology_filter.update(eight_wheels, 0));
    auto four_wheels = eight_wheels;
    four_wheels.grounded_mask = 0x3c;
    four_wheels.geometry.axle_to_hitch = 8.57861;
    assert(topology_filter.update(four_wheels, 100));
    assert(topology_filter.update(four_wheels, 499));
    assert(topology_filter.stable().grounded_mask == 0xff);
    assert(topology_filter.update(four_wheels, 500));
    assert(topology_filter.stable().grounded_mask == 0x3c);

    // A measured front-wheel angle supersedes the generic 38-degree steering
    // map, and a lateral fifth-wheel offset participates in rigid hitch motion.
    const Prediction measured_steer = predict_reverse_boxes(
        0.0, 3.8, 7.0, 2.55, {}, 0.0, 0.0, -2.4,
        5.0, 0.05, 0.25, 0.0, 0.0, 12.0 * pi / 180.0);
    const Prediction zero_steer = predict_reverse_boxes(
        0.0, 3.8, 7.0, 2.55, {}, 0.0, 0.0, -2.4,
        5.0, 0.05, 0.25);
    assert(std::abs(normalize_angle(
               measured_steer.boxes.back().pose.heading -
               zero_steer.boxes.back().pose.heading)) > 0.1);

    const TrailerSpec offset_test_trailer{
        18.0 * pi / 180.0, 7.8, 2.5, 2.55, 0.0};
    const Prediction centred_hitch = predict_reverse_boxes(
        0.45, 3.8, 7.0, 2.55, {offset_test_trailer},
        0.0, 0.0, -2.4, 5.0, 0.05, 0.25, -0.8, 0.0);
    const Prediction lateral_hitch = predict_reverse_boxes(
        0.45, 3.8, 7.0, 2.55, {offset_test_trailer},
        0.0, 0.0, -2.4, 5.0, 0.05, 0.25, -0.8, 0.25);
    assert(std::abs(normalize_angle(
               centred_hitch.boxes.back().pose.heading -
               lateral_hitch.boxes.back().pose.heading)) > 1e-4);

    std::cout << "reverse kinematics tests passed: "
              << articulated.boxes.size()
              << " boxes, limited max steps "
              << limited_max_step[0] << ", "
              << limited_max_step[1]
              << ", adaptive wheelbases "
              << short_tractor.wheelbase << ", "
              << long_tractor.wheelbase << "\n";
    return 0;
}
