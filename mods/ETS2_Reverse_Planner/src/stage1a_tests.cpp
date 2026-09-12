#include "stage1a_planner.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
bool near(double a,double b,double epsilon=1e-5)
{
    return std::abs(a-b)<=epsilon;
}

stage1a::PlanRequest request(stage1a::PlannerMode mode,
                             stage0::Pose2 start,stage0::Pose2 target)
{
    stage1a::PlanRequest value;
    value.mode=mode;
    value.tractor_start=start;
    value.subject_start=start;
    value.target=target;
    value.geometry.trailer_axle_to_hitch_m=8.0;
    return value;
}
}

int main()
{
    using namespace stage0;
    using namespace stage1a;

    // W: the route seed has no input parameter and cannot depend on the
    // player's current steering. Steering is consumed later by arrow control.
    static_assert(steering_independent_route_start_curvature_m_inv()==0.0);

    // A: tractor directly faces away from a kingpin behind it.
    const auto a=plan(request(PlannerMode::tractor_to_target_trailer,
                             {{0,0},0},{{0,-12},0}));
    assert(a.found&&a.valid_candidate_count>1);
    assert(a.final_position_error<1e-6&&a.final_heading_error<1e-6);
    assert(a.max_curvature<1e-6);
    assert(a.samples.back().tractor_position.y<-11.99);
    for (std::size_t i=1;i<a.samples.size();++i)
        assert(a.samples[i].distance-a.samples[i-1].distance<=0.100001);

    // B: lateral offset still produces a smooth reverse-only curve.
    const auto b=plan(request(PlannerMode::tractor_to_target_trailer,
                             {{0,0},0},{{3,-15},0}));
    assert(b.found&&b.max_curvature>1e-4);
    assert(b.final_position_error<1e-6&&b.final_heading_error<1e-6);

    // C: attached trailer and bay are collinear.
    auto c_request=request(PlannerMode::trailer_to_parking_target,
                           {{0,8},0},{{0,-16},0});
    c_request.subject_start={{0,0},0};
    const auto c=plan(c_request);
    assert(c.found&&c.max_articulation<1e-5);
    assert(c.final_position_error<1e-6&&c.final_heading_error<1e-6);

    // D: an offset bay produces a curved trailer path and synchronized tractor path.
    auto d_request=c_request;
    d_request.target={{3,-18},0};
    d_request.tractor_start.heading_rad=10.0*pi/180.0;
    const auto d=plan(d_request);
    if (!d.found)
        std::cerr << "D failed: " << d.reason << " startReject="
                  << d.start_state_rejections << " selfReject="
                  << d.self_collision_rejections << "\n";
    assert(d.found&&d.max_articulation>1e-4);
    assert(d.max_articulation<=articulation_hard_limit_rad+1e-9);
    assert(d.initial_tractor_heading_error<=0.75*pi/180.0+1e-9);
    assert(d.initial_tractor_position_error<=0.05+1e-9);
    assert(d.samples.front().tractor_position.y>d.samples.back().tractor_position.y);

    // E: hard validation accepts 70 degrees and rejects rather than clamps
    // articulation above the new limit.
    PathSample illegal{};
    illegal.tractor_heading=70.0*pi/180.0;
    illegal.trailer_heading=0.0;
    std::string hard_reason;
    assert(validate_hard_limits({illegal},hard_reason));
    illegal.tractor_heading=70.01*pi/180.0;
    assert(!validate_hard_limits({illegal},hard_reason));
    assert(hard_reason=="ARTICULATION_LIMIT_EXCEEDED");

    // F: mirrored inputs produce mirrored paths and equal scalar metrics.
    const auto left=plan(request(PlannerMode::tractor_to_target_trailer,
                                {{0,0},0},{{-3,-15},0}));
    const auto right=plan(request(PlannerMode::tractor_to_target_trailer,
                                 {{0,0},0},{{3,-15},0}));
    assert(left.found&&right.found&&left.samples.size()==right.samples.size());
    assert(near(left.path_length,right.path_length,1e-6));
    for (std::size_t i=0;i<left.samples.size();++i)
    {
        assert(near(left.samples[i].tractor_position.x,
                    -right.samples[i].tractor_position.x,2e-5));
        assert(near(left.samples[i].tractor_position.y,
                    right.samples[i].tractor_position.y,2e-5));
    }

    // G: the product exposes only attached-trailer-to-task-frame guidance.
    // Detached tractor-to-kingpin planning remains disabled regardless of
    // whether Stage 0 can identify a nearby native loading-guide owner.
    const auto detached=select_mode(true,0,true,false,1);
    const auto attached=select_mode(true,1,true,true,1);
    assert(detached.mode==PlannerMode::inactive);
    assert(detached.reason=="DETACHED_TRAILER_GUIDANCE_DISABLED");
    assert(attached.mode==PlannerMode::trailer_to_parking_target);
    assert(select_mode(false,0,true,false,1).reason=="PATH_PLANNER_INACTIVE");
    assert(select_mode(true,0,false,false,1).reason==
           "DETACHED_TRAILER_GUIDANCE_DISABLED");
    assert(select_mode(true,1,true,true,2).reason=="PARKING_TARGET_NOT_UNIQUE");

    // Live white-frame replay (2026-09-11): the old cubic-only family rejected
    // all 324 candidates on tractor curvature even though the connected rig
    // was almost straight and the parking entry was only 14.8 m behind it.
    // The bounded physical steering-profile fallback must retain a physical
    // reverse-only solution without relaxing the steering limit.
    auto white_frame=request(PlannerMode::trailer_to_parking_target,
        {{-12375.199,54654.854},-1.383},
        {{-12353.05,54650.44},-1.539380582});
    white_frame.subject_start={{-12367.55,54653.44},-1.388};
    white_frame.geometry.tractor_wheelbase_m=3.81;
    white_frame.geometry.tractor_reference_to_hitch_m=0.14;
    white_frame.geometry.trailer_axle_to_hitch_m=7.92;
    white_frame.geometry.trailer_rear_overhang_m=2.26;
    white_frame.geometry.trailer_length_m=10.18;
    white_frame.geometry.trailer_width_m=2.35;
    const auto white_frame_control=plan_steering_profile_fallback(white_frame);
    if (!white_frame_control.found)
        std::cerr << "white-frame control fallback failed: "
                  << white_frame_control.reason << " attempts="
                  << white_frame_control.candidate_count
                  << " pos=" << white_frame_control.final_position_error
                  << " heading=" << white_frame_control.final_heading_error
                  << " S=" << white_frame_control.selected_d0
                  << " k1=" << white_frame_control.selected_d1
                  << " k2="
                  << white_frame_control.max_required_tractor_curvature
                  << "\n";
    assert(white_frame_control.found);
    const auto white_frame_plan=plan(white_frame);
    if (!white_frame_plan.found)
        std::cerr << "white-frame replay failed: " << white_frame_plan.reason
                  << " candidates=" << white_frame_plan.candidate_count
                  << " curvature=" << white_frame_plan.curvature_rejections
                  << " articulation=" << white_frame_plan.articulation_rejections
                  << " other=" << white_frame_plan.other_rejections
                  << " leastPeak="
                  << white_frame_plan.max_required_tractor_curvature
                  << " failureDistance=" << white_frame_plan.failure_distance
                  << " d0=" << white_frame_plan.selected_d0
                  << " d1=" << white_frame_plan.selected_d1
                  << "\n";
    assert(white_frame_plan.found);
    assert(white_frame_plan.max_articulation<=articulation_hard_limit_rad+1e-9);

    // X: live 2026-09-12 pose. The rig is nearly straight, but the bay is
    // about 27 degrees across its current heading. A route-owned initial
    // steering control must find this visible manoeuvre without consuming the
    // player's steering input.
    auto route_owned_start=request(PlannerMode::trailer_to_parking_target,
        {{-12382.610,54646.265},-2.021},
        {{-12353.05,54650.44},-1.539380582});
    route_owned_start.subject_start={{-12375.34,54649.74},-2.017};
    route_owned_start.geometry=white_frame.geometry;
    route_owned_start.geometry.tractor_reference_to_hitch_m=-0.14;
    PathSample route_owned_live{};
    route_owned_live.trailer_position=route_owned_start.subject_start.position;
    route_owned_live.trailer_heading=route_owned_start.subject_start.heading_rad;
    const Pose2 route_owned_frame{{-12347.959089080,54650.281235035},
                                  -1.539380582};
    const auto neutral_start_plan=plan_parking_terminal_region(
        route_owned_start,route_owned_live,route_owned_frame);
    assert(!neutral_start_plan.found);
    route_owned_start.search_planner_initial_curvature=true;
    route_owned_start.planner_initial_curvature_seed_limit=9;
    const auto route_owned_plan=plan_parking_terminal_region(
        route_owned_start,route_owned_live,route_owned_frame);
    if (!route_owned_plan.found)
        std::cerr << "route-owned initial curvature replay failed: "
                  << route_owned_plan.reason << " candidates="
                  << route_owned_plan.candidate_count << "\n";
    assert(route_owned_plan.found);
    assert(route_owned_plan.max_articulation<=articulation_hard_limit_rad+1e-9);

    // Live mid-route replay (2026-09-12): exact centre-heading alignment at
    // the first-overlap plane failed after a valid route had already guided
    // the trailer halfway in. Projecting onto the game's accepted terminal
    // region must keep this clearly stable approach out of the red-X state.
    auto mid_route=request(PlannerMode::trailer_to_parking_target,
        {{-12366.385,54653.344},-1.348},
        {{-12347.959089080,54650.281235035},-1.539380582});
    mid_route.subject_start={{-12358.81,54651.59},-1.344};
    mid_route.geometry=white_frame.geometry;
    PathSample mid_route_live{};
    mid_route_live.trailer_position=mid_route.subject_start.position;
    mid_route_live.trailer_heading=mid_route.subject_start.heading_rad;
    assert(parking_entry_corridor(mid_route_live,mid_route.target,
                                  mid_route.geometry));
    const auto checkpoints=parking_terminal_checkpoints(
        mid_route_live,mid_route.target,mid_route.geometry);
    const auto frame_forward=forward(mid_route.target.heading_rad);
    const auto frame_right=stage1a::right(mid_route.target.heading_rad);
    const auto nominal_entry=add(mid_route.target.position,multiply(
        frame_forward,mid_route.geometry.trailer_length_m*0.5));
    for (const auto &checkpoint:checkpoints)
    {
        const auto offset=subtract(checkpoint.position,nominal_entry);
        const double longitudinal_offset=dot(offset,frame_forward);
        const double lateral=dot(offset,frame_right);
        const double heading=std::abs(stage0::wrap_angle(
            checkpoint.heading_rad-mid_route.target.heading_rad));
        assert(longitudinal_offset>=-0.30-1e-9 &&
               longitudinal_offset<=0.70+1e-9);
        assert(std::abs(lateral)<=0.35+1e-9);
        assert(heading<=6.0*stage0::pi/180.0+1e-9);
    }
    const auto region_plan=plan_parking_terminal_region(
        mid_route,mid_route_live,mid_route.target);
    if (!region_plan.found)
        std::cerr << "terminal-region replay failed: " << region_plan.reason
                  << " candidates=" << region_plan.candidate_count << "\n";
    assert(region_plan.found);
    assert(region_plan.reason=="PARKING_REGION_PHYSICAL_FOUND");
    assert(region_plan.candidate_count<=200);

    const auto arrow_matches=[](double actual,double expected) {
        return std::abs(stage0::wrap_angle(actual-expected))<1e-9;
    };
    assert(arrow_matches(steering_arrow_heading_rad(0.0,0.0),stage0::pi));
    assert(arrow_matches(steering_arrow_heading_rad(
        0.0,1.0*stage0::pi/180.0),stage0::pi));
    assert(arrow_matches(steering_arrow_heading_rad(
        0.0,18.5*stage0::pi/180.0),3.0*stage0::pi/4.0));
    assert(arrow_matches(steering_arrow_heading_rad(
        0.0,36.0*stage0::pi/180.0),stage0::pi/2.0));
    assert(arrow_matches(steering_arrow_heading_rad(
        0.0,-36.0*stage0::pi/180.0),-stage0::pi/2.0));

    // Historical connected live pose replay: the truck had moved away from a
    // verified K0 while still attached. Convert body centers to the explicitly
    // documented equivalent axle reference before planning.
    constexpr double live_lt=9.0;
    constexpr double live_rear_overhang=3.4;
    constexpr double live_center_from_axle=(live_lt-live_rear_overhang)*0.5;
    const Pose2 live_tractor{{-47889.629,14471.760},0.633};
    const Pose2 live_body{{-47894.125,14466.251},0.702};
    const Pose2 live_k0{{-47899.019,14460.380},0.758};
    const auto live_start_forward=forward(live_body.heading_rad);
    const auto live_goal_forward=forward(live_k0.heading_rad);
    auto live_request=request(PlannerMode::trailer_to_parking_target,
                              live_tractor,live_k0);
    live_request.subject_start={{
        live_body.position.x-live_start_forward.x*live_center_from_axle,
        live_body.position.y-live_start_forward.y*live_center_from_axle},
        live_body.heading_rad};
    live_request.target.position={
        live_k0.position.x-live_goal_forward.x*live_center_from_axle,
        live_k0.position.y-live_goal_forward.y*live_center_from_axle};
    live_request.geometry.trailer_axle_to_hitch_m=live_lt;
    const auto live_hitch=add(live_request.subject_start.position,
        multiply(forward(live_request.subject_start.heading_rad),live_lt));
    const auto live_hitch_delta=subtract(
        live_hitch,live_request.tractor_start.position);
    live_request.geometry.tractor_reference_to_hitch_m=dot(
        live_hitch_delta,forward(live_request.tractor_start.heading_rad));
    live_request.geometry.tractor_reference_to_hitch_lateral_m=dot(
        live_hitch_delta,stage1a::right(
            live_request.tractor_start.heading_rad));
    const auto live_replay=plan(live_request);
    if (!live_replay.found)
        std::cerr << "historical replay failed: start="
                  << live_replay.start_state_rejections
                  << " curve=" << live_replay.curvature_rejections
                  << " articulation=" << live_replay.articulation_rejections
                  << " other=" << live_replay.other_rejections << "\n";
    assert(live_replay.found);
    assert(live_replay.initial_tractor_heading_error<=0.75*pi/180.0+1e-9);
    assert(live_replay.initial_tractor_position_error<=0.05+1e-9);
    assert(live_replay.max_articulation<=articulation_hard_limit_rad);
    assert(live_replay.final_position_error<1e-6);

    auto non_finite=request(PlannerMode::tractor_to_target_trailer,
                            {{0,0},0},{{0,-10},0});
    non_finite.target.position.x=std::numeric_limits<double>::quiet_NaN();
    assert(plan(non_finite).reason=="NON_FINITE_INPUT_POSE");

    // H: a contour across the only straight reverse corridor invalidates the
    // path; moving the same obstacle outside the swept contour restores it.
    auto blocked=request(PlannerMode::tractor_to_target_trailer,
                         {{0,0},0},{{0,-12},0});
    blocked.obstacles.push_back({{0,-6},0,3.0,2.0,"BLOCKING_BOX"});
    const auto blocked_result=plan(blocked);
    assert(!blocked_result.found);
    assert(blocked_result.reason=="NO_CANDIDATE_PASSED_ALL_CONSTRAINTS");
    assert(blocked_result.obstacle_collision_rejections>0);
    assert(blocked_result.obstacle_collision_rejections+
           blocked_result.self_collision_rejections+
           blocked_result.start_state_rejections+
           blocked_result.curvature_rejections+
           blocked_result.articulation_rejections+
           blocked_result.forward_motion_rejections+
           blocked_result.other_rejections==blocked_result.candidate_count);
    blocked.obstacles.front().center.x=8.0;
    const auto cleared_result=plan(blocked);
    assert(cleared_result.found);
    for (const auto &sample:cleared_result.samples)
        assert(!oriented_boxes_overlap(
            tractor_contour(sample,cleared_result.mode==PlannerMode::inactive
                ?VehicleGeometry{}:blocked.geometry,false),blocked.obstacles.front(),
            blocked.geometry.contour_clearance_m));

    // I: a deliberately impossible cab/fifth-wheel clearance causes the
    // attached rig's own contours to overlap. It must be rejected, not hidden
    // by clamping articulation or deleting samples.
    auto self_blocked=c_request;
    self_blocked.geometry.tractor_cab_rear_from_hitch_m=0.0;
    const auto self_blocked_result=plan(self_blocked);
    assert(!self_blocked_result.found);
    assert(self_blocked_result.self_collision_rejections>0);
    assert(self_blocked_result.self_collision_rejections+
           self_blocked_result.obstacle_collision_rejections+
           self_blocked_result.start_state_rejections+
           self_blocked_result.curvature_rejections+
           self_blocked_result.articulation_rejections+
           self_blocked_result.forward_motion_rejections+
           self_blocked_result.other_rejections==self_blocked_result.candidate_count);

    // J: display contract is always the tractor's two physical rear-corner
    // tracks, including attached-trailer mode. The pair keeps fixed width and
    // remains centred on the tractor rear edge for every path sample.
    for (const auto *result:{&a,&d})
    {
        const auto geometry=result==&a
            ?request(PlannerMode::tractor_to_target_trailer,{{0,0},0},{{0,-1},0}).geometry
            :d_request.geometry;
        for (const auto &sample:result->samples)
        {
            const auto tracks=tractor_rear_tracks(sample,geometry);
            assert(near(length(subtract(tracks.right,tracks.left)),
                        geometry.tractor_width_m+0.36,1e-8));
            const auto midpoint=multiply(add(tracks.left,tracks.right),0.5);
            const auto expected=subtract(
                add(sample.tractor_position,
                    multiply(forward(sample.tractor_heading),
                             geometry.tractor_center_from_reference_m)),
                multiply(forward(sample.tractor_heading),
                         geometry.tractor_length_m*0.5));
            assert(length(subtract(midpoint,expected))<1e-8);
        }
    }

    // K: reproduce the legacy runtime's actor-rear-axis placement exactly.
    // The new dynamic planner stores physical-front headings and a rear-axle
    // reference, so both rear corners and the fifth wheel must still map to
    // the same world points (left/right labels may exchange after +pi).
    {
        const Vec2 actor_origin{13.0,-7.0};
        const double actor_rear_heading=0.63;
        const double rear_axle_local_z=1.18;
        const double center_from_rear=-2.36;
        const double hook_from_rear=-0.24;
        VehicleGeometry geometry;
        geometry.tractor_width_m=2.53;
        geometry.tractor_length_m=6.91;
        geometry.tractor_center_from_reference_m=-center_from_rear;
        geometry.tractor_reference_to_hitch_m=-hook_from_rear;
        const auto rear_pose=calibrated_rear_axle_pose(
            actor_origin,actor_rear_heading,rear_axle_local_z);
        PathSample live{};
        live.tractor_position=rear_pose.position;
        live.tractor_heading=rear_pose.heading_rad;
        const auto green=tractor_rear_tracks(live,geometry);

        const Vec2 actor_reverse{std::sin(actor_rear_heading),
                                 std::cos(actor_rear_heading)};
        const Vec2 actor_local_x{std::cos(actor_rear_heading),
                                -std::sin(actor_rear_heading)};
        const auto legacy_center=add(rear_pose.position,
            multiply(actor_reverse,center_from_rear+geometry.tractor_length_m*0.5));
        const double half=geometry.tractor_width_m*0.5+0.18;
        const auto legacy_a=subtract(legacy_center,multiply(actor_local_x,half));
        const auto legacy_b=add(legacy_center,multiply(actor_local_x,half));
        const bool direct=length(subtract(green.left,legacy_a))<1e-9&&
                          length(subtract(green.right,legacy_b))<1e-9;
        const bool exchanged=length(subtract(green.left,legacy_b))<1e-9&&
                             length(subtract(green.right,legacy_a))<1e-9;
        assert(direct||exchanged);

        const auto legacy_hitch=add(rear_pose.position,
                                    multiply(actor_reverse,hook_from_rear));
        const auto planner_hitch=add(rear_pose.position,
            multiply(forward(rear_pose.heading_rad),
                     geometry.tractor_reference_to_hitch_m));
        assert(length(subtract(legacy_hitch,planner_hitch))<1e-9);
    }

    // L: replay the last captured attached-rig seed with the wheel/hook
    // dimensions reported by the game runtime. Dynamic replanning must not
    // become impossible merely because the heuristic probe geometry was
    // replaced with the calibrated vehicle geometry.
    {
        PlanRequest replay;
        replay.mode=PlannerMode::trailer_to_parking_target;
        replay.tractor_start={{-12391.312081494,54660.085405287},-1.044348818};
        replay.subject_start={{-12382.145232368,54656.970280074},-1.269341143};
        replay.target={{-12347.224384438,54664.455734323},-1.539380582};
        replay.geometry.tractor_wheelbase_m=3.89;
        replay.geometry.tractor_width_m=2.53;
        replay.geometry.tractor_length_m=6.91;
        replay.geometry.tractor_center_from_reference_m=2.36;
        replay.geometry.tractor_reference_to_hitch_m=-1.133;
        replay.geometry.trailer_axle_to_hitch_m=8.57;
        replay.geometry.trailer_rear_overhang_m=2.92;
        replay.geometry.trailer_length_m=11.49;
        replay.geometry.trailer_width_m=2.35;
        replay.obstacles={
            {{-12349.506835938,54659.835937500},-1.538933547,
             2.579018116,13.903878212,"D0"},
            {{-12349.773437500,54650.421875000},-1.510988863,
             2.546030283,13.885635376,"D1"}};
        const auto result=plan(replay);
        if (!result.found)
            std::cerr << "dynamic replay failed: start="
                      << result.start_state_rejections << " curvature="
                      << result.curvature_rejections << " articulation="
                      << result.articulation_rejections << " obstacle="
                      << result.obstacle_collision_rejections << " self="
                      << result.self_collision_rejections << " forward="
                      << result.forward_motion_rejections << " other="
                      << result.other_rejections << "\n";
        assert(result.found);
    }

    // M: parking is a terminal region rather than an exact axle point. The
    // half-trailer entry plane is accepted, while insufficient longitudinal
    // or lateral overlap and excessive heading error remain outside it.
    {
        VehicleGeometry geometry;
        PathSample live{};
        live.trailer_position={0,geometry.trailer_length_m*0.5};
        live.trailer_heading=0;
        const Pose2 target{{0,0},0};
        const auto half=parking_overlap(live,target,geometry);
        assert(near(half.longitudinal_fraction,0.5,1e-8));
        assert(parking_target_reached(live,target,geometry));
        live.trailer_position.y=geometry.trailer_length_m*0.5+0.15;
        assert(!parking_target_reached(live,target,geometry));
        live.trailer_position={geometry.trailer_width_m*0.25,
                               geometry.trailer_length_m*0.5};
        assert(!parking_target_reached(live,target,geometry));
        live.trailer_position={0,geometry.trailer_length_m*0.5};
        live.trailer_heading=7.0*stage0::pi/180.0;
        assert(!parking_target_reached(live,target,geometry));
        assert(parking_guidance_phase(live,target,geometry));
        live.trailer_heading=16.0*stage0::pi/180.0;
        assert(!parking_guidance_phase(live,target,geometry));
        live.trailer_heading=90.0*stage0::pi/180.0;
        assert(!parking_guidance_phase(live,target,geometry));
    }

    // N: cab arrows use the error between future-path steering and the live
    // effective steering value, with an 8/4 degree hysteresis band.
    {
        VehicleGeometry geometry;
        PlanResult future;
        future.found=true;
        future.samples.resize(4);
        for (std::size_t i=0;i<future.samples.size();++i)
            future.samples[i].tractor_position={0,-static_cast<double>(i)};
        future.samples[1].tractor_heading=0.08;
        future.samples[2].tractor_heading=0.16;
        future.samples[3].tractor_heading=0.24;
        const double required=-near_horizon_reverse_steering_rad(
            future,geometry,3.0);
        assert(required<-8.0*stage0::pi/180.0);
        assert(update_steering_instruction(
            SteeringInstruction::reverse_continue,
            steering_correction_rad(future,geometry,0.0))==
            SteeringInstruction::left);
        const double aligned_input=required/geometry.tractor_max_steer_rad;
        assert(update_steering_instruction(
            SteeringInstruction::right,
            steering_correction_rad(future,geometry,aligned_input))==
            SteeringInstruction::reverse_continue);
        assert(update_steering_instruction(
            SteeringInstruction::reverse_continue,
            steering_correction_rad(future,geometry,
                aligned_input-12.0/38.0))==
            SteeringInstruction::right);
        for (auto &sample:future.samples)
            sample.tractor_heading=-sample.tractor_heading;
        assert(update_steering_instruction(
            SteeringInstruction::reverse_continue,
            steering_correction_rad(future,geometry,0.0))==
            SteeringInstruction::right);
    }

    // O: advanced coupling is based on fifth-wheel/kingpin alignment, not on
    // tractor and trailer body overlap. Small game-like tolerance is allowed,
    // but an arbitrary-angle or vertically separated overlap is rejected.
    {
        const auto aligned=coupling_alignment(
            {10.04,20.08},1.20,0.02,{10.0,20.0},1.25,0.0);
        assert(advanced_coupling_aligned(aligned));
        const auto crossed=coupling_alignment(
            {10.04,20.08},1.20,45.0*stage0::pi/180.0,
            {10.0,20.0},1.25,0.0);
        assert(!advanced_coupling_aligned(crossed));
        const auto lateral_miss=coupling_alignment(
            {10.25,20.0},1.20,0.0,{10.0,20.0},1.25,0.0);
        assert(!advanced_coupling_aligned(lateral_miss));
        const auto height_miss=coupling_alignment(
            {10.0,20.0},1.50,0.0,{10.0,20.0},1.25,0.0);
        assert(!advanced_coupling_aligned(height_miss));

        const auto final_approach=coupling_alignment(
            {0.20,2.25},1.20,10.0*stage0::pi/180.0,
            {0.0,0.0},1.20,0.0);
        assert(advanced_coupling_approach_phase(final_approach));
        assert(!advanced_coupling_aligned(final_approach));
        const auto overshot=coupling_alignment(
            {0.0,-0.20},1.20,0.0,{0.0,0.0},1.20,0.0);
        assert(!advanced_coupling_approach_phase(overshot));
        const auto crossed_approach=coupling_alignment(
            {0.0,2.0},1.20,20.0*stage0::pi/180.0,
            {0.0,0.0},1.20,0.0);
        assert(!advanced_coupling_approach_phase(crossed_approach));
        // The wider physics snap corridor suppresses a false X but does not
        // claim strict advanced-coupling completion.
        assert(coupling_capture_corridor(crossed_approach));
        const auto impossible_capture=coupling_alignment(
            {0.0,2.0},1.20,70.0*stage0::pi/180.0,
            {0.0,0.0},1.20,0.0);
        assert(!coupling_capture_corridor(impossible_capture));
    }

    // P: long-distance attached planning reconstructs the tractor through the
    // real off-axle fifth wheel. Every rear-axle interval must satisfy the
    // no-lateral-slip constraint; the former pointwise sin(phi)/L inversion
    // accumulated a visible cross-track error for this geometry.
    {
        PlanRequest exact;
        exact.mode=PlannerMode::trailer_to_parking_target;
        exact.geometry.trailer_axle_to_hitch_m=8.4;
        exact.geometry.tractor_reference_to_hitch_m=-1.13;
        exact.geometry.tractor_reference_to_hitch_lateral_m=0.08;
        exact.subject_start={{0.0,0.0},0.0};
        const double tractor_heading=10.0*stage0::pi/180.0;
        const auto initial_hitch=add(exact.subject_start.position,
            multiply(forward(exact.subject_start.heading_rad),
                     exact.geometry.trailer_axle_to_hitch_m));
        exact.tractor_start={tractor_reference_from_hitch(
            initial_hitch,tractor_heading,exact.geometry),tractor_heading};
        exact.target={{3.0,-18.0},0.0};
        exact.check_known_obstacles=false;
        exact.check_self_contour=false;
        const auto result=plan(exact);
        if (!result.found)
            std::cerr << "exact articulation regression failed: "
                      << result.reason << " start="
                      << result.start_state_rejections << " curvature="
                      << result.curvature_rejections << " articulation="
                      << result.articulation_rejections << " forward="
                      << result.forward_motion_rejections << " other="
                      << result.other_rejections << "\n";
        assert(result.found&&result.samples.size()>100);
        for (std::size_t i=1;i<result.samples.size();++i)
        {
            const auto &before=result.samples[i-1];
            const auto &after=result.samples[i];
            const auto rigid_hitch=add(
                add(after.tractor_position,
                    multiply(forward(after.tractor_heading),
                             exact.geometry.tractor_reference_to_hitch_m)),
                multiply(stage1a::right(after.tractor_heading),
                         exact.geometry.tractor_reference_to_hitch_lateral_m));
            assert(length(subtract(rigid_hitch,after.hitch_position))<1e-8);
            const auto rear_delta=subtract(after.tractor_position,
                                           before.tractor_position);
            const double midpoint=stage0::wrap_angle(before.tractor_heading+
                0.5*stage0::wrap_angle(after.tractor_heading-
                                       before.tractor_heading));
            assert(std::abs(dot(rear_delta,stage1a::right(midpoint)))<1e-4);
            const double rear_distance=length(rear_delta);
            const double curvature=std::abs(stage0::wrap_angle(
                after.tractor_heading-before.tractor_heading))/rear_distance;
            const double maximum=std::tan(
                exact.geometry.tractor_max_steer_rad)/
                exact.geometry.tractor_wheelbase_m;
            assert(curvature<=maximum+1e-8);
        }
    }

    // Q: trailer spatial curvature uses the exact articulation denominator
    // and responds to a real off-axle hitch, current tractor curvature and
    // the stable grounded trailer axle's independent steering angle.
    {
        VehicleGeometry geometry;
        geometry.trailer_axle_to_hitch_m=8.0;
        const double articulation=30.0*stage0::pi/180.0;
        assert(near(articulated_trailer_curvature(
            articulation,0.0,geometry),std::tan(articulation)/8.0,1e-10));
        geometry.tractor_reference_to_hitch_m=-1.13;
        geometry.tractor_reference_to_hitch_lateral_m=0.08;
        const double tractor_curvature=0.12;
        const double scale=1.0-0.08*tractor_curvature;
        const double expected=(scale*std::sin(articulation)-
            1.13*tractor_curvature*std::cos(articulation))/(
            8.0*(scale*std::cos(articulation)+
                 1.13*tractor_curvature*std::sin(articulation)));
        assert(near(articulated_trailer_curvature(
            articulation,tractor_curvature,geometry),expected,1e-10));

        geometry.trailer_axle_steering_rad=8.0*stage0::pi/180.0;
        const double beta=geometry.trailer_axle_steering_rad;
        const double expected_with_beta=(
            (scale*std::sin(articulation)-
             1.13*tractor_curvature*std::cos(articulation))*std::cos(beta)-
            (scale*std::cos(articulation)+
             1.13*tractor_curvature*std::sin(articulation))*std::sin(beta))/(
            8.0*(scale*std::cos(articulation)+
                 1.13*tractor_curvature*std::sin(articulation)));
        assert(near(articulated_trailer_curvature(
            articulation,tractor_curvature,geometry),expected_with_beta,1e-10));

        geometry.tractor_reference_to_hitch_m=0.0;
        geometry.tractor_reference_to_hitch_lateral_m=0.0;
        assert(near(articulated_trailer_curvature(
            0.0,0.0,geometry),-std::sin(beta)/8.0,1e-10));
        geometry.trailer_axle_steering_rad=0.0;
        assert(near(articulated_trailer_curvature(
            articulation,0.0,geometry),std::tan(articulation)/8.0,1e-10));
    }

    // R: a constant equivalent trailer steering angle changes the axle-path
    // tangent, while endpoint body headings and the rigid hitch constraint
    // remain exact. This is the contract used by 100 ms live replanning.
    {
        PlanRequest steered;
        steered.mode=PlannerMode::trailer_to_parking_target;
        steered.geometry.trailer_axle_to_hitch_m=8.57329;
        steered.geometry.trailer_axle_steering_rad=
            0.5*stage0::pi/180.0;
        steered.geometry.tractor_reference_to_hitch_m=0.66;
        steered.subject_start={{0.0,0.0},0.0};
        // Steady straight rolling with beta requires the tractor to share the
        // axle rolling heading, while the trailer body remains at zero.
        const double tractor_heading=
            steered.geometry.trailer_axle_steering_rad;
        const auto hitch=add(steered.subject_start.position,
            multiply(forward(steered.subject_start.heading_rad),
                     steered.geometry.trailer_axle_to_hitch_m));
        steered.tractor_start={tractor_reference_from_hitch(
            hitch,tractor_heading,steered.geometry),tractor_heading};
        steered.target={multiply(
            reverse_direction(steered.geometry.trailer_axle_steering_rad),
            15.0),0.0};
        steered.check_known_obstacles=false;
        steered.check_self_contour=false;
        const auto result=plan(steered);
        if (!result.found)
            std::cerr << "constant-beta regression failed: " << result.reason
                      << " start=" << result.start_state_rejections
                      << " curvature=" << result.curvature_rejections
                      << " articulation=" << result.articulation_rejections
                      << " forward=" << result.forward_motion_rejections
                      << " other=" << result.other_rejections << "\n";
        assert(result.found&&result.samples.size()>20);
        assert(std::abs(stage0::wrap_angle(
            result.samples.front().trailer_heading-
            steered.subject_start.heading_rad))<1e-8);
        assert(std::abs(stage0::wrap_angle(
            result.samples.back().trailer_heading-
            steered.target.heading_rad))<1e-8);
        for (const auto &sample:result.samples)
        {
            const auto rigid=add(sample.trailer_position,
                multiply(forward(sample.trailer_heading),
                         steered.geometry.trailer_axle_to_hitch_m));
            assert(length(subtract(rigid,sample.hitch_position))<1e-8);
        }
    }

    // S: lift/lower animation masks are held behind a 400 ms stability gate.
    // Stable-topology beta remains live, while 5/6/7-wheel transients never
    // replace the last committed 4- or 8-wheel equivalent axle.
    {
        StableTrailerAxleFilter filter;
        assert(filter.update({0xff,8.57329,0.001,false},0));
        assert(filter.valid());
        assert(filter.stable().grounded_mask==0xff);
        assert(filter.update({0x3f,7.91750,0.12,true},100));
        assert(filter.stable().grounded_mask==0xff);
        assert(filter.update({0x3c,8.57861,0.0,false},200));
        assert(filter.update({0x3c,8.57861,0.0,false},599));
        assert(filter.stable().grounded_mask==0xff);
        assert(filter.update({0x3c,8.57861,0.0,false},600));
        assert(filter.stable().grounded_mask==0x3c);
        assert(near(filter.stable().axle_to_hitch_m,8.57861,1e-10));
        assert(filter.update({0x3c,8.57861,0.02,false},700));
        assert(near(filter.stable().axle_steering_rad,0.02,1e-10));
        assert(filter.update({0x1c,8.18097,0.07,false},800));
        assert(filter.update({0x7c,8.29301,0.05,false},900));
        assert(filter.update({0xff,8.57329,0.001,false},1000));
        assert(filter.update({0xff,8.57329,0.001,false},1399));
        assert(filter.stable().grounded_mask==0x3c);
        assert(filter.update({0xff,8.57329,0.001,false},1400));
        assert(filter.stable().grounded_mask==0xff);
    }

    std::cout << "stage1a tests passed: A straight, B offset, C aligned trailer, "
                 "D offset trailer, E 70deg hard limit, F mirror, "
                 "G attached-only product mode, "
                 "historical live replay, fail-closed input, H obstacle contour, "
                 "I self-contour, J tractor-only twin rear tracks, "
                 "K legacy placement equivalence, L calibrated live replay, "
                 "M parking terminal-region gate, N future-steering arrows, "
                 "O advanced-coupling alignment gate, "
                 "P exact off-axle articulation, Q steered trailer curvature, "
                 "R constant-beta path contract, S stable lift topology, "
                 "T live white-frame steering-profile fallback, "
                 "U mid-route terminal region, V continuous arrow angle, "
                 "W steering-independent route boundary\n";
}
