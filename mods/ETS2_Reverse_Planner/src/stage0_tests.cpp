#include "stage0_model.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }
}

int main()
{
    using namespace stage0;
    assert(activation_valid(0.0, -1));
    assert(activation_valid(0.049, -2));
    assert(!activation_valid(0.051, -1));
    assert(!activation_valid(0.0, 0));
    assert(!activation_valid(0.0, 5));

    for (double h : {0.0, pi / 2.0, -pi / 2.0, pi, pi - 1e-8})
    {
        Pose2 frame{{1234.5, -678.25}, h};
        Vec2 world{1201.25, -650.75};
        const Vec2 roundtrip = local_to_world(world_to_local(world, frame), frame);
        assert(near(roundtrip.x, world.x, 1e-8));
        assert(near(roundtrip.y, world.y, 1e-8));
    }
    const Pose2 north_facing{{0.0,0.0},0.0};
    const auto right=world_to_local({-2.0,0.0},north_facing);
    const auto behind=world_to_local({0.0,-3.0},north_facing);
    assert(near(right.x,2.0) && near(right.y,0.0));
    assert(near(behind.x,0.0) && near(behind.y,-3.0));
    assert(near(world_heading_to_local(pi/2.0,north_facing),-pi/2.0));

    const auto target = estimate_trailer_target({10.0, 20.0}, 0.0, {2.6, 13.6});
    assert(near(target.kingpin.x, 10.0));
    assert(near(target.kingpin.y, 25.2));
    assert(near(std::abs(wrap_angle(target.coupling_approach_heading_rad -
                                    target.heading_rad)), pi));
    assert(target.kingpin_is_heuristic);

    std::cout << "stage0 tests passed: activation, roundtrip 0/90/-90/180, "
                 "directed kingpin approach\n";
}
