#pragma once

#include "stage0_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace stage1a
{
using stage0::Pose2;
using stage0::Vec2;

constexpr double articulation_hard_limit_rad = 70.0 * stage0::pi / 180.0;
constexpr double articulation_soft_rad = 45.0 * stage0::pi / 180.0;
constexpr double articulation_high_rad = 55.0 * stage0::pi / 180.0;
constexpr double default_sample_spacing_m = 0.10;
// Stage 0 only perceives task guides inside the 50 m local telemetry bubble.
// A farther target is necessarily stale (usually a seed retained across a
// teleport/job change) and must never enter the combinatorial curve search.
constexpr double maximum_planning_chord_m = 50.0;
constexpr double maximum_candidate_curve_length_m = 80.0;
constexpr int maximum_arc_length_samples = 1200;

enum class PlannerMode
{
    inactive,
    tractor_to_target_trailer,
    trailer_to_parking_target
};

inline const char *mode_name(PlannerMode mode)
{
    switch (mode)
    {
    case PlannerMode::tractor_to_target_trailer:
        return "TRACTOR_TO_TARGET_TRAILER";
    case PlannerMode::trailer_to_parking_target:
        return "TRAILER_TO_PARKING_TARGET";
    default:
        return "INACTIVE";
    }
}

struct ModeDecision
{
    PlannerMode mode{PlannerMode::inactive};
    std::string reason;
};

inline ModeDecision select_mode(bool planner_active, int attached_trailer_count,
                                bool /*target_trailer_verified*/,
                                bool attached_trailer_verified,
                                int unique_parking_target_count)
{
    if (!planner_active)
        return {PlannerMode::inactive, "PATH_PLANNER_INACTIVE"};
    if (attached_trailer_count == 0)
        return {PlannerMode::inactive, "DETACHED_TRAILER_GUIDANCE_DISABLED"};
    if (!attached_trailer_verified)
        return {PlannerMode::inactive, "ATTACHED_TRAILER_NOT_VERIFIED"};
    if (unique_parking_target_count != 1)
        return {PlannerMode::inactive, "PARKING_TARGET_NOT_UNIQUE"};
    return {PlannerMode::trailer_to_parking_target, {}};
}

struct VehicleGeometry
{
    double tractor_wheelbase_m{3.8};
    double tractor_max_steer_rad{38.0 * stage0::pi / 180.0};
    double trailer_axle_to_hitch_m{8.5};
    // Equivalent rolling direction of the currently stable, grounded trailer
    // wheel group relative to the trailer body heading. Runtime topology
    // changes must be debounced before this value and L are committed.
    double trailer_axle_steering_rad{0.0};
    double tractor_reference_to_hitch_m{0.0};
    double tractor_reference_to_hitch_lateral_m{0.0};
    double tractor_width_m{2.5};
    double tractor_length_m{6.8};
    double tractor_center_from_reference_m{0.0};
    double tractor_cab_length_m{4.0};
    // Approximate longitudinal clearance from fifth wheel to the cab's rear
    // collision plane. Kept explicit/heuristic until a native cab contour is read.
    double tractor_cab_rear_from_hitch_m{2.2};
    double trailer_width_m{2.5};
    double trailer_length_m{13.7};
    double trailer_rear_overhang_m{3.4};
    double contour_clearance_m{0.15};
};

struct TrailerAxleSnapshot
{
    std::uint64_t grounded_mask{};
    double axle_to_hitch_m{};
    double axle_steering_rad{};
    bool lift_transition_in_progress{};
    // These body-envelope terms depend on the chosen equivalent axle too.
    // Stabilize them with L so collision and route graphics cannot twitch
    // while ETS2 reports one wheel at a time during a lift animation.
    double rear_overhang_m{};
    double width_m{};
};

// Wheel contact changes one tyre at a time during ETS2 lift/lower animation.
// Publishing each intermediate mask creates a false 0.6 m class jump in L.
// Keep the previous stable topology, then commit the final mask atomically.
class StableTrailerAxleFilter
{
public:
    bool update(const TrailerAxleSnapshot &candidate,
                std::uint64_t now_ms,std::uint64_t settle_ms=400)
    {
        if (!valid_candidate(candidate)) return valid_;
        if (!valid_)
        {
            if (candidate.lift_transition_in_progress) return false;
            commit(candidate);
            return true;
        }
        if (candidate.grounded_mask==stable_.grounded_mask&&
            !candidate.lift_transition_in_progress)
        {
            // Geometry and beta may evolve continuously while topology stays
            // fixed, so refresh them without a debounce delay.
            stable_=candidate;
            pending_valid_=false;
            return true;
        }
        if (candidate.lift_transition_in_progress)
        {
            pending_valid_=false;
            return true;
        }
        if (!pending_valid_||
            pending_.grounded_mask!=candidate.grounded_mask)
        {
            pending_=candidate;
            pending_since_ms_=now_ms;
            pending_valid_=true;
            return true;
        }
        pending_=candidate;
        if (now_ms-pending_since_ms_>=settle_ms)
        {
            commit(pending_);
            pending_valid_=false;
        }
        return true;
    }

    bool valid() const { return valid_; }
    const TrailerAxleSnapshot &stable() const { return stable_; }
    void reset()
    {
        valid_=false;
        pending_valid_=false;
        stable_={};
        pending_={};
        pending_since_ms_=0;
    }

private:
    static bool valid_candidate(const TrailerAxleSnapshot &candidate)
    {
        return candidate.grounded_mask!=0&&
            std::isfinite(candidate.axle_to_hitch_m)&&
            candidate.axle_to_hitch_m>=0.5&&
            std::isfinite(candidate.axle_steering_rad);
    }
    void commit(const TrailerAxleSnapshot &candidate)
    {
        stable_=candidate;
        stable_.lift_transition_in_progress=false;
        valid_=true;
    }

    bool valid_{};
    bool pending_valid_{};
    TrailerAxleSnapshot stable_{};
    TrailerAxleSnapshot pending_{};
    std::uint64_t pending_since_ms_{};
};

struct OrientedBox
{
    Vec2 center{};
    double heading_rad{};
    double width_m{};
    double length_m{};
    std::string label;
};

struct PathSample
{
    double distance{};
    Vec2 tractor_position{};
    double tractor_heading{};
    Vec2 hitch_position{};
    Vec2 trailer_position{};
    double trailer_heading{};
    double articulation_angle{};
    double trailer_curvature{};
};

struct PlanRequest
{
    PlannerMode mode{PlannerMode::inactive};
    Pose2 tractor_start{};
    Pose2 subject_start{}; // tractor reference or attached-trailer axle reference
    Pose2 target{};        // kingpin/coupling pose or parking axle pose
    VehicleGeometry geometry{};
    // Planner-selected signed forward-driving curvature at the tractor rear
    // axle. The game runtime deliberately never feeds measured steering here;
    // measured steering belongs only to the cab-arrow control error.
    double tractor_start_curvature_m_inv{};
    double sample_spacing_m{default_sample_spacing_m};
    std::vector<OrientedBox> obstacles;
    bool check_known_obstacles{true};
    bool check_self_contour{true};
    int candidate_grid_size{12};
    bool allow_steering_profile_fallback{true};
    int physical_seed_pair_limit{25};
    // Search a route-owned initial curvature instead of treating the neutral
    // (steering-independent) runtime value as a mandatory first control.
    // This remains independent of the player's current steering input.
    bool search_planner_initial_curvature{false};
    int planner_initial_curvature_seed_limit{9};
};

struct PlanResult
{
    PlannerMode mode{PlannerMode::inactive};
    bool found{};
    std::string reason{"NO_REQUEST"};
    int candidate_count{};
    int valid_candidate_count{};
    int obstacle_collision_rejections{};
    int self_collision_rejections{};
    int start_state_rejections{};
    int curvature_rejections{};
    int articulation_rejections{};
    int forward_motion_rejections{};
    int other_rejections{};
    int known_obstacle_count{};
    double cost{std::numeric_limits<double>::infinity()};
    double path_length{};
    double max_articulation{};
    double mean_articulation{};
    double max_curvature{};
    double max_required_tractor_curvature{};
    double failure_distance{};
    double curvature_change{};
    double final_position_error{};
    double final_heading_error{};
    double initial_tractor_heading_error{};
    double initial_tractor_position_error{};
    double selected_d0{};
    double selected_d1{};
    std::vector<PathSample> samples;
};

inline constexpr double steering_independent_route_start_curvature_m_inv()
{
    // Cubic candidates infer their required steering from geometry. The
    // physical fallback uses a neutral seed, never the player's wheel input.
    return 0.0;
}

inline bool finite(Vec2 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 subtract(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 multiply(Vec2 a, double value) { return {a.x * value, a.y * value}; }
inline double length(Vec2 value) { return std::hypot(value.x, value.y); }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline Vec2 forward(double heading) { return {std::sin(heading), std::cos(heading)}; }
inline Vec2 reverse_direction(double heading) { return multiply(forward(heading), -1.0); }

inline Vec2 right(double heading) { return {std::cos(heading),-std::sin(heading)}; }

// SCS vehicle placement headings follow the actor's local +Z axis, observed
// to point toward the physical rear. Keep this conversion centralized so a
// live runtime cannot accidentally plan with the vehicle facing backwards.
inline double physical_front_heading_from_actor_rear(double actor_heading)
{
    return stage0::wrap_angle(actor_heading+stage0::pi);
}

inline Vec2 actor_local_to_world(Vec2 actor_origin,double actor_heading,
                                 double local_x,double local_z)
{
    return {actor_origin.x+local_x*std::cos(actor_heading)+
                           local_z*std::sin(actor_heading),
            actor_origin.y-local_x*std::sin(actor_heading)+
                           local_z*std::cos(actor_heading)};
}

inline Pose2 calibrated_rear_axle_pose(Vec2 actor_origin,
                                       double actor_rear_heading,
                                       double rear_axle_local_z)
{
    return {actor_local_to_world(actor_origin,actor_rear_heading,
                                 0.0,rear_axle_local_z),
            physical_front_heading_from_actor_rear(actor_rear_heading)};
}

struct RearTrackPair
{
    Vec2 left{};
    Vec2 right{};
};

inline RearTrackPair tractor_rear_tracks(const PathSample &sample,
                                         const VehicleGeometry &geometry,
                                         double envelope_clearance_m=0.18)
{
    // Follow the two reverse-leading physical rear corners for the whole
    // prediction. Never switch to a trailer edge or a support point chosen
    // from steering angle: that creates visible direction reversals.
    const auto physical_front=forward(sample.tractor_heading);
    const auto lateral_right=right(sample.tractor_heading);
    const auto body_center=add(sample.tractor_position,
        multiply(physical_front,geometry.tractor_center_from_reference_m));
    const auto rear_center=subtract(body_center,
        multiply(physical_front,geometry.tractor_length_m*0.5));
    const double half_span=geometry.tractor_width_m*0.5+
        std::max(0.0,envelope_clearance_m);
    return {subtract(rear_center,multiply(lateral_right,half_span)),
            add(rear_center,multiply(lateral_right,half_span))};
}

inline bool oriented_boxes_overlap(const OrientedBox &a,const OrientedBox &b,
                                   double clearance_m=0.0)
{
    if (!finite(a.center)||!finite(b.center)||!std::isfinite(a.heading_rad)||
        !std::isfinite(b.heading_rad)||a.width_m<=0.0||a.length_m<=0.0||
        b.width_m<=0.0||b.length_m<=0.0) return true;
    const std::array<Vec2,4> axes{forward(a.heading_rad),right(a.heading_rad),
                                  forward(b.heading_rad),right(b.heading_rad)};
    const Vec2 center_delta=subtract(b.center,a.center);
    const auto radius=[](const OrientedBox &box,Vec2 axis)
    {
        return box.length_m*0.5*std::abs(dot(forward(box.heading_rad),axis))+
               box.width_m*0.5*std::abs(dot(right(box.heading_rad),axis));
    };
    for (const auto axis:axes)
        if (std::abs(dot(center_delta,axis))>
            radius(a,axis)+radius(b,axis)+std::max(0.0,clearance_m))
            return false;
    return true;
}

inline OrientedBox trailer_contour(const PathSample &sample,
                                   const VehicleGeometry &geometry)
{
    const double center_from_axle=(geometry.trailer_axle_to_hitch_m-
                                   geometry.trailer_rear_overhang_m)*0.5;
    return {add(sample.trailer_position,
                multiply(forward(sample.trailer_heading),center_from_axle)),
            sample.trailer_heading,geometry.trailer_width_m,
            geometry.trailer_length_m,"SELF_TRAILER"};
}

inline OrientedBox tractor_contour(const PathSample &sample,
                                   const VehicleGeometry &geometry,
                                   bool connected)
{
    if (connected)
    {
        // Only the cab is a forbidden self-collision contour. The fifth-wheel
        // chassis and trailer front overlap by design at a valid coupling.
        const double center_from_hitch=geometry.tractor_cab_rear_from_hitch_m+
                                       geometry.tractor_cab_length_m*0.5;
        return {add(sample.hitch_position,
                    multiply(forward(sample.tractor_heading),center_from_hitch)),
                sample.tractor_heading,geometry.tractor_width_m,
                geometry.tractor_cab_length_m,"SELF_TRACTOR_CAB"};
    }
    return {add(sample.tractor_position,
                multiply(forward(sample.tractor_heading),
                         geometry.tractor_center_from_reference_m)),
            sample.tractor_heading,geometry.tractor_width_m,
            geometry.tractor_length_m,"SELF_TRACTOR"};
}

inline double tractor_steering_range_rad(const PlanResult &plan,
                                         const VehicleGeometry &geometry)
{
    if (plan.samples.size()<2) return std::numeric_limits<double>::infinity();
    double minimum=std::numeric_limits<double>::infinity();
    double maximum=-std::numeric_limits<double>::infinity();
    for (std::size_t index=1;index<plan.samples.size();++index)
    {
        const auto &before=plan.samples[index-1];
        const auto &after=plan.samples[index];
        const double distance=length(subtract(after.tractor_position,
                                              before.tractor_position));
        if (distance<1e-4) continue;
        const double heading_change=stage0::wrap_angle(
            after.tractor_heading-before.tractor_heading);
        const double steering=std::atan(geometry.tractor_wheelbase_m*
                                        heading_change/distance);
        minimum=std::min(minimum,steering);
        maximum=std::max(maximum,steering);
    }
    return std::isfinite(minimum)&&std::isfinite(maximum)
        ? maximum-minimum : std::numeric_limits<double>::infinity();
}

enum class SteeringInstruction
{
    reverse_continue,
    left,
    right
};

inline double near_horizon_reverse_steering_rad(
    const PlanResult &plan,const VehicleGeometry &geometry,
    double horizon_m=3.0)
{
    if (plan.samples.size()<2) return 0.0;
    double accumulated_distance=0.0;
    double accumulated_heading=0.0;
    for (std::size_t index=1;index<plan.samples.size();++index)
    {
        const auto &before=plan.samples[index-1];
        const auto &after=plan.samples[index];
        const double interval=length(subtract(after.tractor_position,
                                              before.tractor_position));
        if (interval<1e-4) continue;
        const double used=std::min(interval,
            std::max(0.0,horizon_m-accumulated_distance));
        accumulated_heading+=stage0::wrap_angle(
            after.tractor_heading-before.tractor_heading)*(used/interval);
        accumulated_distance+=used;
        if (accumulated_distance>=horizon_m-1e-6) break;
    }
    if (accumulated_distance<1e-4) return 0.0;
    // Positive physical-front heading change while travelling backward means
    // the vehicle rear is being guided left; negative means right.
    return std::atan(geometry.tractor_wheelbase_m*
                     accumulated_heading/accumulated_distance);
}

inline double steering_correction_rad(
    const PlanResult &plan,const VehicleGeometry &geometry,
    double effective_steering_normalized)
{
    // The path helper measures physical-front heading change along positive
    // reverse distance. Bicycle steering has the opposite sign in reverse.
    // SCS effective_steering uses the same normalized range as the legacy
    // blue instantaneous predictor.
    const double required_steering=-near_horizon_reverse_steering_rad(
        plan,geometry,3.0);
    const double current_steering=std::clamp(
        effective_steering_normalized,-1.0,1.0)*
        geometry.tractor_max_steer_rad;
    return stage0::wrap_angle(required_steering-current_steering);
}

// Turn a steering correction into the visual direction of a cab-side arrow.
// Up to 1 degree remains a straight reverse arrow. Between 1 and 36 degrees
// a smoothstep interpolation rotates it continuously; larger corrections keep
// the former unambiguous 90-degree left/right presentation.
inline double steering_arrow_heading_rad(
    double tractor_heading_rad,double steering_correction_rad,
    double straight_threshold_rad=1.0*stage0::pi/180.0,
    double full_turn_threshold_rad=36.0*stage0::pi/180.0)
{
    const double magnitude=std::abs(steering_correction_rad);
    double fraction=0.0;
    if (magnitude>straight_threshold_rad)
    {
        const double span=std::max(1e-6,
            full_turn_threshold_rad-straight_threshold_rad);
        const double linear=std::clamp(
            (magnitude-straight_threshold_rad)/span,0.0,1.0);
        fraction=linear*linear*(3.0-2.0*linear);
    }
    return stage0::wrap_angle(
        tractor_heading_rad+stage0::pi+
        // SCS effective_steering and the planner curvature use positive for
        // steering-wheel right. Around the rear-facing arrow axis, positive
        // yaw would draw left, so the visual yaw must use the opposite sign.
        -std::copysign(fraction*stage0::pi*0.5,steering_correction_rad));
}

inline SteeringInstruction update_steering_instruction(
    SteeringInstruction previous,double desired_steering_rad,
    double enter_threshold_rad=8.0*stage0::pi/180.0,
    double exit_threshold_rad=4.0*stage0::pi/180.0)
{
    const double magnitude=std::abs(desired_steering_rad);
    if (magnitude>=enter_threshold_rad)
        return desired_steering_rad>0.0
            ?SteeringInstruction::right:SteeringInstruction::left;
    if (magnitude<=exit_threshold_rad)
        return SteeringInstruction::reverse_continue;
    return previous;
}

struct CouplingAlignment
{
    double signed_longitudinal_error_m{};
    double signed_lateral_error_m{};
    double longitudinal_error_m{};
    double lateral_error_m{};
    double heading_error_rad{};
    double vertical_error_m{};
};

inline CouplingAlignment coupling_alignment(
    Vec2 live_fifth_wheel,double live_height_m,double tractor_heading_rad,
    Vec2 target_kingpin,double target_height_m,double trailer_heading_rad)
{
    const auto error=subtract(live_fifth_wheel,target_kingpin);
    const double longitudinal=dot(error,forward(trailer_heading_rad));
    const double lateral=dot(error,right(trailer_heading_rad));
    return {longitudinal,lateral,std::abs(longitudinal),std::abs(lateral),
            std::abs(stage0::wrap_angle(tractor_heading_rad-
                                        trailer_heading_rad)),
            std::abs(live_height_m-target_height_m)};
}

inline bool advanced_coupling_aligned(
    const CouplingAlignment &alignment,
    double maximum_longitudinal_error_m=0.20,
    double maximum_lateral_error_m=0.15,
    double maximum_heading_error_rad=5.0*stage0::pi/180.0,
    double maximum_vertical_error_m=0.15)
{
    return alignment.longitudinal_error_m<=maximum_longitudinal_error_m&&
           alignment.lateral_error_m<=maximum_lateral_error_m&&
           alignment.heading_error_rad<=maximum_heading_error_rad&&
           alignment.vertical_error_m<=maximum_vertical_error_m;
}

inline bool advanced_coupling_approach_phase(
    const CouplingAlignment &alignment,
    double maximum_forward_distance_m=3.0,
    double maximum_lateral_error_m=0.50,
    double maximum_heading_error_rad=15.0*stage0::pi/180.0,
    double maximum_vertical_error_m=0.35)
{
    // Positive longitudinal error means the fifth wheel remains in front of
    // the kingpin along the trailer's physical-front axis. A small negative
    // tolerance absorbs telemetry noise, but an overshoot is not "continue".
    return alignment.signed_longitudinal_error_m>=-0.05&&
           alignment.signed_longitudinal_error_m<=maximum_forward_distance_m&&
           alignment.lateral_error_m<=maximum_lateral_error_m&&
           alignment.heading_error_rad<=maximum_heading_error_rad&&
           alignment.vertical_error_m<=maximum_vertical_error_m;
}

// The physics connection can snap the fifth wheel onto the kingpin inside a
// wider capture corridor than our final, strict alignment contract. A failed
// exact path inside this corridor is therefore not evidence of an unreachable
// target and must never be rendered as a red X.
inline bool coupling_capture_corridor(
    const CouplingAlignment &alignment,
    double maximum_forward_distance_m=3.5,
    double maximum_lateral_error_m=1.0,
    double maximum_heading_error_rad=35.0*stage0::pi/180.0,
    double maximum_vertical_error_m=0.50)
{
    return alignment.signed_longitudinal_error_m>=-0.25&&
           alignment.signed_longitudinal_error_m<=maximum_forward_distance_m&&
           alignment.lateral_error_m<=maximum_lateral_error_m&&
           alignment.heading_error_rad<=maximum_heading_error_rad&&
           alignment.vertical_error_m<=maximum_vertical_error_m;
}

struct ParkingOverlap
{
    double longitudinal_fraction{};
    double lateral_fraction{};
    double heading_error_rad{};
};

inline ParkingOverlap parking_overlap(const PathSample &live,
                                      const Pose2 &target,
                                      const VehicleGeometry &geometry)
{
    PathSample target_sample{};
    target_sample.trailer_position=target.position;
    target_sample.trailer_heading=target.heading_rad;
    const auto current=trailer_contour(live,geometry);
    const auto goal=trailer_contour(target_sample,geometry);
    const auto goal_longitudinal=forward(goal.heading_rad);
    const auto goal_lateral=right(goal.heading_rad);
    const auto current_longitudinal=forward(current.heading_rad);
    const auto current_lateral=right(current.heading_rad);
    const auto delta=subtract(current.center,goal.center);
    const auto projected_half_extent=[&](Vec2 axis) {
        return std::abs(dot(current_longitudinal,axis))*current.length_m*0.5+
               std::abs(dot(current_lateral,axis))*current.width_m*0.5;
    };
    const auto interval_overlap=[](double center_distance,double current_half,
                                   double goal_half) {
        return std::max(0.0,std::min(center_distance+current_half,goal_half)-
                            std::max(center_distance-current_half,-goal_half));
    };
    const double longitudinal=interval_overlap(
        dot(delta,goal_longitudinal),projected_half_extent(goal_longitudinal),
        goal.length_m*0.5);
    const double lateral=interval_overlap(
        dot(delta,goal_lateral),projected_half_extent(goal_lateral),
        goal.width_m*0.5);
    return {std::clamp(longitudinal/std::max(0.1,goal.length_m),0.0,1.0),
            std::clamp(lateral/std::max(0.1,goal.width_m),0.0,1.0),
            std::abs(stage0::wrap_angle(live.trailer_heading-
                                        target.heading_rad))};
}

inline bool parking_target_reached(const PathSample &live,
                                   const Pose2 &target,
                                   const VehicleGeometry &geometry,
                                   double minimum_longitudinal_fraction=0.50,
                                   double minimum_lateral_fraction=0.80,
                                   double maximum_heading_error_rad=
                                       6.0*stage0::pi/180.0)
{
    const auto overlap=parking_overlap(live,target,geometry);
    return overlap.longitudinal_fraction>=minimum_longitudinal_fraction&&
           overlap.lateral_fraction>=minimum_lateral_fraction&&
           overlap.heading_error_rad<=maximum_heading_error_rad;
}

inline bool parking_guidance_phase(const PathSample &live,
                                   const Pose2 &target,
                                   const VehicleGeometry &geometry,
                                   double minimum_longitudinal_fraction=0.01,
                                   double minimum_lateral_fraction=0.20,
                                   double maximum_heading_error_rad=
                                       15.0*stage0::pi/180.0)
{
    const auto overlap=parking_overlap(live,target,geometry);
    return overlap.longitudinal_fraction>minimum_longitudinal_fraction&&
           overlap.lateral_fraction>minimum_lateral_fraction&&
           overlap.heading_error_rad<=maximum_heading_error_rad;
}

// ETS2 accepts a small terminal pose region rather than one mathematical
// point. Use a modest depth inside the frame plus the closest admissible
// lateral/heading offsets so a short remaining manoeuvre is not rejected merely
// for demanding exact centre alignment at one longitudinal entry plane.
inline Pose2 parking_entry_pose(const PathSample &live,const Pose2 &frame,
                                const VehicleGeometry &geometry,
                                double longitudinal_depth_m=0.0,
                                double maximum_lateral_relief_m=0.35,
                                double maximum_heading_relief_rad=
                                    5.0*stage0::pi/180.0)
{
    Pose2 entry{
        add(frame.position,multiply(forward(frame.heading_rad),
            geometry.trailer_length_m*0.5-
            std::clamp(longitudinal_depth_m,0.0,
                       geometry.trailer_length_m*0.25))),
        frame.heading_rad};
    const double lateral_error=dot(
        subtract(live.trailer_position,entry.position),
        right(frame.heading_rad));
    entry.position=add(entry.position,multiply(
        right(frame.heading_rad),
        std::clamp(lateral_error,-maximum_lateral_relief_m,
                                  maximum_lateral_relief_m)));
    const double heading_error=stage0::wrap_angle(
        live.trailer_heading-frame.heading_rad);
    entry.heading_rad=stage0::wrap_angle(frame.heading_rad+std::clamp(
        heading_error,-maximum_heading_relief_rad,
                       maximum_heading_relief_rad));
    return entry;
}

inline bool parking_entry_corridor(const PathSample &live,const Pose2 &frame,
                                   const VehicleGeometry &geometry,
                                   double maximum_heading_error_rad=
                                       15.0*stage0::pi/180.0)
{
    const Pose2 entry=parking_entry_pose(live,frame,geometry);
    const auto delta=subtract(live.trailer_position,entry.position);
    const double longitudinal=dot(delta,forward(frame.heading_rad));
    const double lateral=std::abs(dot(delta,right(frame.heading_rad)));
    return longitudinal>=-0.50&&
           longitudinal<=std::max(6.0,geometry.trailer_length_m*0.60)&&
           lateral<=1.0&&
           std::abs(stage0::wrap_angle(live.trailer_heading-
                                       frame.heading_rad))<=
               maximum_heading_error_rad;
}

struct CubicBezier
{
    Vec2 p0{}, p1{}, p2{}, p3{};
};

inline Vec2 evaluate(const CubicBezier &curve, double t)
{
    const double u = 1.0 - t;
    return add(add(multiply(curve.p0, u*u*u), multiply(curve.p1, 3.0*u*u*t)),
               add(multiply(curve.p2, 3.0*u*t*t), multiply(curve.p3, t*t*t)));
}

inline Vec2 derivative(const CubicBezier &curve, double t)
{
    const double u = 1.0 - t;
    return add(add(multiply(subtract(curve.p1, curve.p0), 3.0*u*u),
                   multiply(subtract(curve.p2, curve.p1), 6.0*u*t)),
               multiply(subtract(curve.p3, curve.p2), 3.0*t*t));
}

inline Vec2 second_derivative(const CubicBezier &curve, double t)
{
    const Vec2 a{curve.p2.x - 2.0*curve.p1.x + curve.p0.x,
                 curve.p2.y - 2.0*curve.p1.y + curve.p0.y};
    const Vec2 b{curve.p3.x - 2.0*curve.p2.x + curve.p1.x,
                 curve.p3.y - 2.0*curve.p2.y + curve.p1.y};
    return add(multiply(a, 6.0*(1.0-t)), multiply(b, 6.0*t));
}

inline double travel_heading(Vec2 tangent)
{
    return std::atan2(tangent.x, tangent.y);
}

inline double heading_derivative_per_metre(Vec2 first, Vec2 second)
{
    const double speed2 = dot(first, first);
    if (speed2 < 1e-12) return std::numeric_limits<double>::quiet_NaN();
    // heading is atan2(dx,dy), rather than the standard atan2(dy,dx).
    return (first.y * second.x - first.x * second.y) /
           std::pow(speed2, 1.5);
}

struct ArcPoint
{
    double t{}, distance{};
};

template <typename Curve>
inline std::vector<ArcPoint> make_arc_table(const Curve &curve)
{
    const double chord = length(subtract(curve.p3, curve.p0));
    const int segments = std::clamp(static_cast<int>(std::ceil(chord * 100.0)), 600, 5000);
    std::vector<ArcPoint> table;
    table.reserve(static_cast<std::size_t>(segments) + 1);
    table.push_back({0.0, 0.0});
    Vec2 previous = curve.p0;
    double accumulated = 0.0;
    for (int index = 1; index <= segments; ++index)
    {
        const double t = static_cast<double>(index) / segments;
        const Vec2 point = evaluate(curve, t);
        accumulated += length(subtract(point, previous));
        table.push_back({t, accumulated});
        previous = point;
    }
    return table;
}

template <typename Curve>
inline std::vector<double> arc_length_parameters(const Curve &curve,
                                                  double spacing,
                                                  double &total_length)
{
    const auto table = make_arc_table(curve);
    total_length = table.back().distance;
    std::vector<double> parameters;
    if (!(total_length > 1e-6) || !std::isfinite(total_length)) return parameters;
    spacing = std::clamp(spacing, 0.04, 0.25);
    const int steps = std::clamp(
        static_cast<int>(std::ceil(total_length / spacing)),
        1, maximum_arc_length_samples);
    parameters.reserve(static_cast<std::size_t>(steps) + 1);
    std::size_t upper = 1;
    for (int index = 0; index <= steps; ++index)
    {
        const double wanted = index == steps ? total_length :
            total_length * static_cast<double>(index) / steps;
        while (upper < table.size() && table[upper].distance < wanted) ++upper;
        if (upper >= table.size()) { parameters.push_back(1.0); continue; }
        const auto &a = table[upper-1];
        const auto &b = table[upper];
        const double fraction = b.distance > a.distance
            ? (wanted-a.distance)/(b.distance-a.distance) : 0.0;
        parameters.push_back(a.t+(b.t-a.t)*fraction);
    }
    return parameters;
}

inline double articulation_penalty(double radians)
{
    const double value = std::abs(radians);
    if (value <= articulation_soft_rad) return value * value * 2.0;
    if (value <= articulation_high_rad)
    {
        const double excess = value - articulation_soft_rad;
        return 2.0*value*value + 80.0*excess*excess;
    }
    const double excess = value - articulation_high_rad;
    return 2.0*value*value + 80.0*std::pow(articulation_high_rad-articulation_soft_rad,2) +
           350.0*excess*excess;
}

inline bool validate_hard_limits(const std::vector<PathSample> &samples,
                                 std::string &reason)
{
    for (const auto &sample : samples)
    {
        if (!finite(sample.tractor_position) || !finite(sample.hitch_position) ||
            !finite(sample.trailer_position) || !std::isfinite(sample.tractor_heading) ||
            !std::isfinite(sample.trailer_heading) ||
            !std::isfinite(sample.articulation_angle) ||
            !std::isfinite(sample.trailer_curvature))
        {
            reason = "NON_FINITE_PATH_SAMPLE";
            return false;
        }
        if (std::abs(stage0::wrap_angle(sample.tractor_heading-
                                        sample.trailer_heading)) >
            articulation_hard_limit_rad + 1e-9)
        {
            reason = "ARTICULATION_LIMIT_EXCEEDED";
            return false;
        }
    }
    return true;
}

inline Vec2 tractor_reference_from_hitch(Vec2 hitch,double tractor_heading,
                                         const VehicleGeometry &geometry)
{
    return subtract(
        subtract(hitch,multiply(forward(tractor_heading),
            geometry.tractor_reference_to_hitch_m)),
        multiply(right(tractor_heading),
            geometry.tractor_reference_to_hitch_lateral_m));
}

// Trailer-body curvature measured per metre travelled by the equivalent
// trailer axle. A steered axle rolls in body-heading + beta. Beta is frozen
// over one integration step; a time-varying beta contributes d(beta)/ds to the
// axle-path curvature, but not to the rigid trailer body's yaw rate.
//
// The denominator is essential: sin(phi)/L is a tractor-distance
// approximation, not the spatial curvature of the trailer path. The offset
// terms account for a fifth wheel that is not at the tractor rear axle.
inline double articulated_trailer_curvature(
    double articulation,double tractor_curvature,
    const VehicleGeometry &geometry)
{
    const double scale=1.0-
        geometry.tractor_reference_to_hitch_lateral_m*tractor_curvature;
    const double longitudinal=
        geometry.tractor_reference_to_hitch_m;
    const double numerator=scale*std::sin(articulation)+
        longitudinal*tractor_curvature*std::cos(articulation);
    const double longitudinal_speed=
        scale*std::cos(articulation)-
        longitudinal*tractor_curvature*std::sin(articulation);
    const double beta=std::clamp(
        geometry.trailer_axle_steering_rad,
        -60.0*stage0::pi/180.0,60.0*stage0::pi/180.0);
    const double denominator=std::max(
        0.5,geometry.trailer_axle_to_hitch_m)*longitudinal_speed;
    return std::abs(denominator)>1e-8
        ? (numerator*std::cos(beta)-
           longitudinal_speed*std::sin(beta))/denominator
        : std::numeric_limits<double>::infinity();
}

struct PropagatedRigState
{
    Vec2 tractor_reference{};
    double tractor_heading{};
    Vec2 trailer_axle{};
    double trailer_heading{};
};

inline double reverse_trailer_heading_rate(
    double tractor_heading,double trailer_heading,double tractor_curvature,
    const VehicleGeometry &geometry)
{
    const double phi=stage0::wrap_angle(tractor_heading-trailer_heading);
    const double a=geometry.tractor_reference_to_hitch_m;
    const double b=geometry.tractor_reference_to_hitch_lateral_m;
    const double scale=1.0-b*tractor_curvature;
    const double numerator=scale*std::sin(phi)+
        a*tractor_curvature*std::cos(phi);
    const double longitudinal_speed=scale*std::cos(phi)-
        a*tractor_curvature*std::sin(phi);
    const double beta=std::clamp(geometry.trailer_axle_steering_rad,
        -60.0*stage0::pi/180.0,60.0*stage0::pi/180.0);
    return -(numerator-longitudinal_speed*std::tan(beta))/
        std::max(0.5,geometry.trailer_axle_to_hitch_m);
}

inline double steering_profile_curvature(double u,double k0,double k1,
                                          double k2)
{
    u=std::clamp(u,0.0,1.0);
    const double v=1.0-u;
    // Cubic control profile. The final two controls share k2, allowing a
    // non-zero terminal steering state without adding an under-constrained
    // fourth shooting variable.
    return v*v*v*k0+3.0*v*v*u*k1+3.0*v*u*u*k2+u*u*u*k2;
}

inline PropagatedRigState propagate_reverse_step(
    const PropagatedRigState &state,double curvature,double distance,
    const VehicleGeometry &geometry)
{
    const double rate0=reverse_trailer_heading_rate(
        state.tractor_heading,state.trailer_heading,curvature,geometry);
    const double tractor_mid=stage0::wrap_angle(
        state.tractor_heading-0.5*curvature*distance);
    const double trailer_mid=stage0::wrap_angle(
        state.trailer_heading+0.5*rate0*distance);
    const double rate_mid=reverse_trailer_heading_rate(
        tractor_mid,trailer_mid,curvature,geometry);
    PropagatedRigState next;
    next.tractor_heading=stage0::wrap_angle(
        state.tractor_heading-curvature*distance);
    next.trailer_heading=stage0::wrap_angle(
        state.trailer_heading+rate_mid*distance);
    next.tractor_reference=subtract(
        state.tractor_reference,multiply(forward(tractor_mid),distance));
    const auto hitch=add(add(next.tractor_reference,
        multiply(forward(next.tractor_heading),
                 geometry.tractor_reference_to_hitch_m)),
        multiply(right(next.tractor_heading),
                 geometry.tractor_reference_to_hitch_lateral_m));
    next.trailer_axle=subtract(hitch,multiply(forward(next.trailer_heading),
        geometry.trailer_axle_to_hitch_m));
    return next;
}

inline PropagatedRigState steering_profile_endpoint(
    const PlanRequest &request,double distance,double k1,double k2)
{
    PropagatedRigState state{request.tractor_start.position,
        request.tractor_start.heading_rad,request.subject_start.position,
        request.subject_start.heading_rad};
    const int steps=std::max(1,static_cast<int>(std::ceil(distance/0.10)));
    const double ds=distance/steps;
    const double k0=request.tractor_start_curvature_m_inv;
    for (int step=0;step<steps;++step)
    {
        const double u=(static_cast<double>(step)+0.5)/steps;
        state=propagate_reverse_step(state,
            steering_profile_curvature(u,k0,k1,k2),ds,request.geometry);
    }
    return state;
}

inline bool solve_three_by_three(double matrix[3][3],double rhs[3],
                                 double solution[3])
{
    for (int pivot=0;pivot<3;++pivot)
    {
        int best=pivot;
        for (int row=pivot+1;row<3;++row)
            if (std::abs(matrix[row][pivot])>std::abs(matrix[best][pivot]))
                best=row;
        if (std::abs(matrix[best][pivot])<1e-9) return false;
        if (best!=pivot)
        {
            for (int column=pivot;column<3;++column)
                std::swap(matrix[pivot][column],matrix[best][column]);
            std::swap(rhs[pivot],rhs[best]);
        }
        const double divisor=matrix[pivot][pivot];
        for (int column=pivot;column<3;++column)
            matrix[pivot][column]/=divisor;
        rhs[pivot]/=divisor;
        for (int row=0;row<3;++row)
        {
            if (row==pivot) continue;
            const double factor=matrix[row][pivot];
            for (int column=pivot;column<3;++column)
                matrix[row][column]-=factor*matrix[pivot][column];
            rhs[row]-=factor*rhs[pivot];
        }
    }
    for (int index=0;index<3;++index) solution[index]=rhs[index];
    return true;
}

inline PlanResult evaluate_steering_profile(
    const PlanRequest &request,double distance,double k1,double k2)
{
    PlanResult result;
    result.mode=request.mode;
    const double max_curvature=std::tan(request.geometry.tractor_max_steer_rad)/
        std::max(0.5,request.geometry.tractor_wheelbase_m);
    PropagatedRigState state{request.tractor_start.position,
        request.tractor_start.heading_rad,request.subject_start.position,
        request.subject_start.heading_rad};
    const int steps=std::max(1,static_cast<int>(std::ceil(distance/0.10)));
    const double ds=distance/steps;
    const double k0=std::clamp(request.tractor_start_curvature_m_inv,
                               -max_curvature,max_curvature);
    result.samples.reserve(steps+1);
    double articulation_sum=0.0;
    for (int step=0;step<=steps;++step)
    {
        PathSample sample{};
        sample.distance=step*ds;
        sample.tractor_position=state.tractor_reference;
        sample.tractor_heading=state.tractor_heading;
        sample.trailer_position=state.trailer_axle;
        sample.trailer_heading=state.trailer_heading;
        sample.hitch_position=add(state.trailer_axle,
            multiply(forward(state.trailer_heading),
                     request.geometry.trailer_axle_to_hitch_m));
        sample.articulation_angle=stage0::wrap_angle(
            state.tractor_heading-state.trailer_heading);
        result.max_articulation=std::max(result.max_articulation,
            std::abs(sample.articulation_angle));
        articulation_sum+=std::abs(sample.articulation_angle);
        if (result.max_articulation>articulation_hard_limit_rad+1e-9)
        {
            result.reason="ARTICULATION_LIMIT_EXCEEDED";
            return result;
        }
        if (request.check_self_contour&&oriented_boxes_overlap(
                tractor_contour(sample,request.geometry,true),
                trailer_contour(sample,request.geometry),
                request.geometry.contour_clearance_m))
        {
            result.reason="SELF_CONTOUR_COLLISION";
            return result;
        }
        if (request.check_known_obstacles)
            for (const auto &obstacle:request.obstacles)
                if (oriented_boxes_overlap(
                        tractor_contour(sample,request.geometry,true),obstacle,
                        request.geometry.contour_clearance_m)||
                    oriented_boxes_overlap(
                        trailer_contour(sample,request.geometry),obstacle,
                        request.geometry.contour_clearance_m))
                {
                    result.reason="KNOWN_OBSTACLE_COLLISION";
                    return result;
                }
        result.samples.push_back(sample);
        if (step==steps) break;
        const double u=(static_cast<double>(step)+0.5)/steps;
        const double curvature=steering_profile_curvature(u,k0,k1,k2);
        result.max_required_tractor_curvature=std::max(
            result.max_required_tractor_curvature,std::abs(curvature));
        if (std::abs(curvature)>max_curvature+1e-9)
        {
            result.reason="CURVATURE_PHYSICALLY_UNREACHABLE";
            return result;
        }
        const auto previous_heading=state.trailer_heading;
        state=propagate_reverse_step(state,curvature,ds,request.geometry);
        result.samples.back().trailer_curvature=stage0::wrap_angle(
            state.trailer_heading-previous_heading)/ds;
        result.max_curvature=std::max(
            result.max_curvature,
            std::abs(result.samples.back().trailer_curvature));
    }
    const auto &last=result.samples.back();
    result.final_position_error=length(subtract(
        last.trailer_position,request.target.position));
    result.final_heading_error=std::abs(stage0::wrap_angle(
        last.trailer_heading-request.target.heading_rad));
    if (result.final_position_error>0.20||
        result.final_heading_error>1.0*stage0::pi/180.0)
    {
        result.reason="CONTROL_PROFILE_ENDPOINT_MISS";
        return result;
    }
    result.path_length=distance;
    result.mean_articulation=articulation_sum/result.samples.size();
    result.cost=distance+result.mean_articulation*4.0+
        result.final_position_error*100.0+result.final_heading_error*100.0+
        (std::abs(k1)+std::abs(k2))*2.0;
    result.valid_candidate_count=1;
    result.found=true;
    result.reason="KINEMATIC_STEERING_PROFILE_FOUND";
    return result;
}

inline PlanResult plan_steering_profile_fallback(const PlanRequest &request)
{
    PlanResult best;
    best.mode=request.mode;
    const double chord=length(subtract(request.target.position,
                                       request.subject_start.position));
    const double max_curvature=std::tan(request.geometry.tractor_max_steer_rad)/
        std::max(0.5,request.geometry.tractor_wheelbase_m);
    // Try the low-effort, near-straight solution first. This is the common
    // parking-frame case and avoids spending all 25 shooting seeds per update.
    constexpr std::array<double,5> seeds{0.0,-0.06,0.06,-0.12,0.12};
    int attempts=0;
    const int seed_pair_limit=std::clamp(
        request.physical_seed_pair_limit,1,25);
    double least_residual=std::numeric_limits<double>::infinity();
    const double heading_delta=stage0::wrap_angle(
        request.target.heading_rad-request.subject_start.heading_rad);
    const auto start_reverse=reverse_direction(request.subject_start.heading_rad);
    const auto target_offset=subtract(
        request.target.position,request.subject_start.position);
    const double lateral_delta=start_reverse.y*target_offset.x-
                               start_reverse.x*target_offset.y;
    const double preferred_sign=std::abs(heading_delta)>2.0*stage0::pi/180.0
        ? (heading_delta>=0.0?1.0:-1.0)
        : (lateral_delta>=0.0?1.0:-1.0);
    const std::array<double,9> initial_fractions{
        0.0,preferred_sign*0.25,-preferred_sign*0.25,
        preferred_sign*0.50,-preferred_sign*0.50,
        preferred_sign*0.75,-preferred_sign*0.75,
        preferred_sign,-preferred_sign};
    const int initial_seed_limit=request.search_planner_initial_curvature
        ? std::clamp(request.planner_initial_curvature_seed_limit,1,9)
        : 1;
    for (int initial_index=0;initial_index<initial_seed_limit;++initial_index)
    {
        PlanRequest shooting_request=request;
        shooting_request.tractor_start_curvature_m_inv=
            request.search_planner_initial_curvature
                ? initial_fractions[initial_index]*max_curvature
                : request.tractor_start_curvature_m_inv;
        int seed_pairs=0;
        for (std::size_t seed1_index=0;
             seed1_index<seeds.size()&&seed_pairs<seed_pair_limit;
             ++seed1_index)
        for (std::size_t seed2_index=0;
             seed2_index<seeds.size()&&seed_pairs<seed_pair_limit;
             ++seed2_index)
        {
        ++seed_pairs;
        const double seed1=seeds[seed1_index];
        const double seed2=seeds[seed2_index];
        double variables[3]{std::max(0.5,chord),seed1,seed2};
        for (int iteration=0;iteration<14;++iteration)
        {
            ++attempts;
            const auto endpoint=steering_profile_endpoint(
                shooting_request,variables[0],variables[1],variables[2]);
            double residual[3]{
                endpoint.trailer_axle.x-request.target.position.x,
                endpoint.trailer_axle.y-request.target.position.y,
                stage0::wrap_angle(endpoint.trailer_heading-
                                   request.target.heading_rad)*5.0};
            const double norm=std::hypot(residual[0],residual[1])+
                              std::abs(residual[2]);
            if (norm<least_residual)
            {
                least_residual=norm;
                best.final_position_error=std::hypot(residual[0],residual[1]);
                best.final_heading_error=std::abs(residual[2])/5.0;
                best.selected_d0=variables[0];
                best.selected_d1=variables[1];
                best.max_required_tractor_curvature=variables[2];
            }
            if (norm<0.03)
            {
                auto candidate=evaluate_steering_profile(
                    shooting_request,variables[0],variables[1],variables[2]);
                if (candidate.found)
                {
                    candidate.candidate_count=attempts;
                    candidate.known_obstacle_count=
                        static_cast<int>(request.obstacles.size());
                    return candidate;
                }
                break;
            }
            double jacobian[3][3]{};
            constexpr double eps[3]{0.05,0.001,0.001};
            for (int column=0;column<3;++column)
            {
                auto perturbed=std::array<double,3>{
                    variables[0],variables[1],variables[2]};
                perturbed[column]+=eps[column];
                const auto shifted=steering_profile_endpoint(
                    shooting_request,perturbed[0],perturbed[1],perturbed[2]);
                const double shifted_residual[3]{
                    shifted.trailer_axle.x-request.target.position.x,
                    shifted.trailer_axle.y-request.target.position.y,
                    stage0::wrap_angle(shifted.trailer_heading-
                                       request.target.heading_rad)*5.0};
                for (int row=0;row<3;++row)
                    jacobian[row][column]=
                        (shifted_residual[row]-residual[row])/eps[column];
            }
            double rhs[3]{-residual[0],-residual[1],-residual[2]};
            double correction[3]{};
            if (!solve_three_by_three(jacobian,rhs,correction)) break;
            variables[0]=std::clamp(variables[0]+0.65*correction[0],
                                    chord*0.70,chord*2.50);
            variables[1]=std::clamp(variables[1]+0.65*correction[1],
                                    -max_curvature,max_curvature);
            variables[2]=std::clamp(variables[2]+0.65*correction[2],
                                    -max_curvature,max_curvature);
        }
        }
    }
    best.candidate_count=attempts;
    best.known_obstacle_count=static_cast<int>(request.obstacles.size());
    if (!best.found) best.reason="NO_PHYSICAL_STEERING_PROFILE";
    return best;
}

// Reconstruct the next tractor pose from a prescribed hitch point. The root
// equation enforces zero lateral displacement at the tractor rear axle. It
// therefore retains the real longitudinal and lateral fifth-wheel offsets
// instead of treating the hitch as if it were the rear-axle centre.
inline bool solve_next_tractor_pose(
    Vec2 previous_reference,double previous_heading,Vec2 next_hitch,
    const VehicleGeometry &geometry,double &next_heading,
    Vec2 &next_reference)
{
    auto residual=[&](double candidate,Vec2 *reference=nullptr) {
        const auto next=tractor_reference_from_hitch(
            next_hitch,candidate,geometry);
        if (reference) *reference=next;
        const double midpoint=stage0::wrap_angle(previous_heading+
            0.5*stage0::wrap_angle(candidate-previous_heading));
        return dot(subtract(next,previous_reference),right(midpoint));
    };

    double candidate=previous_heading;
    constexpr double epsilon=1e-5;
    for (int iteration=0;iteration<12;++iteration)
    {
        const double value=residual(candidate);
        if (!std::isfinite(value)) return false;
        if (std::abs(value)<1e-7) break;
        const double derivative=(residual(candidate+epsilon)-
                                 residual(candidate-epsilon))/(2.0*epsilon);
        if (!std::isfinite(derivative)||std::abs(derivative)<1e-8)
            return false;
        const double correction=std::clamp(value/derivative,-0.25,0.25);
        candidate=stage0::wrap_angle(candidate-correction);
    }
    Vec2 reference{};
    const double final_residual=residual(candidate,&reference);
    if (!std::isfinite(final_residual)||std::abs(final_residual)>1e-4||
        !finite(reference))
        return false;
    const auto delta=subtract(reference,previous_reference);
    const double midpoint=stage0::wrap_angle(previous_heading+
        0.5*stage0::wrap_angle(candidate-previous_heading));
    if (length(delta)<1e-7||dot(delta,forward(midpoint))>=-1e-6)
        return false;
    next_heading=candidate;
    next_reference=reference;
    return true;
}

template <typename Curve>
inline PlanResult evaluate_candidate(const PlanRequest &request,
                                     const Curve &curve,
                                     double d0, double d1)
{
    PlanResult result;
    result.mode = request.mode;
    result.selected_d0 = d0;
    result.selected_d1 = d1;
    double total_length{};
    const auto parameters = arc_length_parameters(curve, request.sample_spacing_m,
                                                   total_length);
    if (total_length > maximum_candidate_curve_length_m)
    {
        result.reason = "PATH_LENGTH_LIMIT_EXCEEDED";
        return result;
    }
    if (parameters.size() < 2)
    {
        result.reason = "DEGENERATE_CURVE";
        return result;
    }

    result.samples.reserve(parameters.size());
    const double lt = request.geometry.trailer_axle_to_hitch_m;
    // The inverse pose solve and 0.10 m arc-length resampling introduce a
    // small finite-difference error at the steering boundary. Allow 0.05 deg
    // of numerical headroom; this is far below telemetry/model uncertainty
    // and is not an operational steering-limit relaxation.
    constexpr double steering_numerical_tolerance_rad=
        0.05*stage0::pi/180.0;
    const double max_tractor_curvature = std::tan(
        request.geometry.tractor_max_steer_rad+
        steering_numerical_tolerance_rad) /
                                         std::max(0.5,request.geometry.tractor_wheelbase_m);
    double articulation_sum = 0.0;
    double articulation_cost = 0.0;
    double curvature_cost = 0.0;
    double previous_curvature = 0.0;
    bool have_previous_curvature = false;

    for (std::size_t index=0; index<parameters.size(); ++index)
    {
        const double t = parameters[index];
        const Vec2 position = evaluate(curve,t);
        const Vec2 first = derivative(curve,t);
        const Vec2 second = second_derivative(curve,t);
        if (length(first)<1e-7)
        {
            result.reason="DEGENERATE_CURVE_TANGENT";
            return result;
        }
        const double motion_heading = travel_heading(first);
        const double rolling_heading = stage0::wrap_angle(
            motion_heading+stage0::pi);
        const double beta=request.mode==PlannerMode::trailer_to_parking_target
            ? std::clamp(request.geometry.trailer_axle_steering_rad,
                -60.0*stage0::pi/180.0,60.0*stage0::pi/180.0)
            : 0.0;
        const double physical_heading=stage0::wrap_angle(
            rolling_heading-beta);
        const double geometric_heading_rate = heading_derivative_per_metre(first,second);
        if (!std::isfinite(geometric_heading_rate))
        {
            result.reason="NON_FINITE_CURVATURE";
            return result;
        }
        PathSample sample{};
        sample.distance = total_length * static_cast<double>(index) /
                          static_cast<double>(parameters.size()-1);
        if (request.mode==PlannerMode::tractor_to_target_trailer)
        {
            if (std::abs(geometric_heading_rate)>max_tractor_curvature+1e-9)
            {
                result.reason="CURVATURE_PHYSICALLY_UNREACHABLE";
                return result;
            }
            sample.tractor_position=position;
            sample.tractor_heading=physical_heading;
            sample.hitch_position=position;
            sample.trailer_position=position;
            sample.trailer_heading=physical_heading;
            sample.trailer_curvature=geometric_heading_rate;
        }
        else
        {
            const double reverse_curvature=-geometric_heading_rate;
            if (!std::isfinite(reverse_curvature))
            {
                result.reason="NON_FINITE_CURVATURE";
                return result;
            }
            sample.trailer_position=position;
            sample.trailer_heading=physical_heading;
            sample.trailer_curvature=reverse_curvature;
            sample.hitch_position=add(position,
                multiply(forward(physical_heading),lt));
            if (index==0)
            {
                sample.tractor_heading=request.tractor_start.heading_rad;
                sample.tractor_position=tractor_reference_from_hitch(
                    sample.hitch_position,sample.tractor_heading,
                    request.geometry);
            }
            else
            {
                const auto &previous=result.samples.back();
                if (!solve_next_tractor_pose(
                        previous.tractor_position,previous.tractor_heading,
                        sample.hitch_position,request.geometry,
                        sample.tractor_heading,sample.tractor_position))
                {
                    result.reason="ARTICULATED_POSE_SOLVE_FAILED";
                    return result;
                }
                const double tractor_distance=length(subtract(
                    sample.tractor_position,previous.tractor_position));
                const double tractor_curvature=stage0::wrap_angle(
                    sample.tractor_heading-previous.tractor_heading)/
                    std::max(1e-7,tractor_distance);
                result.max_required_tractor_curvature=std::max(
                    result.max_required_tractor_curvature,
                    std::abs(tractor_curvature));
                if (!std::isfinite(tractor_curvature)||
                    std::abs(tractor_curvature)>max_tractor_curvature+1e-9)
                {
                    result.failure_distance=sample.distance;
                    result.reason="CURVATURE_PHYSICALLY_UNREACHABLE";
                    return result;
                }
            }
            const double phi=stage0::wrap_angle(
                sample.tractor_heading-sample.trailer_heading);
            if (std::abs(phi)>articulation_hard_limit_rad+1e-9)
            {
                result.reason="ARTICULATION_LIMIT_EXCEEDED";
                return result;
            }
            sample.articulation_angle=phi;
            articulation_sum+=std::abs(phi);
            articulation_cost+=articulation_penalty(phi);
        }
        result.max_articulation=std::max(result.max_articulation,
                                         std::abs(sample.articulation_angle));
        result.max_curvature=std::max(result.max_curvature,
                                      std::abs(sample.trailer_curvature));
        curvature_cost+=sample.trailer_curvature*sample.trailer_curvature;
        if (have_previous_curvature)
            result.curvature_change+=std::abs(sample.trailer_curvature-previous_curvature);
        previous_curvature=sample.trailer_curvature;
        have_previous_curvature=true;
        result.samples.push_back(sample);
    }

    result.initial_tractor_heading_error=std::abs(stage0::wrap_angle(
        result.samples.front().tractor_heading-request.tractor_start.heading_rad));
    result.initial_tractor_position_error=length(subtract(
        result.samples.front().tractor_position,
        request.tractor_start.position));
    std::string hard_reason;
    if (request.mode==PlannerMode::trailer_to_parking_target &&
        !validate_hard_limits(result.samples,hard_reason))
    {
        result.reason=hard_reason;
        return result;
    }

    if (request.mode==PlannerMode::trailer_to_parking_target &&
        (result.initial_tractor_heading_error>0.75*stage0::pi/180.0||
         result.initial_tractor_position_error>0.05))
    {
        result.reason="START_STATE_DISCONTINUITY";
        return result;
    }

    // The reconstructed tractor reference must move backward along its own
    // physical-front direction for every non-degenerate interval.
    for (std::size_t index=1; index<result.samples.size(); ++index)
    {
        const auto delta=subtract(result.samples[index].tractor_position,
                                  result.samples[index-1].tractor_position);
        if (length(delta)<1e-7) continue;
        const double midpoint_heading=stage0::wrap_angle(
            result.samples[index-1].tractor_heading+
            stage0::wrap_angle(result.samples[index].tractor_heading-
                               result.samples[index-1].tractor_heading)*0.5);
        if (dot(delta,forward(midpoint_heading))>=-1e-5)
        {
            result.reason="FORWARD_MOTION_REQUIRED";
            return result;
        }
    }

    for (const auto &sample:result.samples)
    {
        const bool connected=request.mode==PlannerMode::trailer_to_parking_target;
        const auto tractor_box=tractor_contour(sample,request.geometry,connected);
        if (connected&&request.check_self_contour)
        {
            const auto trailer_box=trailer_contour(sample,request.geometry);
            if (oriented_boxes_overlap(tractor_box,trailer_box,
                                      request.geometry.contour_clearance_m))
            {
                result.reason="SELF_CONTOUR_COLLISION";
                return result;
            }
        }
        if (request.check_known_obstacles)
        {
            for (const auto &obstacle:request.obstacles)
            {
                if (oriented_boxes_overlap(tractor_box,obstacle,
                                          request.geometry.contour_clearance_m))
                {
                    result.reason="KNOWN_OBSTACLE_COLLISION";
                    return result;
                }
                if (connected&&oriented_boxes_overlap(
                    trailer_contour(sample,request.geometry),obstacle,
                    request.geometry.contour_clearance_m))
                {
                    result.reason="KNOWN_OBSTACLE_COLLISION";
                    return result;
                }
            }
        }
    }

    const auto &last=result.samples.back();
    const Vec2 final_position=request.mode==PlannerMode::tractor_to_target_trailer
        ? last.tractor_position:last.trailer_position;
    const double final_heading=request.mode==PlannerMode::tractor_to_target_trailer
        ? last.tractor_heading:last.trailer_heading;
    result.final_position_error=length(subtract(final_position,request.target.position));
    result.final_heading_error=std::abs(stage0::wrap_angle(final_heading-
                                                           request.target.heading_rad));
    result.path_length=total_length;
    result.mean_articulation=result.samples.empty()?0.0:
        articulation_sum/static_cast<double>(result.samples.size());
    result.initial_tractor_heading_error=std::abs(stage0::wrap_angle(
        result.samples.front().tractor_heading-request.tractor_start.heading_rad));
    result.initial_tractor_position_error=length(subtract(
        result.samples.front().tractor_position,
        request.tractor_start.position));
    result.cost=total_length+articulation_cost*0.35+
        curvature_cost*1.5+result.curvature_change*3.0+
        result.initial_tractor_heading_error*8.0+
        result.initial_tractor_position_error*40.0+
        result.final_position_error*1000.0+result.final_heading_error*500.0;
    result.found=true;
    result.reason="KINEMATIC_PATH_FOUND";
    return result;
}

#if 0 // Retained only as rejected research; not part of the runtime planner.
inline PlanResult plan_extreme_articulation_recovery(
    const PlanRequest &request,double actual_articulation)
{
    PlanResult rejected;
    rejected.mode=request.mode;
    const double initial_abs=std::abs(actual_articulation);
    if (initial_abs>articulation_emergency_limit_rad+1e-9)
    {
        rejected.candidate_count=1;
        rejected.articulation_rejections=1;
        rejected.reason="ARTICULATION_BEYOND_RECOVERABLE_RANGE";
        return rejected;
    }

    const double wheelbase=std::max(0.5,
        request.geometry.tractor_wheelbase_m);
    const double trailer_length=std::max(0.5,
        request.geometry.trailer_axle_to_hitch_m);
    constexpr double recovery_target_rad=55.0*stage0::pi/180.0;
    constexpr double step_m=0.10;
    constexpr double maximum_recovery_distance_m=18.0;
    constexpr std::array<double,4> steering_fractions{1.0,0.9,0.8,0.7};
    PlanResult best;
    best.mode=request.mode;
    int obstacle_rejections=0,self_rejections=0,articulation_rejections=0;

    for (const double fraction:steering_fractions)
    {
        PlanResult result;
        result.mode=request.mode;
        const double recovery_steer=std::copysign(
            request.geometry.tractor_max_steer_rad*fraction,
            actual_articulation);
        PathSample current{};
        current.tractor_position=request.tractor_start.position;
        current.tractor_heading=request.tractor_start.heading_rad;
        current.trailer_position=request.subject_start.position;
        current.trailer_heading=request.subject_start.heading_rad;
        current.articulation_angle=actual_articulation;
        const double tractor_curvature=std::tan(recovery_steer)/wheelbase;
        const double hitch_longitudinal=
            request.geometry.tractor_reference_to_hitch_m;
        const double hitch_lateral=
            request.geometry.tractor_reference_to_hitch_lateral_m;
        current.trailer_curvature=articulated_trailer_curvature(
            current.articulation_angle,tractor_curvature,request.geometry);
        current.hitch_position=add(
            add(current.tractor_position,
                multiply(forward(current.tractor_heading),
                         hitch_longitudinal)),
            multiply(right(current.tractor_heading),hitch_lateral));
        result.samples.push_back(current);
        double articulation_sum=initial_abs;
        bool candidate_rejected=false;

        for (double distance=step_m;
             distance<=maximum_recovery_distance_m+1e-9;
             distance+=step_m)
        {
        const double next_tractor_heading=stage0::wrap_angle(
            current.tractor_heading-tractor_curvature*step_m);
        const double midpoint_tractor_heading=stage0::wrap_angle(
            current.tractor_heading+stage0::wrap_angle(
                next_tractor_heading-current.tractor_heading)*0.5);
        const double current_scale=1.0-hitch_lateral*tractor_curvature;
        const double current_trailer_rate=-(
            current_scale*std::sin(current.articulation_angle)+
            hitch_longitudinal*tractor_curvature*
                std::cos(current.articulation_angle))/trailer_length;
        const double midpoint_trailer_heading=stage0::wrap_angle(
            current.trailer_heading+0.5*current_trailer_rate*step_m);
        const double midpoint_articulation=stage0::wrap_angle(
            midpoint_tractor_heading-midpoint_trailer_heading);
        const double midpoint_scale=1.0-hitch_lateral*tractor_curvature;
        const double midpoint_trailer_rate=-(
            midpoint_scale*std::sin(midpoint_articulation)+
            hitch_longitudinal*tractor_curvature*
                std::cos(midpoint_articulation))/trailer_length;
        const double next_trailer_heading=stage0::wrap_angle(
            current.trailer_heading+midpoint_trailer_rate*step_m);
        PathSample next{};
        next.distance=distance;
        next.tractor_position=subtract(current.tractor_position,
            multiply(forward(midpoint_tractor_heading),step_m));
        next.tractor_heading=next_tractor_heading;
        next.trailer_heading=next_trailer_heading;
        next.articulation_angle=stage0::wrap_angle(
            next.tractor_heading-next.trailer_heading);
        next.trailer_curvature=articulated_trailer_curvature(
            next.articulation_angle,tractor_curvature,request.geometry);
        next.hitch_position=add(
            add(next.tractor_position,
                multiply(forward(next.tractor_heading),
                         hitch_longitudinal)),
            multiply(right(next.tractor_heading),hitch_lateral));
        next.trailer_position=subtract(next.hitch_position,
            multiply(forward(next.trailer_heading),trailer_length));

        if (std::abs(next.articulation_angle)>
            std::abs(current.articulation_angle)+1e-6||
            next.articulation_angle*actual_articulation<=0.0)
        {
            result.reason="EXTREME_ARTICULATION_NOT_RECOVERING";
            candidate_rejected=true;
            break;
        }
        if (request.check_known_obstacles)
        {
            const auto tractor_box=tractor_contour(next,request.geometry,true);
            const auto trailer_box=trailer_contour(next,request.geometry);
            for (const auto &obstacle:request.obstacles)
            {
                if (oriented_boxes_overlap(tractor_box,obstacle,
                                           request.geometry.contour_clearance_m)||
                    oriented_boxes_overlap(trailer_box,obstacle,
                                           request.geometry.contour_clearance_m))
                {
                    result.reason="KNOWN_OBSTACLE_COLLISION";
                    candidate_rejected=true;
                    break;
                }
            }
            if (candidate_rejected) break;
        }
        result.samples.push_back(next);
        articulation_sum+=std::abs(next.articulation_angle);
        current=next;
        if (std::abs(current.articulation_angle)<=recovery_target_rad)
            break;
        }

        if (!candidate_rejected&&
            std::abs(current.articulation_angle)>recovery_target_rad+1e-9)
        {
            result.reason="EXTREME_ARTICULATION_RECOVERY_INCOMPLETE";
            candidate_rejected=true;
        }
        if (!candidate_rejected&&request.check_self_contour&&
            oriented_boxes_overlap(tractor_contour(current,request.geometry,true),
                                   trailer_contour(current,request.geometry),
                                   request.geometry.contour_clearance_m))
        {
            result.reason="SELF_CONTOUR_COLLISION";
            candidate_rejected=true;
        }
        if (candidate_rejected)
        {
            if (result.reason=="KNOWN_OBSTACLE_COLLISION")
                ++obstacle_rejections;
            else if (result.reason=="SELF_CONTOUR_COLLISION")
                ++self_rejections;
            else
                ++articulation_rejections;
            continue;
        }
        result.valid_candidate_count=1;
        result.path_length=current.distance;
        result.max_articulation=initial_abs;
        result.mean_articulation=articulation_sum/result.samples.size();
        result.max_curvature=std::abs(std::tan(recovery_steer)/wheelbase);
        result.final_position_error=length(subtract(
            current.trailer_position,request.target.position));
        result.final_heading_error=std::abs(stage0::wrap_angle(
            current.trailer_heading-request.target.heading_rad));
        result.cost=result.path_length+result.mean_articulation*10.0+
                    fraction*0.25;
        result.found=true;
        result.reason="EXTREME_ARTICULATION_RECOVERY_ACTIVE";
        if (!best.found||result.cost<best.cost) best=std::move(result);
    }

    best.candidate_count=static_cast<int>(steering_fractions.size());
    best.obstacle_collision_rejections=obstacle_rejections;
    best.self_collision_rejections=self_rejections;
    best.articulation_rejections=articulation_rejections;
    best.known_obstacle_count=static_cast<int>(request.obstacles.size());
    if (!best.found)
    {
        best.valid_candidate_count=0;
        best.reason="NO_EXTREME_ARTICULATION_RECOVERY_PATH";
    }
    return best;
}
#endif

inline PlanResult plan(const PlanRequest &request)
{
    PlanResult best;
    best.mode=request.mode;
    if (request.mode==PlannerMode::inactive)
    {
        best.reason="PATH_PLANNER_INACTIVE";
        return best;
    }
    if (!finite(request.tractor_start.position)||!finite(request.subject_start.position)||
        !finite(request.target.position)||!std::isfinite(request.tractor_start.heading_rad)||
        !std::isfinite(request.subject_start.heading_rad)||
        !std::isfinite(request.target.heading_rad))
    {
        best.reason="NON_FINITE_INPUT_POSE";
        return best;
    }
    const double chord=length(subtract(request.target.position,
                                       request.subject_start.position));
    if (chord<0.20)
    {
        best.reason="TARGET_TOO_CLOSE_FOR_FIXED_PATH";
        return best;
    }
    if (chord>maximum_planning_chord_m)
    {
        best.reason="TARGET_OUT_OF_LOCAL_TELEMETRY_RANGE";
        return best;
    }
    const double beta=request.mode==PlannerMode::trailer_to_parking_target
        ? std::clamp(request.geometry.trailer_axle_steering_rad,
            -60.0*stage0::pi/180.0,60.0*stage0::pi/180.0)
        : 0.0;
    const Vec2 start_reverse=reverse_direction(
        request.subject_start.heading_rad+beta);
    const Vec2 target_reverse=reverse_direction(
        request.target.heading_rad+beta);
    int obstacle_rejections=0,self_rejections=0,start_rejections=0;
    int curvature_rejections=0,articulation_rejections=0;
    int forward_rejections=0,other_rejections=0;
    double least_rejected_tractor_curvature=
        std::numeric_limits<double>::infinity();
    double least_rejected_failure_distance=0.0;
    int candidate_count=0,valid_candidate_count=0;
    const auto consider_candidate=[&](const auto &curve,
                                      double d0,double d1)
    {
        auto candidate=evaluate_candidate(request,curve,d0,d1);
        ++candidate_count;
        if (!candidate.found)
        {
            if (candidate.reason=="KNOWN_OBSTACLE_COLLISION")
                ++obstacle_rejections;
            else if (candidate.reason=="SELF_CONTOUR_COLLISION")
                ++self_rejections;
            else if (candidate.reason=="START_STATE_DISCONTINUITY")
                ++start_rejections;
            else if (candidate.reason=="CURVATURE_PHYSICALLY_UNREACHABLE" ||
                     candidate.reason=="NON_FINITE_CURVATURE")
            {
                ++curvature_rejections;
                if (candidate.max_required_tractor_curvature<
                    least_rejected_tractor_curvature)
                {
                    least_rejected_tractor_curvature=
                        candidate.max_required_tractor_curvature;
                    least_rejected_failure_distance=candidate.failure_distance;
                    best.selected_d0=candidate.selected_d0;
                    best.selected_d1=candidate.selected_d1;
                }
            }
            else if (candidate.reason=="ARTICULATION_LIMIT_EXCEEDED")
                ++articulation_rejections;
            else if (candidate.reason=="FORWARD_MOTION_REQUIRED")
                ++forward_rejections;
            else
                ++other_rejections;
            return;
        }
        ++valid_candidate_count;
        if (!best.found||candidate.cost<best.cost)
            best=std::move(candidate);
    };
    const int grid=std::clamp(request.candidate_grid_size,4,12);
    for (int i=0;i<grid;++i)
    {
        const double d0=chord*(0.20+1.35*static_cast<double>(i)/(grid-1));
        for (int j=0;j<grid;++j)
        {
            const double d1=chord*(0.20+1.35*static_cast<double>(j)/(grid-1));
            const CubicBezier curve{
                request.subject_start.position,
                add(request.subject_start.position,multiply(start_reverse,d0)),
                subtract(request.target.position,multiply(target_reverse,d1)),
                request.target.position};
            consider_candidate(curve,d0,d1);
        }
    }
    // Add one analytically start-calibrated candidate per target handle. Its
    // initial trailer curvature exactly represents the measured articulation,
    // avoiding the former five-degree start approximation and first-segment
    // kink while retaining the deterministic cubic family.
    if (request.mode==PlannerMode::trailer_to_parking_target)
    {
        const double actual_articulation=stage0::wrap_angle(
            request.tractor_start.heading_rad-
            request.subject_start.heading_rad);
        const double required_reverse_curvature=
            articulated_trailer_curvature(actual_articulation,
                request.tractor_start_curvature_m_inv,request.geometry);
        if (std::abs(required_reverse_curvature)>1e-7)
        {
            for (int j=0;j<grid;++j)
            {
                const double d1=chord*(
                    0.20+1.35*static_cast<double>(j)/(grid-1));
                const Vec2 p2=subtract(
                    request.target.position,multiply(target_reverse,d1));
                const Vec2 offset=subtract(
                    p2,request.subject_start.position);
                const double heading_cross=
                    start_reverse.y*offset.x-start_reverse.x*offset.y;
                const double d0_squared=
                    -(2.0/3.0)*heading_cross/
                    required_reverse_curvature;
                if (!(d0_squared>0.0)||!std::isfinite(d0_squared))
                    continue;
                const double d0=std::sqrt(d0_squared);
                if (d0<0.05||d0>chord*4.0) continue;
                const CubicBezier curve{
                    request.subject_start.position,
                    add(request.subject_start.position,
                        multiply(start_reverse,d0)),
                    p2,request.target.position};
                consider_candidate(curve,d0,d1);
            }
        }

        if (!best.found&&request.allow_steering_profile_fallback)
        {
            auto physical=plan_steering_profile_fallback(request);
            if (physical.found) return physical;
        }

        // Do not add a large display-curve family here. If the compact cubic
        // search cannot satisfy the measured start state, the bounded physical
        // steering-profile solve above is both more faithful and much cheaper
        // than enumerating another 1,296 curves every 100 ms.
    }
    best.candidate_count=candidate_count;
    best.valid_candidate_count=valid_candidate_count;
    best.obstacle_collision_rejections=obstacle_rejections;
    best.self_collision_rejections=self_rejections;
    best.start_state_rejections=start_rejections;
    best.curvature_rejections=curvature_rejections;
    best.articulation_rejections=articulation_rejections;
    best.forward_motion_rejections=forward_rejections;
    best.other_rejections=other_rejections;
    best.known_obstacle_count=static_cast<int>(request.obstacles.size());
    if (!best.found)
    {
        best.valid_candidate_count=0;
        // Avoid reporting whichever failure happened to occur for the last
        // grid cell. The per-class counters preserve the actual evidence.
        best.reason="NO_CANDIDATE_PASSED_ALL_CONSTRAINTS";
        if (std::isfinite(least_rejected_tractor_curvature))
        {
            best.max_required_tractor_curvature=
                least_rejected_tractor_curvature;
            best.failure_distance=least_rejected_failure_distance;
        }
    }
    return best;
}

inline std::array<Pose2,12> parking_terminal_checkpoints(
    const PathSample &live,const Pose2 &frame,const VehicleGeometry &geometry)
{
    const auto longitudinal=forward(frame.heading_rad);
    const auto lateral_axis=right(frame.heading_rad);
    const Vec2 nominal=add(frame.position,multiply(
        longitudinal,geometry.trailer_length_m*0.5));
    const double lateral_relief=std::clamp(dot(
        subtract(live.trailer_position,nominal),lateral_axis),-0.35,0.35);
    const double heading_relief=std::clamp(stage0::wrap_angle(
        live.trailer_heading-frame.heading_rad),
        -6.0*stage0::pi/180.0,6.0*stage0::pi/180.0);
    const auto pose=[&](double longitudinal_offset,double lateral,
                       double heading) {
        return Pose2{add(add(nominal,multiply(
                             longitudinal,longitudinal_offset)),
                         multiply(lateral_axis,lateral)),
                     stage0::wrap_angle(frame.heading_rad+heading)};
    };
    return {
        pose(0.0,0.0,heading_relief*0.5),
        pose(0.0,0.0,heading_relief),
        pose(0.0,lateral_relief*0.5,heading_relief*0.5),
        pose(0.0,lateral_relief,heading_relief),
        pose(0.35,0.0,heading_relief),
        pose(-0.15,0.0,heading_relief),
        pose(0.70,0.0,heading_relief),
        pose(-0.30,0.0,heading_relief),
        pose(0.35,lateral_relief*0.5,heading_relief),
        pose(-0.15,lateral_relief*0.5,heading_relief),
        pose(0.70,lateral_relief,heading_relief),
        pose(-0.30,lateral_relief,heading_relief)};
}

inline PlanResult plan_parking_terminal_region(
    const PlanRequest &base_request,const PathSample &live,const Pose2 &frame)
{
    PlanRequest nominal=base_request;
    nominal.target=parking_entry_pose(live,frame,base_request.geometry,
                                       0.0,0.0,0.0);
    if (length(subtract(nominal.target.position,
                        base_request.subject_start.position))>
        maximum_planning_chord_m)
    {
        PlanResult rejected;
        rejected.mode=base_request.mode;
        rejected.reason="TARGET_OUT_OF_LOCAL_TELEMETRY_RANGE";
        return rejected;
    }
    nominal.allow_steering_profile_fallback=false;
    auto result=plan(nominal);
    int work=result.candidate_count;
    if (result.found) return result;

    for (const auto &checkpoint:
         parking_terminal_checkpoints(live,frame,base_request.geometry))
    {
        PlanRequest attempt=base_request;
        attempt.target=checkpoint;
        attempt.candidate_grid_size=4;
        attempt.allow_steering_profile_fallback=false;
        auto cubic=plan(attempt);
        work+=cubic.candidate_count;
        if (cubic.found)
        {
            cubic.candidate_count=work;
            cubic.reason="PARKING_REGION_CUBIC_FOUND";
            return cubic;
        }
        attempt.physical_seed_pair_limit=1;
        auto physical=plan_steering_profile_fallback(attempt);
        work+=physical.candidate_count;
        if (physical.found)
        {
            physical.candidate_count=work;
            physical.reason="PARKING_REGION_PHYSICAL_FOUND";
            return physical;
        }
    }
    result.candidate_count=work;
    result.reason="NO_PARKING_TERMINAL_REGION_PATH";
    return result;
}
}
