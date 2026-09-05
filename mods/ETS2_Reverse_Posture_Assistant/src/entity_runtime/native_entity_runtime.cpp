#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <scssdk_telemetry.h>
#include <common\scssdk_telemetry_common_configs.h>
#include <common\scssdk_telemetry_truck_common_channels.h>
#include <common\scssdk_telemetry_trailer_common_channels.h>
#include <MinHook.h>

#include "build_profiles.hpp"
#include "..\world_runtime\reverse_kinematics.hpp"

namespace
{
constexpr bool native_entity_backend_quarantined = false;
constexpr bool single_entity_diagnostic = false;
constexpr bool independent_world_entity_enabled = false;
constexpr bool trace_all_model_loads = true;
constexpr std::size_t maximum_traced_model_paths = 8192;
// Hook isolation stage: observe the accessory resource load only.  All hooks
// that detach nodes, replace transforms, or modify final draw records remain
// disabled until this read-only stage has passed an in-game stability test.
constexpr bool lifecycle_hooks_enabled = true;
constexpr bool model_load_hook_only = true;
constexpr char accessory_model_path[] = "/model/reverse_assist/accessory_anchor.pmd";
constexpr std::uintptr_t model_activate_rva = 0x015288d0;
constexpr std::uintptr_t model_transfer_rva = 0x0032cfc0;
constexpr std::uintptr_t model_parameter_init_rva = 0x0040cc30;
constexpr std::uintptr_t vehicle_accessory_collect_rva = 0x00648640;
constexpr std::uintptr_t vehicle_addon_finalize_rva = 0x00648e80;
constexpr std::uintptr_t render_entry_populate_rva = 0x00311bb0;
constexpr std::uintptr_t final_base_model_create_rva = 0x015174d0;
constexpr std::uintptr_t final_base_model_create_return_rva = 0x00649230;
constexpr std::uintptr_t final_model_create_rva = 0x01517420;
constexpr std::uintptr_t final_model_create_return_rva = 0x00649fdc;
constexpr std::uintptr_t final_accessory_insert_rva = 0x00537740;
constexpr std::uintptr_t final_accessory_insert_return_rva = 0x0064a591;
constexpr std::uintptr_t set_parent_rva = 0x013149c0;
constexpr std::uintptr_t set_transform_rva = 0x01314a80;
// The map file grid is 4000 metres, but Prism's runtime fplacement_t uses
// 512-metre floating-origin cells. The exact 1.60.1.7 transform helpers
// reference 512.0f when combining the signed cell indices with local X/Z.
constexpr float sector_size = 512.0f;
constexpr std::size_t maximum_entities = single_entity_diagnostic ? 1 : 64;
constexpr ULONGLONG driving_scene_warmup_ms = 5000;
constexpr ULONGLONG final_drawable_warmup_ms = 5000;

#pragma pack(push, 1)
struct PrismTransform
{
    float x;
    float y;
    float z;
    std::int16_t sector_x;
    std::int16_t sector_z;
    float qw;
    float qx;
    float qy;
    float qz;
};
#pragma pack(pop)
static_assert(sizeof(PrismTransform) == 32);

constexpr std::size_t maximum_config_wheels = 32;
constexpr std::uintptr_t truck_wheel_on_ground_context = 0x1000;
constexpr std::uintptr_t truck_wheel_lift_context = 0x1100;
constexpr std::uintptr_t truck_wheel_steering_context = 0x1200;
constexpr std::uintptr_t trailer_wheel_on_ground_context = 0x2000;
constexpr std::uintptr_t trailer_wheel_lift_context = 0x2100;
constexpr std::uintptr_t trailer_wheel_steering_context = 0x2200;

struct WheelConfiguration
{
    scs_value_fvector_t position{};
    bool position_valid = false;
    bool steerable = false;
    bool steerable_valid = false;
    bool liftable = false;
    bool liftable_valid = false;
};

struct WheelTelemetry
{
    bool on_ground = true;
    bool on_ground_valid = false;
    float lift = 0.0f;
    bool lift_valid = false;
    float steering = 0.0f;
    bool steering_valid = false;
};

struct VehicleConfiguration
{
    scs_value_fvector_t hook{};
    bool hook_valid = false;
    scs_u32_t wheel_count = 0;
    std::array<WheelConfiguration, maximum_config_wheels> wheels{};
};

struct Telemetry
{
    scs_value_dplacement_t truck{};
    scs_value_dplacement_t trailer{};
    VehicleConfiguration truck_configuration{};
    VehicleConfiguration trailer_configuration{};
    std::array<WheelTelemetry, maximum_config_wheels> truck_wheels{};
    std::array<WheelTelemetry, maximum_config_wheels> trailer_wheels{};
    float steering = 0.0f;
    std::int32_t gear = 0;
    bool truck_valid = false;
    bool trailer_valid = false;
    bool trailer_connected = false;
    bool driving = false;
};

using ModelLoad = std::uint64_t *(__fastcall *)(std::uint64_t *, const char **,
                                                const std::uint64_t *, const std::uint64_t *, char);
using ModelActivate = void(__fastcall *)(std::uint64_t *, std::uint64_t);
using ModelTransfer = std::uint64_t *(__fastcall *)(std::uint64_t *, std::uint64_t *);
using ModelParameterInit = void(__fastcall *)(std::uint64_t *);
using ModelFinalize = void(__fastcall *)(std::uint64_t *);
using VehicleAccessoryCollect = void(__fastcall *)(
    std::uint64_t, std::uint64_t *, std::uint64_t, char, std::uint8_t);
using VehicleAddonFinalize = void(__fastcall *)(
    std::uint64_t, std::uint64_t *, std::uint64_t, std::uint64_t *,
    std::uint64_t, std::uint64_t, std::uint64_t, char, char);
using RenderEntryPopulate = void(__fastcall *)(std::uint64_t, std::uint64_t *);
using FinalBaseModelCreate = std::uint64_t *(__fastcall *)(
    std::uint64_t, std::uint64_t *, std::uint64_t, std::uint64_t);
using FinalModelCreate = std::uint64_t *(__fastcall *)(
    std::uint64_t, std::uint64_t *, const std::uint64_t *,
    const std::uint64_t *, std::uint64_t, char);
using FinalAccessoryInsert = std::uint64_t(__fastcall *)(std::uint64_t *,
                                                         std::uint64_t *);
using SetParent = void(__fastcall *)(std::uint64_t, std::uint64_t);
using SetTransform = void(__fastcall *)(std::uint64_t, const PrismTransform *);
using VehicleRenderDispatch = void(__fastcall *)(std::uint64_t,
                                                 std::uint64_t);
using TrailerVisualUpdate = void(__fastcall *)(std::uint64_t);
using TrailerRender = void(__fastcall *)(std::uint64_t, std::uint64_t *,
                                         std::uint64_t, std::uint64_t);

struct Engine
{
    std::uint8_t *base = nullptr;
    ModelLoad model_load = nullptr;
    ModelActivate model_activate = nullptr;
    ModelTransfer model_transfer = nullptr;
    ModelParameterInit model_parameter_init = nullptr;
    VehicleAccessoryCollect vehicle_accessory_collect = nullptr;
    VehicleAddonFinalize vehicle_addon_finalize = nullptr;
    RenderEntryPopulate render_entry_populate = nullptr;
    FinalBaseModelCreate final_base_model_create = nullptr;
    FinalModelCreate final_model_create = nullptr;
    FinalAccessoryInsert final_accessory_insert = nullptr;
    SetParent set_parent = nullptr;
    SetTransform set_transform = nullptr;
    VehicleRenderDispatch vehicle_render_dispatch = nullptr;
    TrailerVisualUpdate trailer_visual_update = nullptr;
    TrailerRender trailer_render = nullptr;
    bool enabled = false;
};

struct Entity
{
    std::uint64_t *model = nullptr;
    reverse_assist::BodyKind kind = reverse_assist::BodyKind::tractor;
    std::uint64_t parent = 0;
};

Telemetry telemetry;
std::mutex telemetry_configuration_mutex;
Engine engine;
const reverse_assist::compatibility::BuildProfile *active_build_profile = nullptr;
std::vector<Entity> entities;
std::vector<Entity> trailer_entities;
scs_log_t game_log = nullptr;
std::filesystem::path log_path;
std::filesystem::path model_trace_path;
std::filesystem::path world_probe_path;
bool faulted = false;
volatile LONG native_creation_stage = 0;
bool logged_load_ok = false;
bool logged_transfer_ok = false;
bool logged_parameter_init_ok = false;
bool logged_activate_ok = false;
bool logged_finalize_ok = false;
bool logged_transform_ok = false;
ModelLoad original_model_load = nullptr;
ModelActivate original_model_activate = nullptr;
VehicleAccessoryCollect original_vehicle_accessory_collect = nullptr;
VehicleAddonFinalize original_vehicle_addon_finalize = nullptr;
RenderEntryPopulate original_render_entry_populate = nullptr;
FinalBaseModelCreate original_final_base_model_create = nullptr;
FinalModelCreate original_final_model_create = nullptr;
FinalAccessoryInsert original_final_accessory_insert = nullptr;
SetParent original_set_parent = nullptr;
SetTransform original_set_transform = nullptr;
VehicleRenderDispatch original_vehicle_render_dispatch = nullptr;
TrailerVisualUpdate original_trailer_visual_update = nullptr;
TrailerRender original_trailer_render = nullptr;
std::mutex accessory_models_mutex;
std::mutex model_trace_mutex;
std::vector<std::string> traced_model_paths;
std::vector<std::uint64_t *> accessory_source_models;
std::array<std::atomic<std::uint64_t>, 2> accessory_source_nodes{};
std::array<std::atomic<bool>, 2> logged_render_targets{};
std::vector<std::uint64_t *> pending_accessory_models;
std::array<std::atomic<std::uint64_t>, 8> pending_accessory_nodes{};
std::vector<std::uint64_t *> visible_accessory_models;
std::array<std::atomic<std::uint64_t>, 8> visible_accessory_nodes{};
PrismTransform accessory_target_transform{0.0f, -100.0f, 0.0f, 0, 0,
                                           1.0f, 0.0f, 0.0f, 0.0f};
ULONGLONG last_accessory_capture_tick = 0;
bool logged_finalize_source_miss = false;
std::atomic<bool> logged_final_accessory_insert{};
std::atomic<std::uint32_t> final_model_create_diagnostics{};
std::atomic<ULONGLONG> final_accessory_ready_tick{};
std::atomic<ULONGLONG> driving_started_tick{};
std::atomic<bool> logged_driving_scene_ready{};
bool model_load_hook_installed = false;

struct VehicleRenderProbeRecord
{
    std::uintptr_t caller_rva = 0;
    std::uint64_t wrapper = 0;
    std::uint64_t truck = 0;
    std::uint64_t trailer = 0;
    std::uint64_t truck_vtable = 0;
    std::uintptr_t truck_render_rva = 0;
    std::uint64_t trailer_vtable = 0;
    std::uintptr_t trailer_render_rva = 0;
    std::int32_t trailer_state = INT32_MIN;
    std::uint64_t truck_target_f68 = 0;
    std::uint64_t trailer_target_f68 = 0;
    std::uint64_t render_flags = 0;
};

std::mutex vehicle_render_probe_mutex;
std::vector<VehicleRenderProbeRecord> vehicle_render_probe_records;
std::atomic<std::uint64_t> player_trailer_object{};
constexpr std::size_t maximum_prediction_frames = 96;
struct PredictionFrame
{
    PrismTransform transform{};
    reverse_assist::BodyKind kind = reverse_assist::BodyKind::tractor;
};
struct PredictionFrameSet
{
    std::array<PredictionFrame, maximum_prediction_frames> frames{};
    std::size_t count = 0;
};
std::mutex prediction_frames_mutex;
PredictionFrameSet prediction_frames;
std::array<std::uint64_t *, maximum_prediction_frames>
    tractor_prediction_models{};
std::array<std::uint64_t *, maximum_prediction_frames>
    trailer_prediction_models{};
std::mutex prediction_models_mutex;
std::atomic<bool> independent_frame_render_disabled{};
std::atomic<bool> logged_independent_frame_render{};
std::atomic<bool> logged_independent_frame_models{};

bool unsafe_call_set_transform(std::uint64_t *model, const PrismTransform *transform);
bool unsafe_call_set_parent(std::uint64_t *model, std::uint64_t parent);
std::uint64_t unsafe_read_parent(std::uint64_t *model);
bool unsafe_render_independent_prediction_frames(
    std::uint64_t render_context);

struct DescriptorArraySnapshot
{
    std::uint64_t begin = 0;
    std::size_t count = 0;
};

constexpr std::size_t model_fingerprint_qwords = 96;

struct ModelFingerprint
{
    std::array<std::uint64_t, model_fingerprint_qwords> values{};
    bool valid = false;
};

bool unsafe_read_descriptor_array(std::uint64_t *array,
                                  DescriptorArraySnapshot *snapshot)
{
    if (!array || !snapshot) return false;
    __try
    {
        snapshot->begin = array[1];
        snapshot->count = static_cast<std::size_t>(array[2]);
        if (snapshot->count > 4096 ||
            (snapshot->count != 0 && snapshot->begin == 0))
            return false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        snapshot->begin = 0;
        snapshot->count = 0;
        return false;
    }
}

bool unsafe_read_model_fingerprint(std::uint64_t *model,
                                   ModelFingerprint *fingerprint)
{
    if (!model || !fingerprint) return false;
    __try
    {
        for (std::size_t index = 0; index < fingerprint->values.size(); ++index)
            fingerprint->values[index] = model[index];
        fingerprint->valid = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        fingerprint->values.fill(0);
        fingerprint->valid = false;
        return false;
    }
}

std::uint64_t *unsafe_read_descriptor_model(
    const DescriptorArraySnapshot &snapshot, std::size_t index)
{
    if (index >= snapshot.count || !snapshot.begin) return nullptr;
    __try
    {
        const auto descriptor = snapshot.begin + index * 0x30;
        return *reinterpret_cast<std::uint64_t **>(descriptor + 0x10);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

void log_line(const std::string &text, scs_log_type_t type = SCS_LOG_TYPE_message)
{
    if (game_log) game_log(type, text.c_str());
    if (!log_path.empty())
    {
        std::ofstream stream(log_path, std::ios::app);
        stream << text << '\n';
    }
}

void trace_model_load(const char *path, std::uintptr_t return_address,
                      std::uint64_t *model, char flags)
{
    if constexpr (!trace_all_model_loads) return;
    if (!path || path[0] != '/' || model_trace_path.empty()) return;

    std::lock_guard lock(model_trace_mutex);
    if (traced_model_paths.size() >= maximum_traced_model_paths) return;
    if (std::find(traced_model_paths.begin(), traced_model_paths.end(), path) !=
        traced_model_paths.end())
        return;

    traced_model_paths.emplace_back(path);
    std::ofstream stream(model_trace_path, std::ios::app);
    stream << std::hex << std::setfill('0')
           << "caller_rva=0x" << std::setw(8)
           << (return_address -
               reinterpret_cast<std::uintptr_t>(engine.base))
           << " model=0x" << std::setw(16)
           << reinterpret_cast<std::uintptr_t>(model)
           << " flags=0x" << std::setw(2)
           << static_cast<unsigned>(static_cast<unsigned char>(flags))
           << " path=" << path << '\n';
}

bool unsafe_capture_vehicle_render_probe(
    std::uint64_t wrapper, std::uint64_t render_context,
    std::uintptr_t return_address, VehicleRenderProbeRecord *record)
{
    if (!wrapper || !render_context || !record || !engine.base) return false;
    __try
    {
        const auto base = reinterpret_cast<std::uintptr_t>(engine.base);
        record->caller_rva = return_address - base;
        record->wrapper = wrapper;
        record->truck =
            *reinterpret_cast<const std::uint64_t *>(wrapper + 0x18);
        record->trailer =
            *reinterpret_cast<const std::uint64_t *>(wrapper + 0xc8);
        record->render_flags =
            *reinterpret_cast<const std::uint64_t *>(render_context + 0x18);

        if (record->truck)
        {
            record->truck_vtable =
                *reinterpret_cast<const std::uint64_t *>(record->truck);
            const auto render =
                *reinterpret_cast<const std::uint64_t *>(
                    record->truck_vtable + 0x300);
            record->truck_render_rva =
                static_cast<std::uintptr_t>(render) - base;
            record->trailer_state =
                *reinterpret_cast<const std::int32_t *>(
                    record->truck + 0x1450);
            record->truck_target_f68 =
                *reinterpret_cast<const std::uint64_t *>(
                    record->truck + 0xf68);
        }

        if (record->trailer)
        {
            record->trailer_vtable =
                *reinterpret_cast<const std::uint64_t *>(record->trailer);
            const auto render =
                *reinterpret_cast<const std::uint64_t *>(
                    record->trailer_vtable + 0x300);
            record->trailer_render_rva =
                static_cast<std::uintptr_t>(render) - base;
            record->trailer_target_f68 =
                *reinterpret_cast<const std::uint64_t *>(
                    record->trailer + 0xf68);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void trace_vehicle_render_dispatch(
    std::uint64_t wrapper, std::uint64_t render_context,
    std::uintptr_t return_address)
{
    if (world_probe_path.empty()) return;

    VehicleRenderProbeRecord record{};
    if (!unsafe_capture_vehicle_render_probe(
            wrapper, render_context, return_address, &record))
        return;

    std::lock_guard lock(vehicle_render_probe_mutex);
    const auto duplicate = std::find_if(
        vehicle_render_probe_records.begin(),
        vehicle_render_probe_records.end(),
        [&](const VehicleRenderProbeRecord &existing) {
            return existing.caller_rva == record.caller_rva &&
                   existing.wrapper == record.wrapper &&
                   existing.truck == record.truck &&
                   existing.trailer == record.trailer &&
                   existing.truck_render_rva == record.truck_render_rva &&
                   existing.trailer_render_rva == record.trailer_render_rva &&
                   existing.trailer_state == record.trailer_state &&
                   existing.truck_target_f68 == record.truck_target_f68 &&
                   existing.trailer_target_f68 ==
                       record.trailer_target_f68;
        });
    if (duplicate != vehicle_render_probe_records.end() ||
        vehicle_render_probe_records.size() >= 256)
        return;

    vehicle_render_probe_records.push_back(record);
    std::ofstream stream(world_probe_path, std::ios::app);
    stream << std::hex << std::setfill('0')
           << "caller_rva=0x" << std::setw(8) << record.caller_rva
           << " wrapper=0x" << std::setw(16) << record.wrapper
           << " truck=0x" << std::setw(16) << record.truck
           << " truck_vtable=0x" << std::setw(16) << record.truck_vtable
           << " truck_render_rva=0x" << std::setw(8)
           << record.truck_render_rva
           << " trailer=0x" << std::setw(16) << record.trailer
           << " trailer_vtable=0x" << std::setw(16)
           << record.trailer_vtable
           << " trailer_render_rva=0x" << std::setw(8)
           << record.trailer_render_rva
           << " trailer_state=" << std::dec << record.trailer_state
           << std::hex
           << " truck_f68=0x" << std::setw(16)
           << record.truck_target_f68
           << " trailer_f68=0x" << std::setw(16)
           << record.trailer_target_f68
           << " flags=0x" << std::setw(16) << record.render_flags
           << '\n';
}

void unsafe_capture_player_trailer(std::uint64_t wrapper)
{
    if (!wrapper) return;
    __try
    {
        player_trailer_object.store(
            *reinterpret_cast<const std::uint64_t *>(wrapper + 0xc8),
            std::memory_order_release);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        player_trailer_object.store(0, std::memory_order_release);
    }
}

void __fastcall hooked_vehicle_render_dispatch(
    std::uint64_t wrapper, std::uint64_t render_context)
{
    const auto return_address =
        reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    unsafe_capture_player_trailer(wrapper);
    original_vehicle_render_dispatch(wrapper, render_context);
    if (!independent_frame_render_disabled.load(std::memory_order_acquire) &&
        !unsafe_render_independent_prediction_frames(render_context))
    {
        independent_frame_render_disabled.store(true,
                                                std::memory_order_release);
        log_line(
            "[reverse-entity] Independent prediction-frame rendering faulted; "
            "disabled for this run.",
            SCS_LOG_TYPE_error);
    }
    trace_vehicle_render_dispatch(wrapper, render_context, return_address);
}

// Preserve the game's marker visibility, animation and placement unchanged.
// Keep this pass-through hook so the verified four-hook build profiles remain valid.
void __fastcall hooked_trailer_visual_update(std::uint64_t trailer)
{
    original_trailer_visual_update(trailer);
}
bool unsafe_render_prediction_frames(std::uint64_t trailer,
                                     std::uint64_t *render_context,
                                     const PredictionFrameSet *frames)
{
    (void)trailer;
    (void)render_context;
    (void)frames;
    return true;
}

void __fastcall hooked_trailer_render(std::uint64_t trailer,
                                      std::uint64_t *render_context,
                                      std::uint64_t render_pass,
                                      std::uint64_t render_flags)
{
    original_trailer_render(trailer, render_context, render_pass,
                            render_flags);
}

std::uint64_t *__fastcall hooked_model_load(std::uint64_t *output, const char **path,
                                             const std::uint64_t *variant,
                                             const std::uint64_t *look, char flags)
{
    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    auto *result = original_model_load(output, path, variant, look, flags);
    if (output && *output && path && *path)
        trace_model_load(*path, return_address,
                         reinterpret_cast<std::uint64_t *>(*output), flags);
    if (output && *output && path && *path &&
        std::strcmp(*path, accessory_model_path) == 0)
    {
        auto *model = reinterpret_cast<std::uint64_t *>(*output);
        const auto now = GetTickCount64();
        {
            std::lock_guard lock(accessory_models_mutex);
            // The same accessory is normally loaded twice (interior/exterior) in a
            // tight batch. A later batch belongs to a rebuilt vehicle/menu scene;
            // discard the previous scene's pointers before they become stale.
            if (last_accessory_capture_tick != 0 &&
                now - last_accessory_capture_tick > 250)
            {
                accessory_source_models.clear();
                for (auto &source : accessory_source_nodes)
                    source.store(0, std::memory_order_relaxed);
                for (auto &logged : logged_render_targets)
                    logged.store(false, std::memory_order_relaxed);
                pending_accessory_models.clear();
                for (auto &node : pending_accessory_nodes)
                    node.store(0, std::memory_order_relaxed);
                visible_accessory_models.clear();
                for (auto &node : visible_accessory_nodes)
                    node.store(0, std::memory_order_relaxed);
                accessory_target_transform = {0.0f, -100.0f, 0.0f, 0, 0,
                                               1.0f, 0.0f, 0.0f, 0.0f};
                logged_finalize_source_miss = false;
                logged_final_accessory_insert.store(false,
                                                    std::memory_order_relaxed);
                final_model_create_diagnostics.store(
                    0, std::memory_order_relaxed);
                final_accessory_ready_tick.store(0,
                                                 std::memory_order_release);
                logged_driving_scene_ready.store(false,
                                                 std::memory_order_relaxed);
                log_line("[reverse-entity] Accessory scene generation replaced.");
            }
            last_accessory_capture_tick = now;
            if (std::find(accessory_source_models.begin(),
                          accessory_source_models.end(), model) ==
                accessory_source_models.end())
            {
                accessory_source_models.push_back(model);
                const auto source_index = accessory_source_models.size() - 1;
                const auto placement_node =
                    reinterpret_cast<std::uint64_t>(model) + 0x10;
                if (source_index < accessory_source_nodes.size())
                    accessory_source_nodes[source_index].store(
                        placement_node, std::memory_order_relaxed);
                if (std::find(visible_accessory_models.begin(),
                              visible_accessory_models.end(), model) ==
                        visible_accessory_models.end() &&
                    visible_accessory_models.size() <
                        visible_accessory_nodes.size())
                {
                    visible_accessory_models.push_back(model);
                    for (auto &node : visible_accessory_nodes)
                    {
                        if (node.load(std::memory_order_relaxed) == 0)
                        {
                            node.store(placement_node,
                                       std::memory_order_relaxed);
                            break;
                        }
                    }
                }
                ULONG_PTR stack_low = 0;
                ULONG_PTR stack_high = 0;
                GetCurrentThreadStackLimits(&stack_low, &stack_high);
                const auto output_address = reinterpret_cast<std::uintptr_t>(output);
                const bool output_is_stack =
                    output_address >= stack_low && output_address < stack_high;
                std::ostringstream message;
                message << "[reverse-entity] Captured accessory source model "
                        << model << "; loader output slot " << output
                        << (output_is_stack ? " (stack)" : " (non-stack)")
                        << "; caller RVA +0x" << std::hex
                        << (return_address - reinterpret_cast<std::uintptr_t>(engine.base))
                        << ".";
                log_line(message.str());
            }
        }
        // This object is a cache/source model. Never move it: the game clones
        // the final drawable model later, and altering the source contaminates
        // the clone's initial placement.
    }
    return result;
}

bool unsafe_copy_resource_path(std::uint64_t resource, char *output,
                               std::size_t output_size)
{
    if (!resource || !output || output_size < 2) return false;
    output[0] = '\0';
    __try
    {
        // The resource's source descriptor is at +0x10 and its original PMD
        // path is at +0x28. Read this before the model becomes a final vehicle
        // instance because later binding can replace its active resource.
        const auto descriptor =
            *reinterpret_cast<std::uint64_t *>(resource + 0x10);
        if (!descriptor) return false;
        const auto *path =
            *reinterpret_cast<const char **>(descriptor + 0x28);
        if (!path) return false;

        std::size_t index = 0;
        for (; index + 1 < output_size; ++index)
        {
            const char value = path[index];
            output[index] = value;
            if (value == '\0') return index != 0;
        }
        output[output_size - 1] = '\0';
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output[0] = '\0';
        return false;
    }
}

bool is_reverse_assist_model_path(const char *path)
{
    constexpr char prefix[] = "/model/reverse_assist/accessory_anchor.pm";
    return path && std::strncmp(path, prefix, sizeof(prefix) - 1) == 0;
}

std::uint64_t *__fastcall hooked_final_base_model_create(
    std::uint64_t resource, std::uint64_t *output,
    std::uint64_t variant_index, std::uint64_t look_index)
{
    const auto return_address =
        reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    std::array<char, 512> path{};
    const bool is_final_base_creation =
        return_address ==
        reinterpret_cast<std::uintptr_t>(engine.base) +
            final_base_model_create_return_rva;
    const bool path_available =
        is_final_base_creation &&
        unsafe_copy_resource_path(resource, path.data(), path.size());
    const bool is_target =
        path_available && is_reverse_assist_model_path(path.data());

    auto *result = original_final_base_model_create(
        resource, output, variant_index, look_index);
    if (is_final_base_creation)
    {
        const auto diagnostic_index =
            final_model_create_diagnostics.fetch_add(
                1, std::memory_order_relaxed);
        if (diagnostic_index < 64)
        {
            std::ostringstream message;
            message << "[reverse-entity] Final base candidate "
                    << diagnostic_index << ": path "
                    << (path_available ? path.data() : "<unreadable>")
                    << ", model "
                    << reinterpret_cast<void *>(output ? *output : 0)
                    << ".";
            log_line(message.str());
        }
    }
    if (!is_target || !output || !*output) return result;

    auto *model = reinterpret_cast<std::uint64_t *>(*output);
    bool newly_pending = false;
    bool node_registered = false;
    {
        std::lock_guard lock(accessory_models_mutex);
        if (std::find(pending_accessory_models.begin(),
                      pending_accessory_models.end(), model) ==
                pending_accessory_models.end() &&
            std::find(visible_accessory_models.begin(),
                      visible_accessory_models.end(), model) ==
                visible_accessory_models.end())
        {
            pending_accessory_models.push_back(model);
            newly_pending = true;
            const auto placement_node =
                reinterpret_cast<std::uint64_t>(model) + 0x10;
            for (auto &node : pending_accessory_nodes)
            {
                if (node.load(std::memory_order_relaxed) == 0)
                {
                    node.store(placement_node, std::memory_order_relaxed);
                    node_registered = true;
                    break;
                }
            }
        }
    }
    if (newly_pending)
    {
        std::ostringstream message;
        message << "[reverse-entity] Identified final base accessory "
                << model << " from resource " << path.data()
                << (node_registered ? "." : "; pending registry full.");
        log_line(message.str());
    }
    return result;
}

std::uint64_t *__fastcall hooked_final_model_create(
    std::uint64_t resource, std::uint64_t *output,
    const std::uint64_t *variant, const std::uint64_t *look,
    std::uint64_t context, char flags)
{
    const auto return_address =
        reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    std::array<char, 512> path{};
    const bool is_final_vehicle_creation =
        return_address ==
            reinterpret_cast<std::uintptr_t>(engine.base) +
                final_model_create_return_rva;
    const bool path_available =
        is_final_vehicle_creation &&
        unsafe_copy_resource_path(resource, path.data(), path.size());
    const bool is_target =
        path_available &&
        is_reverse_assist_model_path(path.data());

    auto *result = original_final_model_create(
        resource, output, variant, look, context, flags);
    if (is_final_vehicle_creation)
    {
        const auto diagnostic_index =
            final_model_create_diagnostics.fetch_add(
                1, std::memory_order_relaxed);
        if (diagnostic_index < 64)
        {
            std::ostringstream message;
            message << "[reverse-entity] Final model candidate "
                    << diagnostic_index << ": resource "
                    << reinterpret_cast<void *>(resource) << ", path "
                    << (path_available ? path.data() : "<unreadable>")
                    << ", model "
                    << reinterpret_cast<void *>(output ? *output : 0)
                    << ".";
            log_line(message.str());
        }
    }
    if (!is_target || !output || !*output) return result;

    auto *model = reinterpret_cast<std::uint64_t *>(*output);
    bool newly_pending = false;
    bool node_registered = false;
    {
        std::lock_guard lock(accessory_models_mutex);
        if (std::find(pending_accessory_models.begin(),
                      pending_accessory_models.end(), model) ==
                pending_accessory_models.end() &&
            std::find(visible_accessory_models.begin(),
                      visible_accessory_models.end(), model) ==
                visible_accessory_models.end())
        {
            pending_accessory_models.push_back(model);
            newly_pending = true;
            const auto placement_node =
                reinterpret_cast<std::uint64_t>(model) + 0x10;
            for (auto &node : pending_accessory_nodes)
            {
                if (node.load(std::memory_order_relaxed) == 0)
                {
                    node.store(placement_node, std::memory_order_relaxed);
                    node_registered = true;
                    break;
                }
            }
        }
    }
    if (newly_pending)
    {
        std::ostringstream message;
        message << "[reverse-entity] Identified final accessory creation "
                << model << " from resource " << path.data()
                << (node_registered ? "." : "; pending registry full.");
        log_line(message.str());
    }
    return result;
}

std::uint64_t __fastcall hooked_final_accessory_insert(
    std::uint64_t *array, std::uint64_t *entry)
{
    const auto return_address =
        reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto result = original_final_accessory_insert(array, entry);
    if (return_address !=
        reinterpret_cast<std::uintptr_t>(engine.base) +
            final_accessory_insert_return_rva)
        return result;

    if (!logged_final_accessory_insert.exchange(true,
                                                std::memory_order_relaxed))
        log_line("[reverse-entity] Final vehicle accessory table observed.");

    auto *model =
        entry ? reinterpret_cast<std::uint64_t *>(entry[1]) : nullptr;

    PrismTransform target{};
    bool newly_captured = false;
    bool node_registered = false;
    {
        std::lock_guard lock(accessory_models_mutex);
        target = accessory_target_transform;
        const auto pending =
            std::find(pending_accessory_models.begin(),
                      pending_accessory_models.end(), model);
        if (pending != pending_accessory_models.end())
        {
            pending_accessory_models.erase(pending);
            visible_accessory_models.push_back(model);
            newly_captured = true;
            const auto placement_node =
                reinterpret_cast<std::uint64_t>(model) + 0x10;
            for (auto &node : pending_accessory_nodes)
                if (node.load(std::memory_order_relaxed) == placement_node)
                    node.store(0, std::memory_order_relaxed);
            for (auto &node : visible_accessory_nodes)
            {
                if (node.load(std::memory_order_relaxed) == 0)
                {
                    node.store(placement_node, std::memory_order_relaxed);
                    node_registered = true;
                    break;
                }
            }
        }
    }
    if (!newly_captured) return result;

    const auto previous_parent = unsafe_read_parent(model);
    const bool parent_detached = unsafe_call_set_parent(model, 0);
    const bool transform_applied = unsafe_call_set_transform(model, &target);

    std::ostringstream message;
    message << "[reverse-entity] Captured final vehicle-visible accessory "
            << model << " from tracked creation; token 0x"
            << std::hex << (entry ? entry[0] : 0)
            << "; detached parent "
            << reinterpret_cast<void *>(previous_parent)
            << (parent_detached ? "" : " (detach failed)")
            << (transform_applied ? "" : "; transform failed")
            << (node_registered ? "." : "; node registry full.");
    log_line(message.str(),
             parent_detached && transform_applied ? SCS_LOG_TYPE_message
                                                  : SCS_LOG_TYPE_warning);
    return result;
}

void __fastcall hooked_vehicle_accessory_collect(
    std::uint64_t vehicle, std::uint64_t *interior_descriptors,
    std::uint64_t exterior_descriptors, char interior, std::uint8_t flags)
{
    original_vehicle_accessory_collect(
        vehicle, interior_descriptors, exterior_descriptors, interior, flags);

    std::array<std::uint64_t *, 2> sources{};
    std::size_t source_count = 0;
    {
        std::lock_guard lock(accessory_models_mutex);
        source_count = std::min(sources.size(), accessory_source_models.size());
        std::copy_n(accessory_source_models.begin(), source_count, sources.begin());
    }
    if (source_count == 0) return;

    DescriptorArraySnapshot interior_snapshot{};
    DescriptorArraySnapshot exterior_snapshot{};
    unsafe_read_descriptor_array(interior_descriptors, &interior_snapshot);
    unsafe_read_descriptor_array(
        reinterpret_cast<std::uint64_t *>(exterior_descriptors),
        &exterior_snapshot);

    struct ExactMatch
    {
        std::uint64_t *model = nullptr;
        std::size_t index = 0;
        const char *array_name = nullptr;
    };
    std::array<ExactMatch, 2> matches{};
    std::size_t match_count = 0;
    auto scan = [&](const DescriptorArraySnapshot &snapshot,
                    const char *array_name) {
        for (std::size_t index = 0;
             index < snapshot.count && match_count < matches.size(); ++index)
        {
            auto *candidate = unsafe_read_descriptor_model(snapshot, index);
            if (!candidate ||
                std::find(sources.begin(), sources.begin() + source_count,
                          candidate) == sources.begin() + source_count)
                continue;
            bool duplicate = false;
            for (std::size_t match = 0; match < match_count; ++match)
                duplicate = duplicate || matches[match].model == candidate;
            if (!duplicate)
                matches[match_count++] = {candidate, index, array_name};
        }
    };
    scan(interior_snapshot, "interior");
    scan(exterior_snapshot, "exterior");
    if (match_count == 0) return;

    PrismTransform target{};
    std::array<ExactMatch, 2> newly_visible{};
    std::size_t newly_visible_count = 0;
    {
        std::lock_guard lock(accessory_models_mutex);
        target = accessory_target_transform;
        for (std::size_t index = 0; index < match_count; ++index)
        {
            auto *model = matches[index].model;
            if (std::find(visible_accessory_models.begin(),
                          visible_accessory_models.end(), model) !=
                visible_accessory_models.end())
                continue;
            if (visible_accessory_models.size() >= visible_accessory_nodes.size())
                break;
            visible_accessory_models.push_back(model);
            const auto node_index = visible_accessory_models.size() - 1;
            visible_accessory_nodes[node_index].store(
                reinterpret_cast<std::uint64_t>(model) + 0x10,
                std::memory_order_relaxed);
            newly_visible[newly_visible_count++] = matches[index];
        }
    }

    for (std::size_t index = 0; index < newly_visible_count; ++index)
    {
        auto *model = newly_visible[index].model;
        const auto previous_parent = unsafe_read_parent(model);
        unsafe_call_set_parent(model, 0);
        unsafe_call_set_transform(model, &target);
        std::ostringstream message;
        message << "[reverse-entity] Captured exact accessory model " << model
                << " from " << newly_visible[index].array_name
                << " descriptor " << newly_visible[index].index
                << "; detached from parent "
                << reinterpret_cast<void *>(previous_parent) << ".";
        log_line(message.str());
    }
}

bool unsafe_override_render_entry(std::uint64_t render_entry,
                                  const PrismTransform &transform)
{
    if (!render_entry) return false;
    __try
    {
        // FUN_140311bb0 publishes the final model draw record. Its matrix is at
        // +0x18 and its split floating-origin position is at +0x58. This is the
        // first object in the accessory path that directly controls pixels.
        auto *matrix = reinterpret_cast<float *>(render_entry + 0x18);
        const float doubled_x = transform.qx + transform.qx;
        const float doubled_y = transform.qy + transform.qy;
        const float doubled_z = transform.qz + transform.qz;

        matrix[0] =
            1.0f - (transform.qz * doubled_z + transform.qy * doubled_y);
        matrix[4] =
            transform.qw * doubled_z + transform.qx * doubled_y;
        matrix[1] =
            transform.qx * doubled_y - transform.qw * doubled_z;
        matrix[8] =
            transform.qx * doubled_z - transform.qw * doubled_y;
        matrix[9] =
            transform.qw * doubled_x + transform.qy * doubled_z;
        matrix[6] =
            transform.qy * doubled_z - transform.qw * doubled_x;
        matrix[5] =
            1.0f - (transform.qz * doubled_z + transform.qx * doubled_x);
        matrix[2] =
            transform.qw * doubled_y + transform.qx * doubled_z;
        matrix[10] =
            1.0f - (transform.qy * doubled_y + transform.qx * doubled_x);
        matrix[3] = 0.0f;
        matrix[7] = 0.0f;
        matrix[11] = 0.0f;
        matrix[12] = 0.0f;
        matrix[13] = 0.0f;
        matrix[14] = 0.0f;
        matrix[15] = 1.0f;

        *reinterpret_cast<float *>(render_entry + 0x58) = transform.x;
        *reinterpret_cast<float *>(render_entry + 0x5c) = transform.y;
        *reinterpret_cast<float *>(render_entry + 0x60) = transform.z;
        *reinterpret_cast<std::int16_t *>(render_entry + 0x64) =
            transform.sector_x;
        *reinterpret_cast<std::int16_t *>(render_entry + 0x66) =
            transform.sector_z;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void __fastcall hooked_render_entry_populate(std::uint64_t render_entry,
                                             std::uint64_t *placement)
{
    original_render_entry_populate(render_entry, placement);

    // FUN_140311bb0 receives the placement node as its second argument and
    // copies that node's pose into the final 0x68-byte world/draw record.
    // The previous diagnostic incorrectly read render_entry + 0x68, which is
    // already outside that record. Match the actual placement argument instead.
    const auto placement_node = reinterpret_cast<std::uint64_t>(placement);

    std::size_t source_index = accessory_source_nodes.size();
    for (std::size_t index = 0; index < accessory_source_nodes.size(); ++index)
    {
        if (placement_node != 0 &&
            placement_node ==
                accessory_source_nodes[index].load(std::memory_order_relaxed))
        {
            source_index = index;
            break;
        }
    }
    if (source_index == accessory_source_nodes.size()) return;

    PrismTransform target{};
    {
        std::lock_guard lock(accessory_models_mutex);
        target = accessory_target_transform;
    }
    if (!unsafe_override_render_entry(render_entry, target)) return;

    ULONGLONG no_ready_tick = 0;
    final_accessory_ready_tick.compare_exchange_strong(
        no_ready_tick, GetTickCount64(), std::memory_order_release,
        std::memory_order_relaxed);

    if (!logged_render_targets[source_index].exchange(
            true, std::memory_order_relaxed))
    {
        std::ostringstream message;
        message << "[reverse-entity] Captured final drawable accessory record "
                << reinterpret_cast<void *>(render_entry)
                << " for placement " << reinterpret_cast<void *>(placement_node)
                << " (view " << source_index
                << "); native world-record override active.";
        log_line(message.str());
    }
}

void __fastcall hooked_model_activate(std::uint64_t *model, std::uint64_t look)
{
    original_model_activate(model, look);
    bool is_accessory = false;
    {
        std::lock_guard lock(accessory_models_mutex);
        is_accessory =
            std::find(visible_accessory_models.begin(),
                      visible_accessory_models.end(), model) !=
            visible_accessory_models.end();
        if (is_accessory && !telemetry.driving)
            accessory_target_transform = {0.0f, -100.0f, 0.0f, 0, 0,
                                          1.0f, 0.0f, 0.0f, 0.0f};
    }
    if (!is_accessory) return;

    // Newly built driving/menu/service scenes can appear while telemetry is
    // paused and no frame-end callback is delivered. Hide the accessory as
    // soon as the game finishes activating it; reverse mode will reposition
    // it on the next driving frame.
    PrismTransform hidden{};
    hidden.y = -100.0f;
    hidden.qw = 1.0f;
    if (!unsafe_call_set_transform(model, &hidden))
    {
        std::lock_guard lock(accessory_models_mutex);
        std::erase(visible_accessory_models, model);
        log_line("[reverse-entity] Rejected an invalid accessory instance during activation.",
                 SCS_LOG_TYPE_warning);
    }
}

void __fastcall hooked_vehicle_addon_finalize(
    std::uint64_t vehicle, std::uint64_t *descriptor_array,
    std::uint64_t parameter_map, std::uint64_t *auxiliary_array,
    std::uint64_t interior_look, std::uint64_t exterior_look,
    std::uint64_t resource_array, char interior, char exterior)
{
    std::array<std::uint64_t *, 2> sources{};
    std::array<ModelFingerprint, 2> source_fingerprints{};
    std::size_t source_count = 0;
    ULONGLONG source_capture_tick = 0;
    {
        std::lock_guard lock(accessory_models_mutex);
        source_count = std::min(sources.size(), accessory_source_models.size());
        std::copy_n(accessory_source_models.begin(), source_count, sources.begin());
        source_capture_tick = last_accessory_capture_tick;
    }
    for (std::size_t index = 0; index < source_count; ++index)
        unsafe_read_model_fingerprint(sources[index], &source_fingerprints[index]);

    DescriptorArraySnapshot before{};
    std::array<std::size_t, 2> matching_indices{SIZE_MAX, SIZE_MAX};
    std::size_t match_count = 0;
    if (source_count != 0 &&
        unsafe_read_descriptor_array(descriptor_array, &before))
    {
        for (std::size_t index = 0;
             index < before.count && match_count < matching_indices.size();
             ++index)
        {
            auto *candidate = unsafe_read_descriptor_model(before, index);
            if (candidate &&
                std::find(sources.begin(), sources.begin() + source_count,
                          candidate) != sources.begin() + source_count)
                matching_indices[match_count++] = index;
        }
    }

    original_vehicle_addon_finalize(
        vehicle, descriptor_array, parameter_map, auxiliary_array,
        interior_look, exterior_look, resource_array, interior, exterior);

    DescriptorArraySnapshot after{};
    if (!unsafe_read_descriptor_array(descriptor_array, &after)) return;

    // FUN_140648e80 receives an empty output array in the player-truck path.
    // The source instances captured by FUN_140648640 are therefore not present
    // by pointer or stable index. Match the final clones by rare, equal fields
    // in their model_inst_u fingerprints instead. Interior/exterior clones of
    // this PMD share resource pointers, while unrelated cabin accessories do
    // not; fields shared by more than two output models are treated as generic
    // engine state and ignored.
    std::size_t best_score = 0;
    std::size_t best_count = 0;
    bool used_list_order_fallback = false;
    if (match_count == 0 && source_count != 0 && after.count != 0 &&
        after.count <= 256)
    {
        std::vector<ModelFingerprint> candidates(after.count);
        std::vector<std::size_t> scores(after.count, 0);
        for (std::size_t index = 0; index < after.count; ++index)
            unsafe_read_model_fingerprint(
                unsafe_read_descriptor_model(after, index), &candidates[index]);

        for (std::size_t field = 1; field < model_fingerprint_qwords; ++field)
        {
            if (!source_fingerprints[0].valid) continue;
            const auto value = source_fingerprints[0].values[field];
            if (value <= 0x10000 || value == UINT64_MAX) continue;
            if (source_count > 1 &&
                (!source_fingerprints[1].valid ||
                 source_fingerprints[1].values[field] != value))
                continue;

            std::size_t occurrences = 0;
            for (const auto &candidate : candidates)
                if (candidate.valid && candidate.values[field] == value)
                    ++occurrences;
            if (occurrences == 0 || occurrences > 2) continue;

            for (std::size_t index = 0; index < candidates.size(); ++index)
                if (candidates[index].valid &&
                    candidates[index].values[field] == value)
                    ++scores[index];
        }

        best_score =
            scores.empty() ? 0 : *std::max_element(scores.begin(), scores.end());
        if (best_score >= 2)
        {
            for (std::size_t index = 0; index < scores.size(); ++index)
            {
                if (scores[index] != best_score) continue;
                ++best_count;
                if (match_count < matching_indices.size())
                    matching_indices[match_count++] = index;
            }
            if (best_count > matching_indices.size())
                match_count = 0;
        }
    }

    // On the verified player-truck build our universal accessory is the first
    // descriptor emitted after its two source models are loaded. The initial
    // pass has an empty input array, so no pointer/index relationship exists;
    // use this validated list order only for the tight source-load/finalize
    // transaction. This avoids waiting for an optional later rebuild pass.
    if (match_count == 0 && source_count == 2 && after.count != 0 &&
        source_capture_tick != 0 &&
        GetTickCount64() - source_capture_tick < 3000)
    {
        matching_indices[0] = 0;
        match_count = 1;
        used_list_order_fallback = true;
    }

    bool should_log_diagnostic = false;
    {
        std::lock_guard lock(accessory_models_mutex);
        const auto now = GetTickCount64();
        should_log_diagnostic =
            source_count != 0 && !logged_finalize_source_miss &&
            last_accessory_capture_tick != 0 &&
            now - last_accessory_capture_tick < 3000;
        if (should_log_diagnostic) logged_finalize_source_miss = true;
    }
    if (should_log_diagnostic)
    {
        std::ostringstream message;
        message << "[reverse-entity] Final accessory pass: input descriptors "
                << before.count << ", output descriptors " << after.count
                << ", best rare-field score " << best_score
                << ", best candidates " << best_count
                << (used_list_order_fallback
                        ? ", using verified descriptor 0 fallback."
                        : ".");
        log_line(message.str(),
                 match_count == 0 ? SCS_LOG_TYPE_warning
                                  : SCS_LOG_TYPE_message);
    }
    if (match_count == 0) return;

    std::array<std::uint64_t *, 2> captured{};
    std::size_t captured_count = 0;
    for (std::size_t match = 0; match < match_count; ++match)
    {
        auto *model =
            unsafe_read_descriptor_model(after, matching_indices[match]);
        if (model) captured[captured_count++] = model;
    }
    if (captured_count == 0) return;

    PrismTransform target{};
    std::array<std::uint64_t *, 2> newly_visible{};
    std::size_t newly_visible_count = 0;
    {
        std::lock_guard lock(accessory_models_mutex);
        target = accessory_target_transform;
        for (std::size_t index = 0; index < captured_count; ++index)
        {
            auto *model = captured[index];
            if (std::find(visible_accessory_models.begin(),
                          visible_accessory_models.end(), model) !=
                visible_accessory_models.end())
                continue;
            if (visible_accessory_models.size() >= visible_accessory_nodes.size())
                break;
            visible_accessory_models.push_back(model);
            const auto node_index = visible_accessory_models.size() - 1;
            visible_accessory_nodes[node_index].store(
                reinterpret_cast<std::uint64_t>(model) + 0x10,
                std::memory_order_relaxed);
            newly_visible[newly_visible_count++] = model;
        }
    }

    for (std::size_t index = 0; index < newly_visible_count; ++index)
    {
        auto *model = newly_visible[index];
        const auto previous_parent = unsafe_read_parent(model);
        unsafe_call_set_parent(model, 0);
        unsafe_call_set_transform(model, &target);
        std::ostringstream message;
        message << "[reverse-entity] Captured final visible accessory model "
                << model << " at descriptor index "
                << matching_indices[index] << "; detached from angled parent "
                << reinterpret_cast<void *>(previous_parent)
                << " for world placement.";
        log_line(message.str());
    }
}

void __fastcall hooked_vehicle_addon_finalize_exact(
    std::uint64_t vehicle, std::uint64_t *descriptor_array,
    std::uint64_t parameter_map, std::uint64_t *auxiliary_array,
    std::uint64_t interior_look, std::uint64_t exterior_look,
    std::uint64_t resource_array, char interior, char exterior)
{
    original_vehicle_addon_finalize(
        vehicle, descriptor_array, parameter_map, auxiliary_array,
        interior_look, exterior_look, resource_array, interior, exterior);

    PrismTransform target{};
    std::vector<std::uint64_t *> finalized;
    {
        std::lock_guard lock(accessory_models_mutex);
        target = accessory_target_transform;
        finalized = pending_accessory_models;
        pending_accessory_models.clear();
        for (auto &node : pending_accessory_nodes)
            node.store(0, std::memory_order_relaxed);

        for (auto *model : finalized)
        {
            if (std::find(visible_accessory_models.begin(),
                          visible_accessory_models.end(), model) !=
                visible_accessory_models.end())
                continue;
            visible_accessory_models.push_back(model);
            const auto placement_node =
                reinterpret_cast<std::uint64_t>(model) + 0x10;
            for (auto &node : visible_accessory_nodes)
            {
                if (node.load(std::memory_order_relaxed) == 0)
                {
                    node.store(placement_node, std::memory_order_relaxed);
                    break;
                }
            }
        }
    }

    for (auto *model : finalized)
    {
        const auto previous_parent = unsafe_read_parent(model);
        const bool parent_detached = unsafe_call_set_parent(model, 0);
        const bool transform_applied =
            unsafe_call_set_transform(model, &target);
        std::ostringstream message;
        message << "[reverse-entity] Finalized exact base accessory "
                << model << "; detached parent "
                << reinterpret_cast<void *>(previous_parent)
                << (parent_detached ? "" : " (detach failed)")
                << (transform_applied ? "." : "; transform failed.");
        log_line(message.str(),
                 parent_detached && transform_applied
                     ? SCS_LOG_TYPE_message
                     : SCS_LOG_TYPE_warning);
    }
}

bool is_visible_accessory_node(std::uint64_t placement_node)
{
    if (!placement_node) return false;
    for (const auto &node : accessory_source_nodes)
        if (placement_node == node.load(std::memory_order_relaxed))
            return true;
    for (const auto &node : pending_accessory_nodes)
        if (placement_node == node.load(std::memory_order_relaxed))
            return true;
    for (const auto &node : visible_accessory_nodes)
        if (placement_node == node.load(std::memory_order_relaxed))
            return true;
    return false;
}

void __fastcall hooked_set_parent(std::uint64_t placement_node,
                                  std::uint64_t parent)
{
    const bool is_accessory = is_visible_accessory_node(placement_node);
    original_set_parent(placement_node, is_accessory ? 0 : parent);
}

void __fastcall hooked_set_transform(std::uint64_t placement_node,
                                     const PrismTransform *game_transform)
{
    PrismTransform replacement{};
    const bool is_accessory = is_visible_accessory_node(placement_node);
    if (!is_accessory)
    {
        original_set_transform(placement_node, game_transform);
        return;
    }
    {
        std::lock_guard lock(accessory_models_mutex);
        replacement = accessory_target_transform;
    }
    original_set_transform(placement_node, &replacement);
}

bool install_model_load_hook()
{
    if (!engine.model_load || !engine.vehicle_render_dispatch ||
        !engine.trailer_visual_update || !engine.trailer_render)
        return false;
    if constexpr (!model_load_hook_only)
    {
        if (!engine.final_base_model_create ||
            !engine.vehicle_addon_finalize ||
            !engine.render_entry_populate ||
            !engine.set_parent || !engine.set_transform)
            return false;
    }
    if (MH_Initialize() != MH_OK)
    {
        log_line("[reverse-entity] MinHook initialization failed.", SCS_LOG_TYPE_error);
        return false;
    }
    bool hooks_created =
        MH_CreateHook(reinterpret_cast<LPVOID>(engine.model_load),
                      reinterpret_cast<LPVOID>(&hooked_model_load),
                      reinterpret_cast<LPVOID *>(&original_model_load)) == MH_OK &&
        MH_CreateHook(
            reinterpret_cast<LPVOID>(engine.vehicle_render_dispatch),
            reinterpret_cast<LPVOID>(&hooked_vehicle_render_dispatch),
            reinterpret_cast<LPVOID *>(
                &original_vehicle_render_dispatch)) == MH_OK &&
        MH_CreateHook(
            reinterpret_cast<LPVOID>(engine.trailer_visual_update),
            reinterpret_cast<LPVOID>(&hooked_trailer_visual_update),
            reinterpret_cast<LPVOID *>(
                &original_trailer_visual_update)) == MH_OK &&
        MH_CreateHook(
            reinterpret_cast<LPVOID>(engine.trailer_render),
            reinterpret_cast<LPVOID>(&hooked_trailer_render),
            reinterpret_cast<LPVOID *>(&original_trailer_render)) == MH_OK;
    if constexpr (!model_load_hook_only)
    {
        hooks_created =
            hooks_created &&
            MH_CreateHook(
                reinterpret_cast<LPVOID>(engine.final_base_model_create),
                reinterpret_cast<LPVOID>(&hooked_final_base_model_create),
                reinterpret_cast<LPVOID *>(
                    &original_final_base_model_create)) == MH_OK &&
            MH_CreateHook(
                reinterpret_cast<LPVOID>(engine.vehicle_addon_finalize),
                reinterpret_cast<LPVOID>(
                    &hooked_vehicle_addon_finalize_exact),
                reinterpret_cast<LPVOID *>(
                    &original_vehicle_addon_finalize)) == MH_OK &&
            MH_CreateHook(
                reinterpret_cast<LPVOID>(engine.render_entry_populate),
                reinterpret_cast<LPVOID>(&hooked_render_entry_populate),
                reinterpret_cast<LPVOID *>(
                    &original_render_entry_populate)) == MH_OK &&
            MH_CreateHook(
                reinterpret_cast<LPVOID>(engine.set_parent),
                reinterpret_cast<LPVOID>(&hooked_set_parent),
                reinterpret_cast<LPVOID *>(&original_set_parent)) == MH_OK &&
            MH_CreateHook(
                reinterpret_cast<LPVOID>(engine.set_transform),
                reinterpret_cast<LPVOID>(&hooked_set_transform),
                reinterpret_cast<LPVOID *>(&original_set_transform)) == MH_OK;
    }
    if (!hooks_created || MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.model_load));
        MH_RemoveHook(
            reinterpret_cast<LPVOID>(engine.vehicle_render_dispatch));
        MH_RemoveHook(
            reinterpret_cast<LPVOID>(engine.trailer_visual_update));
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.trailer_render));
        if constexpr (!model_load_hook_only)
        {
            MH_RemoveHook(reinterpret_cast<LPVOID>(engine.final_base_model_create));
            MH_RemoveHook(reinterpret_cast<LPVOID>(engine.vehicle_addon_finalize));
            MH_RemoveHook(reinterpret_cast<LPVOID>(engine.render_entry_populate));
            MH_RemoveHook(reinterpret_cast<LPVOID>(engine.set_parent));
            MH_RemoveHook(reinterpret_cast<LPVOID>(engine.set_transform));
        }
        MH_Uninitialize();
        original_model_load = nullptr;
        original_vehicle_render_dispatch = nullptr;
        original_trailer_visual_update = nullptr;
        original_trailer_render = nullptr;
        original_final_base_model_create = nullptr;
        original_vehicle_addon_finalize = nullptr;
        original_set_parent = nullptr;
        original_set_transform = nullptr;
        log_line("[reverse-entity] Hook installation failed; native backend disabled and telemetry-only mode retained.",
                 SCS_LOG_TYPE_error);
        return false;
    }
    model_load_hook_installed = true;
    if constexpr (model_load_hook_only)
        log_line("[reverse-entity] Installed 4/4 enabled hooks: model_load, vehicle_render_dispatch, trailer_visual_update, trailer_render.");
    else
        log_line("[reverse-entity] Final vehicle accessory instance hooks installed.");
    return true;
}

void remove_model_load_hook()
{
    if (!model_load_hook_installed) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(reinterpret_cast<LPVOID>(engine.model_load));
    MH_RemoveHook(reinterpret_cast<LPVOID>(engine.vehicle_render_dispatch));
    MH_RemoveHook(reinterpret_cast<LPVOID>(engine.trailer_visual_update));
    MH_RemoveHook(reinterpret_cast<LPVOID>(engine.trailer_render));
    if constexpr (!model_load_hook_only)
    {
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.final_base_model_create));
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.vehicle_addon_finalize));
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.render_entry_populate));
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.set_parent));
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.set_transform));
    }
    MH_Uninitialize();
    model_load_hook_installed = false;
    original_model_load = nullptr;
    original_vehicle_render_dispatch = nullptr;
    original_trailer_visual_update = nullptr;
    original_trailer_render = nullptr;
    original_model_activate = nullptr;
    original_vehicle_accessory_collect = nullptr;
    original_vehicle_addon_finalize = nullptr;
    original_render_entry_populate = nullptr;
    original_final_base_model_create = nullptr;
    original_final_model_create = nullptr;
    original_final_accessory_insert = nullptr;
    original_set_parent = nullptr;
    original_set_transform = nullptr;
    std::lock_guard lock(accessory_models_mutex);
    accessory_source_models.clear();
    for (auto &source : accessory_source_nodes)
        source.store(0, std::memory_order_relaxed);
    for (auto &logged : logged_render_targets)
        logged.store(false, std::memory_order_relaxed);
    pending_accessory_models.clear();
    for (auto &node : pending_accessory_nodes)
        node.store(0, std::memory_order_relaxed);
    visible_accessory_models.clear();
    for (auto &node : visible_accessory_nodes)
        node.store(0, std::memory_order_relaxed);
    accessory_target_transform = {0.0f, -100.0f, 0.0f, 0, 0,
                                  1.0f, 0.0f, 0.0f, 0.0f};
    last_accessory_capture_tick = 0;
    logged_finalize_source_miss = false;
    logged_final_accessory_insert.store(false, std::memory_order_relaxed);
    final_model_create_diagnostics.store(0, std::memory_order_relaxed);
    final_accessory_ready_tick.store(0, std::memory_order_relaxed);
    driving_started_tick.store(0, std::memory_order_relaxed);
    logged_driving_scene_ready.store(false, std::memory_order_relaxed);
    player_trailer_object.store(0, std::memory_order_relaxed);
    {
        std::lock_guard frame_lock(prediction_frames_mutex);
        prediction_frames = {};
    }
    independent_frame_render_disabled.store(false,
                                            std::memory_order_relaxed);
    logged_independent_frame_render.store(false,
                                          std::memory_order_relaxed);
    logged_independent_frame_models.store(false,
                                          std::memory_order_relaxed);
    {
        std::lock_guard probe_lock(vehicle_render_probe_mutex);
        vehicle_render_probe_records.clear();
    }
}

bool validate_process_heap(const char *stage)
{
    if (HeapValidate(GetProcessHeap(), 0, nullptr)) return true;
    faulted = true;
    engine.enabled = false;
    log_line(std::string("[reverse-entity] Heap validation failed after ") + stage +
                 "; backend disabled.",
             SCS_LOG_TYPE_error);
    return false;
}

void log_diagnostic_once(bool &logged, const char *message)
{
    if (logged) return;
    log_line(message);
    logged = true;
}

void log_first_transform(const PrismTransform *transform)
{
    if (logged_transform_ok) return;
    std::ostringstream message;
    message << "[reverse-entity] Single-entity diagnostic: first transform ok at local ("
            << transform->x << ", " << transform->y << ", " << transform->z
            << "), sector (" << transform->sector_x << ", "
            << transform->sector_z << ").";
    log_line(message.str());
    logged_transform_ok = true;
}

std::string sha256_file(const std::filesystem::path &path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD received = 0;
    std::vector<std::uint8_t> object;
    std::array<std::uint8_t, 32> digest{};
    std::ifstream stream(path, std::ios::binary);
    if (!stream ||
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                          &received, 0) != 0)
    {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    object.resize(object_size);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::array<char, 1 << 16> buffer{};
    while (stream)
    {
        stream.read(buffer.data(), buffer.size());
        const auto count = static_cast<ULONG>(stream.gcount());
        if (count && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), count, 0) != 0)
        {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }
    }
    const auto status = BCryptFinishHash(hash, digest.data(),
                                         static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status != 0) return {};

    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : digest) output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

std::filesystem::path executable_path()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return buffer;
}

std::size_t executable_image_size(const std::uint8_t *base)
{
    if (!base) return 0;
    __try
    {
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return 0;
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(
            base + static_cast<std::size_t>(dos->e_lfanew));
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return 0;
        return nt->OptionalHeader.SizeOfImage;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool initialize_engine()
{
    const auto executable = executable_path();
    log_path = executable.parent_path().parent_path().parent_path() /
               "ETS2ReverseEntityRuntime.log";
    model_trace_path = executable.parent_path().parent_path().parent_path() /
                       "ETS2ReverseEntityModelTrace.log";
    world_probe_path = executable.parent_path().parent_path().parent_path() /
                       "ETS2ReverseWorldDispatchProbe.log";
    if (native_entity_backend_quarantined)
    {
        log_line("[reverse-entity] Native entity backend quarantined after heap-corruption detection.",
                 SCS_LOG_TYPE_warning);
        return false;
    }

    const auto digest = sha256_file(executable);
    engine.base = reinterpret_cast<std::uint8_t *>(GetModuleHandleW(nullptr));
    const auto image_size = executable_image_size(engine.base);
    if (!engine.base || image_size == 0)
    {
        log_line("[reverse-entity] Compatibility disabled: executable image metadata is unavailable; telemetry-only mode active.",
                 SCS_LOG_TYPE_error);
        return false;
    }
    const auto signature_reader =
        [&](std::uintptr_t rva, const std::uint8_t *bytes,
            std::size_t size)
    {
        return rva <= image_size && size <= image_size - rva &&
               std::memcmp(engine.base + rva, bytes, size) == 0;
    };
    bool exact_hash_match = false;
    std::size_t failed_hook = 0;
    active_build_profile = reverse_assist::compatibility::select_build_profile(
        digest, signature_reader, &exact_hash_match, &failed_hook);
    if (!active_build_profile)
    {
        std::ostringstream message;
        message << "[reverse-entity] Compatibility disabled: no BuildProfile matched sha256="
                << digest;
        if (failed_hook <
            reverse_assist::compatibility::verified_160_1_7_hooks.size())
            message << " failed-enabled-hook="
                    << reverse_assist::compatibility::verified_160_1_7_hooks[failed_hook].name;
        message << "; native hooks skipped, telemetry-only mode active.";
        log_line(message.str(), SCS_LOG_TYPE_warning);
        return false;
    }

    std::ostringstream compatibility;
    compatibility << "[reverse-entity] BuildProfile selected: id="
                  << active_build_profile->id << " game="
                  << active_build_profile->game_version << " match="
                  << (exact_hash_match ? "sha256" : "enabled-hook-signatures")
                  << " sha256=" << digest << " enabled-hooks="
                  << active_build_profile->enabled_hooks.size();
    log_line(compatibility.str());
    for (const auto &hook : active_build_profile->enabled_hooks)
    {
        std::ostringstream verified;
        verified << "[reverse-entity] Enabled hook verified: " << hook.name
                 << " rva=0x" << std::hex << hook.rva
                 << " signature-bytes=" << std::dec << hook.signature.size();
        log_line(verified.str());
    }

    if constexpr (trace_all_model_loads)
    {
        std::ofstream trace(model_trace_path, std::ios::trunc);
        trace << active_build_profile->game_version << " native model-load trace\n";
    }
    {
        std::ofstream trace(world_probe_path, std::ios::trunc);
        trace << active_build_profile->game_version
              << " read-only vehicle world-dispatch probe\n";
    }

    using reverse_assist::compatibility::HookId;
    const auto hook_rva = [](HookId id) {
        const auto *hook = reverse_assist::compatibility::find_hook(
            *active_build_profile, id);
        return hook ? hook->rva : std::uintptr_t{};
    };

    engine.model_load = reinterpret_cast<ModelLoad>(
        engine.base + hook_rva(HookId::model_load));
    engine.model_activate = reinterpret_cast<ModelActivate>(engine.base + model_activate_rva);
    engine.model_transfer = reinterpret_cast<ModelTransfer>(engine.base + model_transfer_rva);
    engine.model_parameter_init =
        reinterpret_cast<ModelParameterInit>(engine.base + model_parameter_init_rva);
    engine.vehicle_accessory_collect =
        reinterpret_cast<VehicleAccessoryCollect>(
            engine.base + vehicle_accessory_collect_rva);
    engine.vehicle_addon_finalize = reinterpret_cast<VehicleAddonFinalize>(
        engine.base + vehicle_addon_finalize_rva);
    engine.render_entry_populate = reinterpret_cast<RenderEntryPopulate>(
        engine.base + render_entry_populate_rva);
    engine.final_base_model_create = reinterpret_cast<FinalBaseModelCreate>(
        engine.base + final_base_model_create_rva);
    engine.final_model_create = reinterpret_cast<FinalModelCreate>(
        engine.base + final_model_create_rva);
    engine.final_accessory_insert = reinterpret_cast<FinalAccessoryInsert>(
        engine.base + final_accessory_insert_rva);
    engine.set_parent = reinterpret_cast<SetParent>(engine.base + set_parent_rva);
    engine.set_transform = reinterpret_cast<SetTransform>(engine.base + set_transform_rva);
    engine.vehicle_render_dispatch =
        reinterpret_cast<VehicleRenderDispatch>(
            engine.base + hook_rva(HookId::vehicle_render_dispatch));
    engine.trailer_visual_update =
        reinterpret_cast<TrailerVisualUpdate>(
            engine.base + hook_rva(HookId::trailer_visual_update));
    engine.trailer_render =
        reinterpret_cast<TrailerRender>(
            engine.base + hook_rva(HookId::trailer_render));
    engine.enabled = true;
    log_line("[reverse-entity] Compatibility checks passed for every enabled hook; native entity backend ready.");
    if constexpr (trace_all_model_loads)
        log_line("[reverse-entity] Model-path tracing enabled; independent render-model pool available.");
    if (single_entity_diagnostic)
        log_line("[reverse-entity] Visibility calibration active: +6.0 m lateral, +2.5 m height.");
    return true;
}

PrismTransform make_transform(double x, double y, double z, double heading,
                              double elevation = 0.0)
{
    auto split_axis = [](double value, float &local, std::int16_t &sector) {
        long long cell = 0;
        if (value > sector_size)
            cell = static_cast<long long>(std::ceil((value - sector_size) / sector_size));
        else if (value < -sector_size)
            cell = static_cast<long long>(std::floor((value + sector_size) / sector_size));
        double remainder = value - static_cast<double>(cell) * sector_size;
        auto clamped_cell = std::clamp<long long>(cell, INT16_MIN, INT16_MAX);
        local = static_cast<float>(remainder);
        sector = static_cast<std::int16_t>(clamped_cell);
    };

    PrismTransform transform{};
    split_axis(x, transform.x, transform.sector_x);
    split_axis(z, transform.z, transform.sector_z);
    transform.y = static_cast<float>(y);
    const auto yaw_half = heading * 0.5;
    const auto elevation_half = elevation * 0.5;
    const double cy = std::cos(yaw_half);
    const double sy = std::sin(yaw_half);
    const double cp = std::cos(elevation_half);
    const double sp = std::sin(elevation_half);
    // Prism line models extend along local +Z.  Yaw first, then rotate around
    // local X so a positive elevation raises the segment's forward endpoint.
    transform.qw = static_cast<float>(cy * cp);
    transform.qx = static_cast<float>(-cy * sp);
    transform.qy = static_cast<float>(sy * cp);
    transform.qz = static_cast<float>(sy * sp);
    return transform;
}

PrismTransform make_local_transform(double x, double y, double z, double heading)
{
    PrismTransform transform{};
    transform.x = static_cast<float>(x);
    transform.y = static_cast<float>(y);
    transform.z = static_cast<float>(z);
    transform.sector_x = 0;
    transform.sector_z = 0;
    const auto half = heading * 0.5;
    transform.qw = static_cast<float>(std::cos(half));
    transform.qy = static_cast<float>(std::sin(half));
    return transform;
}

void log_parent_attachment(std::uint64_t *model, std::uint64_t parent)
{
    std::ostringstream message;
    message << "[reverse-entity] Attached frame model " << model
            << " to vehicle-scene parent "
            << reinterpret_cast<void *>(parent) << ".";
    log_line(message.str());
}

std::uint64_t *unsafe_create_model(const char *path, std::uint64_t parent,
                                   const PrismTransform *initial_transform)
{
    if (!parent || !initial_transform) return nullptr;
    std::uint64_t output = 0;
    std::uint64_t owned = 0;
    const std::uint64_t default_look = 0;
    const std::uint64_t default_variant = 0;

    __try
    {
        native_creation_stage = 1;
        engine.model_load(&output, &path, &default_variant, &default_look, 0);
        if (!output) return nullptr;
        if (!validate_process_heap("model load")) return nullptr;
        log_diagnostic_once(logged_load_ok,
                            "[reverse-entity] Single-entity diagnostic: model load ok.");
        native_creation_stage = 2;
        // Transfer the engine owner_ptr into stable storage. Keeping the raw
        // loader result skips its ownership-state transition and corrupts the
        // intrusive reference count during later world/resource cleanup.
        engine.model_transfer(&owned, &output);
        if (!owned) return nullptr;
        if (!validate_process_heap("ownership transfer")) return nullptr;
        log_diagnostic_once(
            logged_transfer_ok,
            "[reverse-entity] Single-entity diagnostic: ownership transfer ok.");
        auto model = reinterpret_cast<std::uint64_t *>(owned);
        // Match the verified Sign stand world-model creation sequence exactly:
        // create the model unit, retain it, initialize its parameter packet, and
        // publish an absolute world transform. Do not attach a vehicle parent;
        // that would put the model back into the truck's composite hierarchy.
        native_creation_stage = 3;
        engine.model_parameter_init(model);
        if (!validate_process_heap("scene parameter initialization")) return nullptr;
        log_diagnostic_once(
            logged_parameter_init_ok,
            "[reverse-entity] Single-entity diagnostic: scene parameters initialized.");
        native_creation_stage = 4;
        engine.set_parent(reinterpret_cast<std::uint64_t>(model) + 0x10,
                          parent);
        engine.set_transform(reinterpret_cast<std::uint64_t>(model) + 0x10,
                             initial_transform);
        if (!validate_process_heap("parent attachment and initial transform"))
            return nullptr;
        log_parent_attachment(model, parent);
        native_creation_stage = 5;
        engine.model_activate(model, 0);
        if (!validate_process_heap("world activation")) return nullptr;
        log_diagnostic_once(
            logged_activate_ok,
            "[reverse-entity] Single-entity diagnostic: world activation ok.");
        native_creation_stage = 6;
        const auto vtable = *reinterpret_cast<std::uintptr_t **>(model);
        if (!vtable || !vtable[0x140 / sizeof(std::uintptr_t)])
            return nullptr;
        const auto finalize =
            reinterpret_cast<void(__fastcall *)(std::uint64_t *)>(
                vtable[0x140 / sizeof(std::uintptr_t)]);
        finalize(model);
        if (!validate_process_heap("render-state finalization")) return nullptr;
        log_diagnostic_once(
            logged_finalize_ok,
            "[reverse-entity] Single-entity diagnostic: render-state finalization ok.");
        native_creation_stage = 7;
        return model;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        engine.enabled = false;
        return nullptr;
    }
}

std::uint64_t *create_model(reverse_assist::BodyKind kind, std::uint64_t parent,
                            const PrismTransform *initial_transform)
{
    if (!engine.enabled || faulted) return nullptr;
    const char *path = kind == reverse_assist::BodyKind::tractor
        ? "/model/reverse_assist/tractor_box.pmd"
        : "/model/reverse_assist/trailer_box.pmd";
    auto *model = unsafe_create_model(path, parent, initial_transform);
    if (faulted)
    {
        log_line("[reverse-entity] Native model creation faulted at stage " +
                     std::to_string(native_creation_stage) +
                     "; backend disabled for this run.",
                 SCS_LOG_TYPE_error);
    }
    return model;
}

std::uint64_t *unsafe_create_render_model(
    reverse_assist::BodyKind kind, const PrismTransform *initial_transform)
{
    if (!initial_transform || !engine.enabled || faulted) return nullptr;

    const char *path = kind == reverse_assist::BodyKind::tractor
        ? "/model/reverse_assist/sweep_edge_blue.pmd"
        : "/model/reverse_assist/sweep_edge.pmd";
    std::uint64_t output = 0;
    std::uint64_t owned = 0;
    const std::uint64_t default_look = 0;
    const std::uint64_t default_variant = 0;

    __try
    {
        // Build a complete model instance for every prediction pose.  It has
        // no vehicle/trailer parent and is submitted explicitly from the
        // vehicle render pass, so detaching a trailer cannot destroy it.
        engine.model_load(&output, &path, &default_variant, &default_look, 0);
        if (!output) return nullptr;
        engine.model_transfer(&owned, &output);
        if (!owned) return nullptr;

        auto *model = reinterpret_cast<std::uint64_t *>(owned);
        engine.model_parameter_init(model);
        engine.set_transform(reinterpret_cast<std::uint64_t>(model) + 0x10,
                             initial_transform);
        engine.model_activate(model, 0);

        const auto vtable = *reinterpret_cast<std::uintptr_t **>(model);
        if (!vtable || !vtable[0x140 / sizeof(std::uintptr_t)])
            return nullptr;
        const auto finalize =
            reinterpret_cast<void(__fastcall *)(std::uint64_t *)>(
                vtable[0x140 / sizeof(std::uintptr_t)]);
        finalize(model);
        return model;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool unsafe_set_and_render_model(std::uint64_t *model,
                                 const PrismTransform *transform,
                                 std::uint64_t *render_context)
{
    if (!model || !transform || !render_context) return false;
    __try
    {
        engine.set_transform(reinterpret_cast<std::uint64_t>(model) + 0x10,
                             transform);
        const auto vtable = *reinterpret_cast<std::uintptr_t **>(model);
        if (!vtable) return false;
        const auto render = reinterpret_cast<
            void(__fastcall *)(std::uint64_t *, std::uint64_t *)>(
            vtable[0x90 / sizeof(std::uintptr_t)]);
        if (!render) return false;
        render(model, render_context);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void unsafe_hide_unused_prediction_models(
    std::array<std::uint64_t *, maximum_prediction_frames> &models,
    std::size_t first)
{
    const auto hidden = make_transform(0.0, -10000.0, 0.0, 0.0);
    for (std::size_t index = first; index < models.size(); ++index)
    {
        if (!models[index]) continue;
        __try
        {
            engine.set_transform(
                reinterpret_cast<std::uint64_t>(models[index]) + 0x10,
                &hidden);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            independent_frame_render_disabled.store(
                true, std::memory_order_release);
            return;
        }
    }
}

bool unsafe_render_independent_prediction_frames(
    std::uint64_t render_context)
{
    if (!render_context) return true;
    std::lock_guard model_lock(prediction_models_mutex);

    PredictionFrameSet frames{};
    {
        std::lock_guard lock(prediction_frames_mutex);
        frames = prediction_frames;
    }

    std::size_t tractor_index = 0;
    std::size_t trailer_index = 0;
    auto *context = reinterpret_cast<std::uint64_t *>(render_context);

    for (std::size_t index = 0; index < frames.count; ++index)
    {
        const auto &frame = frames.frames[index];
        auto &models = frame.kind == reverse_assist::BodyKind::tractor
            ? tractor_prediction_models
            : trailer_prediction_models;
        auto &kind_index = frame.kind == reverse_assist::BodyKind::tractor
            ? tractor_index
            : trailer_index;
        if (kind_index >= models.size()) continue;

        if (!models[kind_index])
        {
            models[kind_index] =
                unsafe_create_render_model(frame.kind, &frame.transform);
            if (!models[kind_index]) return false;
        }
        if (!unsafe_set_and_render_model(models[kind_index],
                                         &frame.transform, context))
            return false;
        ++kind_index;
    }

    unsafe_hide_unused_prediction_models(tractor_prediction_models,
                                         tractor_index);
    unsafe_hide_unused_prediction_models(trailer_prediction_models,
                                         trailer_index);

    if (frames.count != 0 &&
        !logged_independent_frame_models.exchange(
            true, std::memory_order_relaxed))
    {
        std::ostringstream message;
        message << "[reverse-entity] Created independent render-only "
                   "prediction models; active poses="
                << frames.count << ".";
        log_line(message.str());
    }
    if (frames.count != 0 &&
        !logged_independent_frame_render.exchange(
            true, std::memory_order_relaxed))
        log_line("[reverse-entity] Tractor-side independent swept-area "
                 "rendering is active.");
    return true;
}

bool unsafe_call_set_transform(std::uint64_t *model, const PrismTransform *transform)
{
    __try
    {
        // model_inst_u embeds its placement_node_t at +0x10.
        const auto setter = original_set_transform ? original_set_transform
                                                   : engine.set_transform;
        setter(reinterpret_cast<std::uint64_t>(model) + 0x10, transform);
        // HeapValidate walks the whole process heap and is intentionally only
        // used for the first transform. Running it every frame costs 10-20 FPS.
        if (!logged_transform_ok)
        {
            if (!validate_process_heap("first transform update")) return false;
            log_first_transform(transform);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool unsafe_call_set_parent(std::uint64_t *model, std::uint64_t parent)
{
    __try
    {
        const auto setter =
            original_set_parent ? original_set_parent : engine.set_parent;
        setter(reinterpret_cast<std::uint64_t>(model) + 0x10, parent);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool set_entity_transform(std::uint64_t *model, const PrismTransform *transform)
{
    if (unsafe_call_set_transform(model, transform)) return true;
    log_line("[reverse-entity] Discarded a stale model instance.",
             SCS_LOG_TYPE_warning);
    return false;
}

void apply_transform_to_accessories(const PrismTransform &transform)
{
    std::vector<std::uint64_t *> captured;
    {
        std::lock_guard lock(accessory_models_mutex);
        accessory_target_transform = transform;
        captured = visible_accessory_models;
    }

    std::vector<std::uint64_t *> stale;
    for (auto *model : captured)
        if (!unsafe_call_set_transform(model, &transform))
            stale.push_back(model);

    if (stale.empty()) return;
    std::lock_guard lock(accessory_models_mutex);
    for (auto *model : stale)
    {
        std::erase(visible_accessory_models, model);
        const auto placement_node =
            reinterpret_cast<std::uint64_t>(model) + 0x10;
        for (auto &node : visible_accessory_nodes)
            if (node.load(std::memory_order_relaxed) == placement_node)
                node.store(0, std::memory_order_relaxed);
    }
    log_line("[reverse-entity] Discarded stale final accessory instance.",
             SCS_LOG_TYPE_warning);
}

Entity *entity_at(std::vector<Entity> &pool, std::size_t index,
                  reverse_assist::BodyKind kind, std::uint64_t parent,
                  const PrismTransform *initial_transform)
{
    if (index >= maximum_entities) return nullptr;
    while (pool.size() <= index) pool.push_back({});
    auto &entity = pool[index];
    if (entity.model && (entity.kind != kind || entity.parent != parent))
    {
        const auto hidden = make_local_transform(0.0, -100.0, 0.0, 0.0);
        set_entity_transform(entity.model, &hidden);
        entity.model = nullptr;
    }
    if (!entity.model)
    {
        entity.kind = kind;
        entity.parent = parent;
        entity.model = create_model(kind, parent, initial_transform);
    }
    return entity.model ? &entity : nullptr;
}

std::uint64_t unsafe_read_parent(std::uint64_t *model)
{
    __try
    {
        // placement_node_t begins at model + 0x10; its parent pointer is +0x40.
        return *reinterpret_cast<std::uint64_t *>(
            reinterpret_cast<std::uint8_t *>(model) + 0x50);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

std::vector<std::uint64_t> current_accessory_parents()
{
    std::vector<std::uint64_t *> models;
    {
        std::lock_guard lock(accessory_models_mutex);
        models = visible_accessory_models;
    }
    std::vector<std::uint64_t> parents;
    for (auto it = models.rbegin(); it != models.rend(); ++it)
    {
        const auto parent = unsafe_read_parent(*it);
        if (parent != 0 &&
            std::find(parents.begin(), parents.end(), parent) == parents.end())
            parents.push_back(parent);
    }
    return parents;
}

bool unsafe_destroy_model(std::uint64_t *model)
{
    __try
    {
        std::uint64_t stored = reinterpret_cast<std::uint64_t>(model);
        std::uint64_t empty = 0;
        engine.model_transfer(&stored, &empty);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        engine.enabled = false;
        return false;
    }
}

void destroy_prediction_render_models()
{
    independent_frame_render_disabled.store(true,
                                            std::memory_order_release);
    {
        std::lock_guard frame_lock(prediction_frames_mutex);
        prediction_frames = {};
    }

    std::lock_guard model_lock(prediction_models_mutex);
    std::size_t released = 0;
    bool destroy_fault = false;
    const auto release_pool = [&](auto &models) {
        for (auto *&model : models)
        {
            if (!model) continue;
            if (!unsafe_destroy_model(model))
                destroy_fault = true;
            else
                ++released;
            model = nullptr;
        }
    };
    release_pool(tractor_prediction_models);
    release_pool(trailer_prediction_models);

    logged_independent_frame_render.store(false,
                                          std::memory_order_relaxed);
    logged_independent_frame_models.store(false,
                                          std::memory_order_relaxed);
    if (released != 0)
    {
        log_line("[reverse-entity] Released " +
                 std::to_string(released) +
                 " independent prediction models before scene teardown.");
    }
    if (destroy_fault)
    {
        log_line("[reverse-entity] Prediction model release faulted; "
                 "render backend disabled.",
                 SCS_LOG_TYPE_error);
    }
}

void destroy_tail(std::vector<Entity> &pool, std::size_t first)
{
    bool destroy_fault = false;
    for (std::size_t i = first; i < pool.size(); ++i)
    {
        if (pool[i].model && !unsafe_destroy_model(pool[i].model))
            destroy_fault = true;
        pool[i].model = nullptr;
    }
    pool.resize(first);
    if (destroy_fault)
        log_line("[reverse-entity] Native model destruction faulted; backend disabled.",
                 SCS_LOG_TYPE_error);
}

void destroy_all_entities()
{
    destroy_tail(entities, 0);
    destroy_tail(trailer_entities, 0);
}

std::vector<reverse_assist::WheelSpec> configured_wheels(
    const VehicleConfiguration &configuration,
    const std::array<WheelTelemetry, maximum_config_wheels>
        *wheel_telemetry = nullptr)
{
    std::vector<reverse_assist::WheelSpec> result;
    const std::size_t count = configuration.wheel_count > 0
        ? std::min<std::size_t>(configuration.wheel_count,
                                maximum_config_wheels)
        : maximum_config_wheels;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto &wheel = configuration.wheels[index];
        if (!wheel.position_valid) continue;
        reverse_assist::WheelSpec specification{
            wheel.position.x,
            wheel.position.z,
            wheel.steerable,
            wheel.steerable_valid};
        if (wheel_telemetry)
        {
            const auto &runtime = (*wheel_telemetry)[index];
            // Wheel contact is the authoritative support-state signal.  The
            // lift channel is only a fallback for game/mod combinations that
            // do not publish per-wheel contact.
            if (runtime.on_ground_valid)
            {
                specification.on_ground = runtime.on_ground;
                specification.on_ground_known = true;
            }
            else if (wheel.liftable_valid && wheel.liftable &&
                     runtime.lift_valid)
            {
                specification.on_ground = runtime.lift < 0.50f;
                specification.on_ground_known = true;
            }
            if (runtime.steering_valid)
            {
                specification.steering_angle =
                    static_cast<double>(runtime.steering) *
                    2.0 * reverse_assist::pi;
                specification.steering_angle_known = true;
            }
        }
        result.push_back(specification);
    }
    return result;
}

bool is_first_trailer_configuration(const char *id)
{
    return id &&
        (std::strcmp(id, SCS_TELEMETRY_CONFIG_trailer) == 0 ||
         std::strcmp(id, "trailer.0") == 0);
}

void update_vehicle_configuration(
    const scs_telemetry_configuration_t *configuration)
{
    if (!configuration || !configuration->id ||
        !configuration->attributes)
        return;

    const bool is_truck =
        std::strcmp(configuration->id,
                    SCS_TELEMETRY_CONFIG_truck) == 0;
    const bool is_trailer =
        is_first_trailer_configuration(configuration->id);
    if (!is_truck && !is_trailer) return;

    VehicleConfiguration next{};
    for (const scs_named_value_t *attribute =
             configuration->attributes;
         attribute->name; ++attribute)
    {
        if (std::strcmp(
                attribute->name,
                SCS_TELEMETRY_CONFIG_ATTRIBUTE_hook_position) == 0 &&
            attribute->value.type == SCS_VALUE_TYPE_fvector)
        {
            next.hook = attribute->value.value_fvector;
            next.hook_valid = true;
        }
        else if (std::strcmp(
                     attribute->name,
                     SCS_TELEMETRY_CONFIG_ATTRIBUTE_wheel_count) == 0 &&
                 attribute->value.type == SCS_VALUE_TYPE_u32)
        {
            next.wheel_count = std::min<scs_u32_t>(
                attribute->value.value_u32.value,
                static_cast<scs_u32_t>(maximum_config_wheels));
        }
        else if (std::strcmp(
                     attribute->name,
                     SCS_TELEMETRY_CONFIG_ATTRIBUTE_wheel_position) == 0 &&
                 attribute->value.type == SCS_VALUE_TYPE_fvector &&
                 attribute->index < maximum_config_wheels)
        {
            auto &wheel = next.wheels[attribute->index];
            wheel.position = attribute->value.value_fvector;
            wheel.position_valid = true;
            next.wheel_count = std::max<scs_u32_t>(
                next.wheel_count, attribute->index + 1);
        }
        else if (std::strcmp(
                     attribute->name,
                     SCS_TELEMETRY_CONFIG_ATTRIBUTE_wheel_steerable) == 0 &&
                 attribute->value.type == SCS_VALUE_TYPE_bool &&
                 attribute->index < maximum_config_wheels)
        {
            auto &wheel = next.wheels[attribute->index];
            wheel.steerable =
                attribute->value.value_bool.value != 0;
            wheel.steerable_valid = true;
            next.wheel_count = std::max<scs_u32_t>(
                next.wheel_count, attribute->index + 1);
        }
        else if (std::strcmp(
                     attribute->name,
                     SCS_TELEMETRY_CONFIG_ATTRIBUTE_wheel_liftable) == 0 &&
                 attribute->value.type == SCS_VALUE_TYPE_bool &&
                 attribute->index < maximum_config_wheels)
        {
            auto &wheel = next.wheels[attribute->index];
            wheel.liftable =
                attribute->value.value_bool.value != 0;
            wheel.liftable_valid = true;
            next.wheel_count = std::max<scs_u32_t>(
                next.wheel_count, attribute->index + 1);
        }
    }

    {
        std::lock_guard lock(telemetry_configuration_mutex);
        if (is_truck)
            telemetry.truck_configuration = next;
        else
            telemetry.trailer_configuration = next;
    }

    const auto wheels = configured_wheels(next);
    std::ostringstream message;
    message << std::fixed << std::setprecision(2);
    if (is_truck)
    {
        const double hook_z = next.hook_valid
            ? static_cast<double>(next.hook.z)
            : std::numeric_limits<double>::quiet_NaN();
        const auto geometry =
            reverse_assist::estimate_tractor_spec(wheels, hook_z);
        message << "[reverse-entity] Truck geometry: wheels="
                << wheels.size()
                << " wheelbase=" << geometry.wheelbase
                << " length=" << geometry.length
                << " width=" << geometry.width
                << " hook-offset=" << geometry.hook_from_rear;
    }
    else
    {
        const double hook_z = next.hook_valid
            ? static_cast<double>(next.hook.z) : 0.0;
        const auto geometry =
            reverse_assist::estimate_trailer_spec(
                0.0, hook_z, wheels);
        message << "[reverse-entity] Trailer geometry: wheels="
                << wheels.size()
                << " axle-to-hook=" << geometry.axle_to_hitch
                << " rear-overhang=" << geometry.rear_overhang
                << " width=" << geometry.width;
    }
    log_line(message.str());
}

void update_prediction_frame_targets()
{
    // Prediction rendering
    // uses plugin-owned model instances so the prediction survives detaching
    // the trailer and every sampled pose keeps its own transform.
    {
        std::lock_guard lock(prediction_frames_mutex);
        prediction_frames = {};
    }
    if (!telemetry.driving || !telemetry.truck_valid ||
        telemetry.gear >= 0)
        return;

    const double truck_heading =
        telemetry.truck.orientation.heading * 2.0 * reverse_assist::pi;
    VehicleConfiguration truck_configuration;
    VehicleConfiguration trailer_configuration;
    std::array<WheelTelemetry, maximum_config_wheels>
        truck_wheel_telemetry{};
    std::array<WheelTelemetry, maximum_config_wheels>
        trailer_wheel_telemetry{};
    {
        std::lock_guard lock(telemetry_configuration_mutex);
        truck_configuration = telemetry.truck_configuration;
        trailer_configuration = telemetry.trailer_configuration;
        truck_wheel_telemetry = telemetry.truck_wheels;
        trailer_wheel_telemetry = telemetry.trailer_wheels;
    }
    const auto truck_wheels =
        configured_wheels(truck_configuration,
                          &truck_wheel_telemetry);
    const double truck_hook_z = truck_configuration.hook_valid
        ? static_cast<double>(truck_configuration.hook.z)
        : std::numeric_limits<double>::quiet_NaN();
    const auto tractor_geometry =
        reverse_assist::estimate_tractor_spec(
            truck_wheels, truck_hook_z);

    double trailer_heading = truck_heading;
    std::vector<reverse_assist::TrailerSpec> trailers;
    if (telemetry.trailer_connected && telemetry.trailer_valid)
    {
        trailer_heading =
            telemetry.trailer.orientation.heading * 2.0 *
            reverse_assist::pi;
        const double relative_heading =
            reverse_assist::normalize_angle(
                trailer_heading - truck_heading);
        const auto trailer_wheels =
            configured_wheels(trailer_configuration,
                              &trailer_wheel_telemetry);
        if (!trailer_wheels.empty() &&
            trailer_configuration.hook_valid)
        {
            trailers.push_back(
                reverse_assist::estimate_trailer_spec(
                    relative_heading,
                    trailer_configuration.hook.z,
                    trailer_wheels));
        }
        else
        {
            trailers.push_back(
                {relative_heading, 7.2, 3.0, 2.55});
        }
    }
    const auto prediction = reverse_assist::predict_reverse_boxes(
        telemetry.steering,
        tractor_geometry.wheelbase,
        tractor_geometry.length,
        tractor_geometry.width,
        trailers,
        0.0, tractor_geometry.rear_axle_z,
        tractor_geometry.center_from_rear,
        5.0, 0.05, 0.25,
        tractor_geometry.hook_from_rear);

    struct BodyTrack
    {
        std::vector<reverse_assist::BoundaryPoint> left;
        std::vector<reverse_assist::BoundaryPoint> right;
    };

    BodyTrack tractor_track;
    BodyTrack trailer_track;
    tractor_track.left.reserve(prediction.boxes.size());
    tractor_track.right.reserve(prediction.boxes.size());
    trailer_track.left.reserve(prediction.boxes.size());
    trailer_track.right.reserve(prediction.boxes.size());
    constexpr double envelope_clearance = 0.18;

    for (const auto &box : prediction.boxes)
    {
        if (box.kind == reverse_assist::BodyKind::trailer &&
            (trailers.empty() || box.trailer_index != 0))
            continue;

        const bool is_tractor =
            box.kind == reverse_assist::BodyKind::tractor;
        const double anchor_heading =
            is_tractor ? truck_heading : trailer_heading;
        const auto &anchor =
            is_tractor ? telemetry.truck : telemetry.trailer;
        // The predictor now starts at the configured rear axle in the
        // truck's native local coordinate system.  Keep that coordinate
        // system intact: re-anchoring each body at its telemetry placement
        // discarded the rear-bogie and short-trailer offsets that were just
        // derived from wheel and hook configuration.
        reverse_assist::GroundBox mapped_body = box;
        auto boundary =
            reverse_assist::reverse_leading_boundary(
                mapped_body, envelope_clearance);

        const auto apply_body_plane =
            [&](reverse_assist::BoundaryPoint &point) {
                const double point_world_dx =
                    point.x * std::cos(truck_heading) +
                    point.z * std::sin(truck_heading);
                const double point_world_dz =
                    -point.x * std::sin(truck_heading) +
                    point.z * std::cos(truck_heading);
                const double point_world_x =
                    telemetry.truck.position.x +
                    point_world_dx;
                const double point_world_z =
                    telemetry.truck.position.z +
                    point_world_dz;
                const double anchor_dx =
                    point_world_x - anchor.position.x;
                const double anchor_dz =
                    point_world_z - anchor.position.z;
                const double body_local_x =
                    anchor_dx * std::cos(anchor_heading) -
                    anchor_dz * std::sin(anchor_heading);
                const double body_local_z =
                    anchor_dx * std::sin(anchor_heading) +
                    anchor_dz * std::cos(anchor_heading);
                const double pitch =
                    anchor.orientation.pitch * 2.0 *
                    reverse_assist::pi;
                const double roll =
                    anchor.orientation.roll * 2.0 *
                    reverse_assist::pi;
                point.y = anchor.position.y + 0.35 +
                    reverse_assist::body_plane_height(
                        body_local_x, body_local_z,
                        pitch, roll);
            };
        apply_body_plane(boundary.left);
        apply_body_plane(boundary.right);

        // Blue tractor and orange trailer outlines always follow the two
        // reverse-leading rear corners of their own body.  They must never
        // switch to a front corner as steering or articulation increases.
        auto &track = is_tractor ? tractor_track : trailer_track;
        track.left.push_back(boundary.left);
        track.right.push_back(boundary.right);
    }

    const auto smooth_boundary = [](auto &boundary) {
            if (boundary.size() < 3) return;
            const auto original = boundary;
            for (std::size_t sample = 1;
                 sample + 1 < boundary.size(); ++sample)
            {
                boundary[sample].x =
                    original[sample - 1].x * 0.25 +
                    original[sample].x * 0.50 +
                    original[sample + 1].x * 0.25;
                boundary[sample].y =
                    original[sample - 1].y * 0.25 +
                    original[sample].y * 0.50 +
                    original[sample + 1].y * 0.25;
                boundary[sample].z =
                    original[sample - 1].z * 0.25 +
                    original[sample].z * 0.50 +
                    original[sample + 1].z * 0.25;
            }
        };
    smooth_boundary(tractor_track.left);
    smooth_boundary(tractor_track.right);
    smooth_boundary(trailer_track.left);
    smooth_boundary(trailer_track.right);

    PredictionFrameSet frames{};
    const auto publish_boundary =
        [&](const auto &boundary, reverse_assist::BodyKind kind) {
            for (std::size_t sample = 0;
                 sample + 1 < boundary.size(); ++sample)
            {
                if (frames.count >= frames.frames.size()) break;
                const auto &from = boundary[sample];
                const auto &to = boundary[sample + 1];

                const double segment_x = to.x - from.x;
                const double segment_y = to.y - from.y;
                const double segment_z = to.z - from.z;
                const double horizontal_length =
                    std::hypot(segment_x, segment_z);
                if (std::hypot(horizontal_length, segment_y) < 0.10)
                    continue;

                const double midpoint_x = (from.x + to.x) * 0.5;
                const double midpoint_y = (from.y + to.y) * 0.5;
                const double midpoint_z = (from.z + to.z) * 0.5;
                const double world_dx =
                    midpoint_x * std::cos(truck_heading) +
                    midpoint_z * std::sin(truck_heading);
                const double world_dz =
                    -midpoint_x * std::sin(truck_heading) +
                    midpoint_z * std::cos(truck_heading);
                const double local_heading =
                    std::atan2(segment_x, segment_z);
                const double elevation =
                    std::atan2(segment_y, horizontal_length);

                auto &frame = frames.frames[frames.count++];
                frame.kind = kind;
                frame.transform = make_transform(
                    telemetry.truck.position.x + world_dx,
                    midpoint_y,
                    telemetry.truck.position.z + world_dz,
                    reverse_assist::normalize_angle(
                        truck_heading + local_heading),
                    elevation);
            }
        };
    publish_boundary(tractor_track.left,
                     reverse_assist::BodyKind::tractor);
    publish_boundary(tractor_track.right,
                     reverse_assist::BodyKind::tractor);
    publish_boundary(trailer_track.left,
                     reverse_assist::BodyKind::trailer);
    publish_boundary(trailer_track.right,
                     reverse_assist::BodyKind::trailer);
    if (frames.count == 0) return;

    {
        std::lock_guard lock(prediction_frames_mutex);
        prediction_frames = frames;
    }
}

void update_entities()
{
    update_prediction_frame_targets();
    if (!engine.enabled || faulted) return;
    if (!telemetry.driving || !telemetry.truck_valid) return;

    // Both the telemetry "started" event and the first placement sample can
    // arrive while the profile/vehicle unit graph is still being assembled.
    // Do not mutate the accessory or create a native model until the driving
    // scene and the final drawable accessory record have independently remained
    // stable for a full warm-up interval.
    const auto now = GetTickCount64();
    const auto started =
        driving_started_tick.load(std::memory_order_acquire);
    if (started == 0 || now - started < driving_scene_warmup_ms)
        return;
    if constexpr (!model_load_hook_only)
    {
        const auto drawable_ready =
            final_accessory_ready_tick.load(std::memory_order_acquire);
        if (drawable_ready == 0 ||
            now - drawable_ready < final_drawable_warmup_ms)
            return;
    }

    bool accessory_ready = false;
    {
        std::lock_guard lock(accessory_models_mutex);
        accessory_ready = !visible_accessory_models.empty();
    }
    if (!accessory_ready) return;

    if (!logged_driving_scene_ready.exchange(true,
                                             std::memory_order_relaxed))
        log_line("[reverse-entity] Driving scene and final accessory record are stable; native reverse creation enabled.");

    if constexpr (!independent_world_entity_enabled) return;

    if (telemetry.gear >= 0)
    {
        const auto hidden = make_local_transform(0.0, -100.0, 0.0, 0.0);
        for (auto &entity : entities)
        {
            if (entity.model && !set_entity_transform(entity.model, &hidden))
                entity.model = nullptr;
        }
        return;
    }

    std::vector<reverse_assist::TrailerSpec> trailers;
    if (telemetry.trailer_connected && telemetry.trailer_valid)
    {
        const double truck_heading = telemetry.truck.orientation.heading * 2.0 * reverse_assist::pi;
        const double trailer_heading = telemetry.trailer.orientation.heading * 2.0 * reverse_assist::pi;
        trailers.push_back({reverse_assist::normalize_angle(trailer_heading - truck_heading),
                            7.2, 3.0, 2.55});
    }
    const auto prediction = reverse_assist::predict_reverse_boxes(
        telemetry.steering, 3.8, 7.0, 2.55, trailers, 0.0, 0.0, -2.4, 12.0, 0.10, 1.0);

    // Use the farthest predicted footprint of the last trailer. With no
    // trailer, fall back to the farthest tractor footprint.
    const reverse_assist::GroundBox *selected = nullptr;
    const auto desired_kind = trailers.empty() ? reverse_assist::BodyKind::tractor
                                                : reverse_assist::BodyKind::trailer;
    for (const auto &box : prediction.boxes)
    {
        if (box.kind == desired_kind) selected = &box;
    }
    if (!selected) return;

    if (!entities.empty() && entities.front().kind != desired_kind)
    {
        const auto hidden = make_local_transform(0.0, -100.0, 0.0, 0.0);
        for (auto &entity : entities)
            if (entity.model) set_entity_transform(entity.model, &hidden);
        entities.clear();
    }

    // Attach one independent prediction model to each game-owned
    // interior/exterior vehicle-scene parent.  The pose is intentionally local
    // to that parent; this keeps the object in the normal Prism3D visibility
    // chain without touching camera or render records.
    const auto parents = current_accessory_parents();
    if (parents.empty()) return;
    const auto transform = make_local_transform(
        selected->pose.x, 0.35, selected->pose.z,
        selected->pose.heading);
    const auto count = std::min(parents.size(), maximum_entities);
    for (std::size_t index = 0; index < count; ++index)
    {
        auto *entity = entity_at(
            entities, index, desired_kind, parents[index], &transform);
        if (!entity) continue;
        if (!set_entity_transform(entity->model, &transform))
            entity->model = nullptr;
    }
    for (std::size_t index = count; index < entities.size(); ++index)
    {
        if (entities[index].model)
        {
            const auto hidden =
                make_local_transform(0.0, -100.0, 0.0, 0.0);
            set_entity_transform(entities[index].model, &hidden);
        }
    }
}

SCSAPI_VOID channel_callback(const scs_string_t, const scs_u32_t,
                             const scs_value_t *const value, const scs_context_t context)
{
    if (!value) return;
    const auto channel_context =
        reinterpret_cast<std::uintptr_t>(context);
    switch (channel_context)
    {
    case 1:
        telemetry.gear = value->value_s32.value;
        break;
    case 2:
        telemetry.steering = value->value_float.value;
        break;
    case 3:
        telemetry.truck = value->value_dplacement;
        telemetry.truck_valid = true;
        break;
    case 4:
        telemetry.trailer_connected = value->value_bool.value != 0;
        if (!telemetry.trailer_connected)
        {
            std::lock_guard lock(telemetry_configuration_mutex);
            telemetry.trailer_wheels = {};
        }
        break;
    case 5:
        telemetry.trailer = value->value_dplacement;
        telemetry.trailer_valid = true;
        break;
    default:
        break;
    }

    const auto update_wheel =
        [channel_context, value](
            std::uintptr_t base,
            std::array<WheelTelemetry, maximum_config_wheels> &wheels,
            int field)
    {
        if (channel_context < base ||
            channel_context >= base + maximum_config_wheels)
            return false;
        const std::size_t index =
            static_cast<std::size_t>(channel_context - base);
        std::lock_guard lock(telemetry_configuration_mutex);
        auto &wheel = wheels[index];
        if (field == 0 && value->type == SCS_VALUE_TYPE_bool)
        {
            wheel.on_ground = value->value_bool.value != 0;
            wheel.on_ground_valid = true;
        }
        else if (field == 1 &&
                 value->type == SCS_VALUE_TYPE_float)
        {
            wheel.lift = value->value_float.value;
            wheel.lift_valid = true;
        }
        else if (field == 2 &&
                 value->type == SCS_VALUE_TYPE_float)
        {
            wheel.steering = value->value_float.value;
            wheel.steering_valid = true;
        }
        return true;
    };
    if (update_wheel(truck_wheel_on_ground_context,
                     telemetry.truck_wheels, 0) ||
        update_wheel(truck_wheel_lift_context,
                     telemetry.truck_wheels, 1) ||
        update_wheel(truck_wheel_steering_context,
                     telemetry.truck_wheels, 2) ||
        update_wheel(trailer_wheel_on_ground_context,
                     telemetry.trailer_wheels, 0) ||
        update_wheel(trailer_wheel_lift_context,
                     telemetry.trailer_wheels, 1) ||
        update_wheel(trailer_wheel_steering_context,
                     telemetry.trailer_wheels, 2))
        return;
}

SCSAPI_VOID event_callback(const scs_event_t event,
                           const void *const event_info,
                           const scs_context_t)
{
    if (event == SCS_TELEMETRY_EVENT_configuration)
    {
        update_vehicle_configuration(
            static_cast<const scs_telemetry_configuration_t *>(
                event_info));
    }
    else if (event == SCS_TELEMETRY_EVENT_started)
    {
        independent_frame_render_disabled.store(false,
                                                std::memory_order_release);
        telemetry.driving = true;
        telemetry.truck_valid = false;
        telemetry.trailer_valid = false;
        {
            std::lock_guard lock(telemetry_configuration_mutex);
            telemetry.truck_wheels = {};
            telemetry.trailer_wheels = {};
        }
        driving_started_tick.store(GetTickCount64(),
                                   std::memory_order_release);
        logged_driving_scene_ready.store(false,
                                         std::memory_order_relaxed);
    }
    else if (event == SCS_TELEMETRY_EVENT_paused)
    {
        destroy_prediction_render_models();
        if (!entities.empty() && telemetry.truck_valid)
        {
            const auto hidden = make_local_transform(0.0, -100.0, 0.0, 0.0);
            for (auto &entity : entities)
                if (entity.model) set_entity_transform(entity.model, &hidden);
        }
        // Keep the hidden leaked owner alive until process exit. Releasing the
        // reverse-engineered owner during scene teardown previously caused
        // shutdown hangs; a later driving scene receives a fresh instance.
        entities.clear();
        telemetry.driving = false;
        telemetry.truck_valid = false;
        telemetry.trailer_valid = false;
        {
            std::lock_guard lock(telemetry_configuration_mutex);
            telemetry.truck_wheels = {};
            telemetry.trailer_wheels = {};
        }
        player_trailer_object.store(0, std::memory_order_release);
        driving_started_tick.store(0, std::memory_order_release);
        logged_driving_scene_ready.store(false,
                                         std::memory_order_relaxed);
    }
    else if (event == SCS_TELEMETRY_EVENT_frame_end) update_entities();
}

bool register_channel(const scs_telemetry_init_params_v101_t *params, const char *name,
                      scs_value_type_t type, std::uintptr_t context)
{
    return params->register_for_channel(name, SCS_U32_NIL, type,
        SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value,
        channel_callback, reinterpret_cast<scs_context_t>(context)) == SCS_RESULT_ok;
}

bool register_indexed_channel(
    const scs_telemetry_init_params_v101_t *params,
    const char *name,
    scs_u32_t index,
    scs_value_type_t type,
    std::uintptr_t context)
{
    return params->register_for_channel(
        name, index, type,
        SCS_TELEMETRY_CHANNEL_FLAG_each_frame |
            SCS_TELEMETRY_CHANNEL_FLAG_no_value,
        channel_callback,
        reinterpret_cast<scs_context_t>(context)) ==
        SCS_RESULT_ok;
}
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,
                                 const scs_telemetry_init_params_t *const params)
{
    if (version != SCS_TELEMETRY_VERSION_1_01) return SCS_RESULT_unsupported;
    const auto *api = static_cast<const scs_telemetry_init_params_v101_t *>(params);
    game_log = api->common.log;
    if (initialize_engine())
    {
        if constexpr (lifecycle_hooks_enabled)
        {
            if (!install_model_load_hook()) engine.enabled = false;
        }
        else
        {
            engine.enabled = false;
            log_line("[reverse-entity] Safety baseline active: native lifecycle hooks disabled.");
        }
    }

    const bool channels =
        register_channel(api, SCS_TELEMETRY_TRUCK_CHANNEL_displayed_gear, SCS_VALUE_TYPE_s32, 1) &&
        register_channel(api, SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, SCS_VALUE_TYPE_float, 2) &&
        register_channel(api, SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_VALUE_TYPE_dplacement, 3) &&
        register_channel(api, SCS_TELEMETRY_TRAILER_CHANNEL_connected, SCS_VALUE_TYPE_bool, 4) &&
        register_channel(api, SCS_TELEMETRY_TRAILER_CHANNEL_world_placement, SCS_VALUE_TYPE_dplacement, 5);
    std::size_t wheel_channels = 0;
    for (scs_u32_t index = 0;
         index < static_cast<scs_u32_t>(maximum_config_wheels);
         ++index)
    {
        wheel_channels += register_indexed_channel(
            api, SCS_TELEMETRY_TRUCK_CHANNEL_wheel_on_ground,
            index, SCS_VALUE_TYPE_bool,
            truck_wheel_on_ground_context + index);
        wheel_channels += register_indexed_channel(
            api, SCS_TELEMETRY_TRUCK_CHANNEL_wheel_lift,
            index, SCS_VALUE_TYPE_float,
            truck_wheel_lift_context + index);
        wheel_channels += register_indexed_channel(
            api, SCS_TELEMETRY_TRUCK_CHANNEL_wheel_steering,
            index, SCS_VALUE_TYPE_float,
            truck_wheel_steering_context + index);
        wheel_channels += register_indexed_channel(
            api, SCS_TELEMETRY_TRAILER_CHANNEL_wheel_on_ground,
            index, SCS_VALUE_TYPE_bool,
            trailer_wheel_on_ground_context + index);
        wheel_channels += register_indexed_channel(
            api, SCS_TELEMETRY_TRAILER_CHANNEL_wheel_lift,
            index, SCS_VALUE_TYPE_float,
            trailer_wheel_lift_context + index);
        wheel_channels += register_indexed_channel(
            api, SCS_TELEMETRY_TRAILER_CHANNEL_wheel_steering,
            index, SCS_VALUE_TYPE_float,
            trailer_wheel_steering_context + index);
    }
    log_line("[reverse-entity] Per-wheel telemetry channels registered: " +
             std::to_string(wheel_channels));
    const bool events =
        api->register_for_event(SCS_TELEMETRY_EVENT_frame_end, event_callback, nullptr) == SCS_RESULT_ok &&
        api->register_for_event(SCS_TELEMETRY_EVENT_started, event_callback, nullptr) == SCS_RESULT_ok &&
        api->register_for_event(SCS_TELEMETRY_EVENT_paused, event_callback, nullptr) == SCS_RESULT_ok &&
        api->register_for_event(SCS_TELEMETRY_EVENT_configuration, event_callback, nullptr) == SCS_RESULT_ok;
    return channels && events ? SCS_RESULT_ok : SCS_RESULT_generic_error;
}

SCSAPI_VOID scs_telemetry_shutdown(void)
{
    log_line("[reverse-entity] Telemetry shutdown started.");
    destroy_prediction_render_models();
    engine.enabled = false;
    remove_model_load_hook();
    log_line("[reverse-entity] Telemetry shutdown completed.");
}
