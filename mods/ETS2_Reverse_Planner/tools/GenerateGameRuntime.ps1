$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = Join-Path $projectRoot 'src\upstream\native_entity_runtime.cpp'
$outputDirectory = Join-Path $projectRoot 'src\game_runtime\generated'
$outputPath = Join-Path $outputDirectory 'reverse_planner_game_runtime.generated.cpp'
$source = [IO.File]::ReadAllText($sourcePath)

$source = $source.Replace(
    '#include <windows.h>',
    "#include <windows.h>`r`n#include <shlobj.h>")
$source = $source.Replace(
    '#include "..\world_runtime\reverse_kinematics.hpp"',
    '#include "../../upstream/reverse_kinematics.hpp"' +
    "`r`n" + '#include "../../stage1a_planner.hpp"')
$source = $source.Replace(
    'constexpr std::size_t maximum_prediction_frames = 96;',
    'constexpr std::size_t maximum_prediction_frames = 384;')
$source = $source.Replace(
    '"/model/reverse_assist/sweep_edge_blue.pmd"',
    '"/model/reverse_planer/sweep_edge_gren.pmd"')
$source = $source.Replace(
    '"/model/reverse_assist/sweep_edge.pmd"',
    '"/model/reverse_planer/sweep_edge_redd.pmd"')
$source = $source.Replace('[reverse-entity]', '[reverse-planner]')
$source = $source.Replace(
    'bool model_load_hook_installed = false;',
    "bool model_load_hook_installed = false;`r`nstd::atomic<bool> runtime_active{};")
$source = $source.Replace(
    "    original_vehicle_render_dispatch(wrapper, render_context);`r`n    if (!independent_frame_render_disabled.load(std::memory_order_acquire)",
    "    original_vehicle_render_dispatch(wrapper, render_context);`r`n    if (!runtime_active.load(std::memory_order_acquire)) return;`r`n    if (!independent_frame_render_disabled.load(std::memory_order_acquire)")
$source = $source.Replace(
    "        player_trailer_object.store(`r`n            *reinterpret_cast<const std::uint64_t *>(wrapper + 0xc8),`r`n            std::memory_order_release);",
    "        const auto trailer =`r`n            *reinterpret_cast<const std::uint64_t *>(wrapper + 0xc8);`r`n        // Preserve the last non-null physics trailer across detachment. The`r`n        // native loading guide becomes visible only after that transition.`r`n        if (trailer)`r`n            player_trailer_object.store(trailer, std::memory_order_release);")
$source = $source.Replace(
    "        player_trailer_object.store(0, std::memory_order_release);",
    "        // A transient/stale wrapper must not discard the last candidate.`r`n        // Every later dereference is guarded by SEH and exact resource ID.")
if ($source -notmatch 'Preserve the last non-null physics trailer') {
    throw 'Failed to preserve the detached trailer object for guide locking.'
}

# The legacy runtime loads first and has already replaced the in-memory hook
# prologues. For an exact known executable hash, use the immutable on-disk hash
# as the compatibility gate instead of requiring those mutable bytes to remain
# pristine. Unknown binaries still require the complete signature fallback.
$profileSelectionBefore = @'
    active_build_profile = reverse_assist::compatibility::select_build_profile(
        digest, signature_reader, &exact_hash_match, &failed_hook);
'@
$profileSelectionAfter = @'
    active_build_profile = nullptr;
    for (const auto &profile : reverse_assist::compatibility::build_profiles)
    {
        if (!profile.executable_sha256.empty() &&
            profile.executable_sha256 == digest)
        {
            active_build_profile = &profile;
            exact_hash_match = true;
            break;
        }
    }
    if (!active_build_profile && sidecar_required_layout_matches(executable))
    {
        for (const auto &profile : reverse_assist::compatibility::build_profiles)
        {
            if (!profile.signature_compatible_fallback) continue;
            const auto *render_hook = reverse_assist::compatibility::find_hook(
                profile,
                reverse_assist::compatibility::HookId::vehicle_render_dispatch);
            if (render_hook && signature_reader(
                    render_hook->rva, render_hook->signature.data(),
                    render_hook->signature.size()))
            {
                active_build_profile = &profile;
                break;
            }
        }
    }
'@
$source = $source.Replace($profileSelectionBefore, $profileSelectionAfter)
if ($source -notmatch 'profile\.executable_sha256 == digest') {
    throw 'Failed to install sidecar-safe exact-hash profile selection.'
}

# Unknown executable hashes must be checked against the immutable file image,
# not the already-hooked process image. The legacy sidecar may load first and
# legitimately replace the render-dispatch prologue. Reading the PE section
# from disk allows hash-only hotfixes with the same verified layout to pass,
# while changed layouts still fail closed.
$diskCompatibilityHelper = @'
bool executable_file_rva_matches(const std::filesystem::path &path,
                                 std::uintptr_t rva,
                                 const std::uint8_t *expected,
                                 std::size_t size)
{
    if (!expected || !size) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    IMAGE_DOS_HEADER dos{};
    input.read(reinterpret_cast<char *>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
        return false;
    input.seekg(dos.e_lfanew, std::ios::beg);
    IMAGE_NT_HEADERS64 nt{};
    input.read(reinterpret_cast<char *>(&nt), sizeof(nt));
    if (!input || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;

    std::uint64_t file_offset = 0;
    bool mapped = false;
    if (rva < nt.OptionalHeader.SizeOfHeaders)
    {
        file_offset = rva;
        mapped = rva + size <= nt.OptionalHeader.SizeOfHeaders;
    }
    else
    {
        input.seekg(dos.e_lfanew + sizeof(DWORD) +
                    sizeof(IMAGE_FILE_HEADER) +
                    nt.FileHeader.SizeOfOptionalHeader, std::ios::beg);
        for (WORD index = 0; index < nt.FileHeader.NumberOfSections; ++index)
        {
            IMAGE_SECTION_HEADER section{};
            input.read(reinterpret_cast<char *>(&section), sizeof(section));
            if (!input) return false;
            const auto extent = std::max<std::uint32_t>(
                section.Misc.VirtualSize, section.SizeOfRawData);
            if (rva < section.VirtualAddress ||
                rva >= section.VirtualAddress + extent)
                continue;
            const auto delta = rva - section.VirtualAddress;
            if (delta + size > section.SizeOfRawData) return false;
            file_offset = section.PointerToRawData + delta;
            mapped = true;
            break;
        }
    }
    if (!mapped) return false;
    std::array<std::uint8_t, 64> actual{};
    if (size > actual.size()) return false;
    input.clear();
    input.seekg(static_cast<std::streamoff>(file_offset), std::ios::beg);
    input.read(reinterpret_cast<char *>(actual.data()),
               static_cast<std::streamsize>(size));
    return input && std::memcmp(actual.data(), expected, size) == 0;
}

bool sidecar_required_layout_matches(const std::filesystem::path &path)
{
    struct RequiredFunction
    {
        std::uintptr_t rva;
        std::array<std::uint8_t, 16> signature;
    };
    // Only functions called or hooked by this sidecar are compatibility
    // gates. Unused legacy trailer hooks must not reject a harmless hash-only
    // hotfix.
    constexpr std::array required{
        RequiredFunction{0x015184f0,
            {0x40,0x53,0x56,0x41,0x56,0x48,0x83,0xec,
             0x40,0x80,0xbc,0x24,0x80,0x00,0x00,0x00}},
        RequiredFunction{0x015288d0,
            {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,
             0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xda}},
        RequiredFunction{0x0032cfc0,
            {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,
             0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48}},
        RequiredFunction{0x0040cc30,
            {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,
             0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83}},
        RequiredFunction{0x013149c0,
            {0x48,0x83,0xec,0x28,0x4c,0x8b,0xc1,0x4c,
             0x8b,0xca,0x48,0x8b,0x49,0x40,0x48,0x3b}},
        RequiredFunction{0x01314a80,
            {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,
             0xec,0x40,0x48,0x8b,0xd9,0x48,0x8b,0xfa}},
        RequiredFunction{0x00772020,
            {0x40,0x53,0x57,0x48,0x83,0xec,0x28,0x48,
             0x8b,0x42,0x18,0x48,0x8b,0xda,0x48,0x8b}},
    };
    return std::all_of(required.begin(), required.end(),
        [&](const RequiredFunction &function)
        {
            return executable_file_rva_matches(
                path, function.rva, function.signature.data(),
                function.signature.size());
        });
}

std::filesystem::path reverse_planner_state_directory()
{
    PWSTR known_documents = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,
                                       KF_FLAG_DEFAULT, nullptr,
                                       &known_documents)) &&
        known_documents)
    {
        const std::filesystem::path result =
            std::filesystem::path(known_documents) /
            L"Euro Truck Simulator 2/mod/ETS2_Reverse_Planner_Stage0/build";
        CoTaskMemFree(known_documents);
        return result;
    }
    if (known_documents) CoTaskMemFree(known_documents);
    wchar_t profile[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) != 0)
        return std::filesystem::path(profile) /
            L"Documents/Euro Truck Simulator 2/mod/"
            L"ETS2_Reverse_Planner_Stage0/build";
    return {};
}

'@
$source = $source.Replace('bool initialize_engine()',
                          $diskCompatibilityHelper + 'bool initialize_engine()')
$source = $source.Replace(
@'
    const auto signature_reader =
        [&](std::uintptr_t rva, const std::uint8_t *bytes,
            std::size_t size)
    {
        return rva <= image_size && size <= image_size - rva &&
               std::memcmp(engine.base + rva, bytes, size) == 0;
    };
'@,
@'
    const auto signature_reader =
        [&](std::uintptr_t rva, const std::uint8_t *bytes,
            std::size_t size)
    {
        return rva <= image_size && size <= image_size - rva &&
               executable_file_rva_matches(executable, rva, bytes, size);
    };
'@)
if ($source -notmatch 'executable_file_rva_matches\(executable, rva, bytes, size\)' -or
    $source -notmatch 'SHGetKnownFolderPath\(FOLDERID_Documents') {
    throw 'Failed to install restart/hotfix compatibility helpers.'
}

# The planner is a sidecar to the existing posture-assistant runtime. It owns
# only the render-dispatch hook it needs. The module is pinned after installing
# that hook and leaves a dormant pass-through detour resident until process
# exit. This makes either SCS telemetry shutdown order safe: no trampoline can
# ever point into an unloaded planner DLL, and the legacy DLL remains untouched.
$hookReplacement = @'
bool install_model_load_hook()
{
    if (!engine.vehicle_render_dispatch) return false;
    if (model_load_hook_installed)
    {
        runtime_active.store(true, std::memory_order_release);
        return true;
    }

    const auto initialize_status = MH_Initialize();
    if (initialize_status != MH_OK &&
        initialize_status != MH_ERROR_ALREADY_INITIALIZED)
    {
        log_line("[reverse-planner] MinHook initialization failed: " +
                     std::string(MH_StatusToString(initialize_status)) + ".",
                 SCS_LOG_TYPE_error);
        return false;
    }

    const auto create_status = MH_CreateHook(
        reinterpret_cast<LPVOID>(engine.vehicle_render_dispatch),
        reinterpret_cast<LPVOID>(&hooked_vehicle_render_dispatch),
        reinterpret_cast<LPVOID *>(&original_vehicle_render_dispatch));
    if (create_status != MH_OK)
    {
        log_line("[reverse-planner] Render sidecar hook creation failed: " +
                     std::string(MH_StatusToString(create_status)) + ".",
                 SCS_LOG_TYPE_error);
        MH_Uninitialize();
        original_vehicle_render_dispatch = nullptr;
        return false;
    }

    const auto enable_status = MH_EnableHook(
        reinterpret_cast<LPVOID>(engine.vehicle_render_dispatch));
    if (enable_status != MH_OK)
    {
        MH_RemoveHook(reinterpret_cast<LPVOID>(engine.vehicle_render_dispatch));
        MH_Uninitialize();
        original_vehicle_render_dispatch = nullptr;
        log_line("[reverse-planner] Render sidecar hook enable failed: " +
                     std::string(MH_StatusToString(enable_status)) + ".",
                 SCS_LOG_TYPE_error);
        return false;
    }

    HMODULE pinned_module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&install_model_load_hook),
            &pinned_module))
    {
        MH_DisableHook(reinterpret_cast<LPVOID>(
            engine.vehicle_render_dispatch));
        MH_RemoveHook(reinterpret_cast<LPVOID>(
            engine.vehicle_render_dispatch));
        MH_Uninitialize();
        original_vehicle_render_dispatch = nullptr;
        log_line("[reverse-planner] Could not pin the render sidecar module; "
                 "hook was rolled back.", SCS_LOG_TYPE_error);
        return false;
    }

    model_load_hook_installed = true;
    runtime_active.store(true, std::memory_order_release);
    log_line("[reverse-planner] Installed one cooperative render-dispatch "
             "sidecar hook; module pinned until process exit.");
    return true;
}

void remove_model_load_hook()
{
    // Do not remove the hook here. Another independently built MinHook client
    // may have chained through this trampoline. Pinning keeps both the detour
    // and trampoline valid until Windows tears down the process. Shutdown only
    // turns this detour into a cheap pass-through.
    runtime_active.store(false, std::memory_order_release);
    if (model_load_hook_installed)
        log_line("[reverse-planner] Render sidecar dormant; resident hook "
                 "retained safely until process exit.");
}

bool validate_process_heap
'@
$hookPattern = '(?s)bool install_model_load_hook\(\).*?\r?\nvoid remove_model_load_hook\(\).*?\r?\n\}\r?\n\r?\nbool validate_process_heap'
$hookedSource = [Text.RegularExpressions.Regex]::Replace(
    $source, $hookPattern, $hookReplacement, 1)
if ($hookedSource -eq $source -or
    $hookedSource -notmatch 'one cooperative render-dispatch') {
    throw 'Failed to replace the legacy multi-hook lifecycle.'
}
$source = $hookedSource

$axleSnapshotHelper = @'
stage1a::TrailerAxleSnapshot stable_trailer_axle_candidate(
    const VehicleConfiguration &configuration,
    const std::array<WheelTelemetry, maximum_config_wheels> &axle_telemetry,
    double relative_heading,double hook_z)
{
    stage1a::TrailerAxleSnapshot candidate{};
    const auto wheels=configured_wheels(configuration,&axle_telemetry);
    const auto specification=reverse_assist::estimate_trailer_spec(
        relative_heading,hook_z,wheels);
    candidate.axle_to_hitch_m=specification.axle_to_hitch;
    candidate.axle_steering_rad=specification.axle_steering;
    candidate.rear_overhang_m=specification.rear_overhang;
    candidate.width_m=specification.width;

    const std::size_t count=configuration.wheel_count>0
        ? std::min<std::size_t>(configuration.wheel_count,
                                maximum_config_wheels)
        : maximum_config_wheels;
    for (std::size_t index=0;index<count&&index<64;++index)
    {
        const auto &wheel=configuration.wheels[index];
        if (!wheel.position_valid) continue;
        const auto &runtime=axle_telemetry[index];
        bool grounded=true;
        if (runtime.on_ground_valid) grounded=runtime.on_ground;
        else if (wheel.liftable_valid&&wheel.liftable&&runtime.lift_valid)
            grounded=runtime.lift<0.50f;
        if (grounded) candidate.grounded_mask|=(std::uint64_t{1}<<index);
        if (wheel.liftable_valid&&wheel.liftable&&runtime.lift_valid&&
            runtime.lift>0.02f&&runtime.lift<0.98f)
            candidate.lift_transition_in_progress=true;
    }
    return candidate;
}

'@
$axleSnapshotMarker = 'bool is_first_trailer_configuration(const char *id)'
$source = $source.Replace(
    $axleSnapshotMarker,
    $axleSnapshotHelper + $axleSnapshotMarker)
if ($source -notmatch 'stable_trailer_axle_candidate') {
    throw 'Failed to install stable trailer-axle telemetry helper.'
}

$replacement = @'
bool unsafe_copy_runtime_bytes(std::uintptr_t address,void *output,
                               std::size_t size)
{
    if (!address || !output || !size) return false;
    __try
    {
        std::memcpy(output,reinterpret_cast<const void *>(address),size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool unsafe_runtime_ascii_equals(std::uintptr_t address,const char *expected)
{
    if (!address || !expected) return false;
    __try
    {
        for (std::size_t index=0;index<96;++index)
        {
            const char actual=*reinterpret_cast<const char *>(address+index);
            if (actual!=expected[index]) return false;
            if (!actual) return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return false;
}

bool unsafe_runtime_resource_is(std::uintptr_t resource,const char *path)
{
    if (!resource) return false;
    __try
    {
        const auto descriptor=
            *reinterpret_cast<const std::uintptr_t *>(resource+0x10);
        if (!descriptor) return false;
        const auto text=
            *reinterpret_cast<const std::uintptr_t *>(descriptor+0x28);
        return unsafe_runtime_ascii_equals(text,path);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool unsafe_runtime_model_has_resource(std::uintptr_t model,const char *path)
{
    if (!model) return false;
    std::array<std::uintptr_t,0x400/sizeof(std::uintptr_t)> first{};
    if (!unsafe_copy_runtime_bytes(model,first.data(),sizeof(first)))
        return false;
    const auto inspect=[&](std::uintptr_t candidate) {
        return candidate>=0x10000000000ULL&&
               candidate<=0x7FFFFFFFFFFFULL&&
               unsafe_runtime_resource_is(candidate,path);
    };
    if (inspect(model)) return true;
    for (const auto pointer:first)
    {
        if (inspect(pointer)) return true;
        if (pointer<0x10000000000ULL||pointer>0x7FFFFFFFFFFFULL) continue;
        std::array<std::uintptr_t,0x100/sizeof(std::uintptr_t)> second{};
        if (!unsafe_copy_runtime_bytes(pointer,second.data(),sizeof(second)))
            continue;
        for (const auto nested:second)
            if (inspect(nested)) return true;
    }
    return false;
}

bool unsafe_runtime_has_unique_loading_guide(std::uint64_t physics_trailer)
{
    if (!physics_trailer) return false;
    __try
    {
        const auto guide_model=*reinterpret_cast<const std::uintptr_t *>(
            physics_trailer+0x1110);
        const auto guide_visible=*reinterpret_cast<const std::uint8_t *>(
            physics_trailer+0x1120);
        return guide_visible!=0&&guide_model!=0&&
            unsafe_runtime_model_has_resource(
                guide_model,"/model/symbol/loading.pmd");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

#pragma pack(push,1)
struct RuntimeVec3 { float x{},y{},z{}; };
struct RuntimeQuat { float w{},x{},y{},z{}; };
struct RuntimePlacement
{
    RuntimeVec3 position{};
    std::int16_t sector_x{},sector_z{};
    RuntimeQuat rotation{};
};
struct RuntimeAaBox { RuntimeVec3 start{},end{}; };
struct RuntimeArrayHeader
{
    std::uintptr_t vtable{},value{};
    std::uint64_t size{},capacity{};
    std::uintptr_t allocator{};
};
#pragma pack(pop)
static_assert(sizeof(RuntimePlacement)==0x20);
static_assert(sizeof(RuntimeAaBox)==0x18);
static_assert(sizeof(RuntimeArrayHeader)==0x28);

RuntimeVec3 runtime_rotate(RuntimeVec3 value,const RuntimeQuat &q)
{
    const float x2=q.x+q.x,y2=q.y+q.y,z2=q.z+q.z;
    return {
        value.x*(1-y2*q.y-z2*q.z)+value.y*(x2*q.y-z2*q.w)+
            value.z*(x2*q.z+y2*q.w),
        value.x*(x2*q.y+z2*q.w)+value.y*(1-x2*q.x-z2*q.z)+
            value.z*(y2*q.z-x2*q.w),
        value.x*(x2*q.z-y2*q.w)+value.y*(y2*q.z+x2*q.w)+
            value.z*(1-x2*q.x-y2*q.y)};
}

// ETS2 1.60 keeps the trailer coupling node in the trailer physics graph even
// after standard telemetry removes the detached trailer configuration. This
// path was calibrated against the SDK hook on both sides of a real attach /
// detach transition. Treat it as version-specific and reject implausible data
// instead of silently feeding corrupt geometry to the planner.
bool unsafe_runtime_read_detached_hook(
    std::uint64_t physics_trailer,RuntimeVec3 *hook)
{
    if (!physics_trailer||!hook) return false;
    __try
    {
        const auto configuration=*reinterpret_cast<const std::uintptr_t *>(
            physics_trailer+0x200);
        if (!configuration) return false;
        const auto candidate=*reinterpret_cast<const RuntimeVec3 *>(
            configuration+0x6d0);
        if (!std::isfinite(candidate.x)||!std::isfinite(candidate.y)||
            !std::isfinite(candidate.z)||std::abs(candidate.x)>4.0f||
            candidate.y<-2.0f||candidate.y>5.0f||candidate.z>=0.0f||
            candidate.z<-25.0f)
            return false;
        *hook=candidate;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

double runtime_front_heading(const RuntimeQuat &rotation)
{
    const auto actor_rear=runtime_rotate({0.0f,0.0f,1.0f},rotation);
    return stage0::wrap_angle(
        std::atan2(actor_rear.x,actor_rear.z)+stage0::pi);
}

std::uintptr_t unsafe_runtime_game_traffic_holder()
{
    static std::uintptr_t cached_holder=0;
    static bool attempted=false;
    if (attempted) return cached_holder;
    attempted=true;
    if (!engine.base) return 0;
    __try
    {
        const auto base=reinterpret_cast<std::uintptr_t>(engine.base);
        const auto *dos=reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic!=IMAGE_DOS_SIGNATURE) return 0;
        const auto *nt=reinterpret_cast<const IMAGE_NT_HEADERS64 *>(
            base+dos->e_lfanew);
        if (nt->Signature!=IMAGE_NT_SIGNATURE) return 0;
        const auto *section=IMAGE_FIRST_SECTION(nt);
        const std::uint8_t *text=nullptr;
        std::size_t text_size=0;
        for (WORD index=0;index<nt->FileHeader.NumberOfSections;++index)
        {
            if (std::memcmp(section[index].Name,".text",5)==0)
            {
                text=reinterpret_cast<const std::uint8_t *>(
                    base+section[index].VirtualAddress);
                text_size=std::max<std::size_t>(
                    section[index].Misc.VirtualSize,
                    section[index].SizeOfRawData);
                break;
            }
        }
        if (!text||text_size<23) return 0;
        constexpr std::array<int,23> pattern{
            0x48,0x8b,0xd9,0x48,0x8b,0x0d,-1,-1,-1,-1,0x48,0x85,0xc9,
            0x74,-1,0x48,0x8b,0x83,-1,-1,-1,-1,0x48};
        std::uintptr_t match=0;
        int matches=0;
        for (std::size_t offset=0;offset+pattern.size()<=text_size;++offset)
        {
            bool equal=true;
            for (std::size_t index=0;index<pattern.size();++index)
                if (pattern[index]>=0&&text[offset+index]!=pattern[index])
                    { equal=false; break; }
            if (!equal) continue;
            match=reinterpret_cast<std::uintptr_t>(text+offset);
            if (++matches>1) return 0;
        }
        if (matches!=1) return 0;
        const auto displacement=*reinterpret_cast<const std::int32_t *>(
            match+6);
        cached_holder=match+10+displacement;
        return cached_holder;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool unsafe_runtime_scan_unique_loading_target(
    stage0::Vec2 truck_position,stage0::Pose2 *kingpin,
    double *estimated_height_m,bool *hook_verified)
{
    if (!kingpin||!estimated_height_m||!hook_verified) return false;
    const auto holder=unsafe_runtime_game_traffic_holder();
    if (!holder) return false;
    __try
    {
        const auto game_traffic=
            *reinterpret_cast<const std::uintptr_t *>(holder);
        if (!game_traffic) return false;
        const auto trailers=*reinterpret_cast<const RuntimeArrayHeader *>(
            game_traffic+0x230);
        if (!trailers.value||trailers.size>128||
            trailers.capacity<trailers.size) return false;
        int visible_exact_count=0;
        stage0::Pose2 selected{};
        double selected_height=0.0;
        bool selected_hook_verified=false;
        for (std::uint64_t index=0;index<trailers.size;++index)
        {
            const auto entity=*reinterpret_cast<const std::uintptr_t *>(
                trailers.value+index*sizeof(std::uintptr_t));
            if (!entity) continue;
            const auto placement=*reinterpret_cast<const RuntimePlacement *>(
                entity+0x28);
            const auto bounds=*reinterpret_cast<const RuntimeAaBox *>(
                entity+0x48);
            const auto wrapper=*reinterpret_cast<const std::uintptr_t *>(
                entity+0xf0);
            if (!wrapper) continue;
            const auto physics=*reinterpret_cast<const std::uint64_t *>(
                wrapper+0x08);
            if (!unsafe_runtime_has_unique_loading_guide(physics)) continue;
            const stage0::Vec2 actor_origin{
                placement.position.x+placement.sector_x*sector_size,
                placement.position.z+placement.sector_z*sector_size};
            const RuntimeVec3 local_center{
                (bounds.start.x+bounds.end.x)*0.5f,
                (bounds.start.y+bounds.end.y)*0.5f,
                (bounds.start.z+bounds.end.z)*0.5f};
            const auto center_offset=runtime_rotate(
                local_center,placement.rotation);
            const stage0::Vec2 center{
                actor_origin.x+center_offset.x,
                actor_origin.y+center_offset.z};
            if (stage1a::length(stage1a::subtract(center,truck_position))>
                stage0::local_scan_radius_m) continue;
            const double heading=runtime_front_heading(placement.rotation);
            const double width=std::abs(bounds.end.x-bounds.start.x);
            const double length=std::abs(bounds.end.z-bounds.start.z);
            RuntimeVec3 detached_hook{};
            if (unsafe_runtime_read_detached_hook(physics,&detached_hook))
            {
                const auto hook_offset=runtime_rotate(
                    detached_hook,placement.rotation);
                selected={{actor_origin.x+hook_offset.x,
                           actor_origin.y+hook_offset.z},heading};
                selected_height=placement.position.y+hook_offset.y;
                selected_hook_verified=true;
            }
            else
            {
                const auto geometry=stage0::estimate_trailer_target(
                    center,heading,{width,length});
                selected={geometry.kingpin,heading};
                selected_height=placement.position.y;
                selected_hook_verified=false;
            }
            ++visible_exact_count;
        }
        if (visible_exact_count!=1) return false;
        *kingpin=selected;
        *estimated_height_m=selected_height;
        *hook_verified=selected_hook_verified;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void update_prediction_frame_targets()
{
    static std::size_t arrow_frame_start = 0;
    static std::size_t arrow_frame_count = 0;
    static bool arrow_suffix_valid = false;
    static bool required_steering_valid = false;
    static double required_steering_rad = 0.0;
    static bool failed_pose_valid = false;
    static bool failed_pose_was_stable = false;
    static stage0::Pose2 failed_tractor_pose{};
    static stage0::Pose2 failed_trailer_pose{};
    static stage0::Pose2 failed_target_pose{};
    static double failed_axle_to_hitch = 0.0;
    static double failed_axle_steering = 0.0;
    static bool successful_pose_valid = false;
    static stage0::Pose2 successful_tractor_pose{};
    static stage0::Pose2 successful_trailer_pose{};
    static stage0::Pose2 successful_target_pose{};
    const auto clear_frames = [&] {
        std::lock_guard lock(prediction_frames_mutex);
        prediction_frames = {};
        arrow_suffix_valid = false;
        arrow_frame_start = 0;
        arrow_frame_count = 0;
    };
    static bool was_reverse = false;
    static ULONGLONG last_replan_tick = 0;
    static ULONGLONG last_failure_log_tick = 0;
    static ULONGLONG failure_started_tick = 0;
    static bool previous_connection_state=false;
    static double steering_correction = 0.0;
    static stage1a::StableTrailerAxleFilter trailer_axle_filter;
    static bool trailer_axle_was_connected=false;
    static scs_u32_t trailer_axle_identity_wheel_count=0;
    static double trailer_axle_identity_hook_z=
        std::numeric_limits<double>::quiet_NaN();
    const bool connection_state_changed=
        previous_connection_state!=telemetry.trailer_connected;
    previous_connection_state=telemetry.trailer_connected;
    if (connection_state_changed)
    {
        clear_frames();
        failure_started_tick=0;
        steering_correction=0.0;
        required_steering_valid=false;
        failed_pose_valid=false;
        successful_pose_valid=false;
    }

    // A disconnected interval is an identity boundary even when the next
    // trailer happens to have the same wheel count and hook coordinates.
    if (!telemetry.trailer_connected)
    {
        trailer_axle_filter.reset();
        trailer_axle_was_connected=false;
        trailer_axle_identity_wheel_count=0;
        trailer_axle_identity_hook_z=std::numeric_limits<double>::quiet_NaN();
    }
    else if (!trailer_axle_was_connected)
    {
        trailer_axle_filter.reset();
        trailer_axle_was_connected=true;
    }

    if (!telemetry.driving || !telemetry.truck_valid || telemetry.gear >= 0)
    {
        if (was_reverse) clear_frames();
        was_reverse = false;
        failure_started_tick = 0;
        steering_correction = 0.0;
        required_steering_valid = false;
        failed_pose_valid = false;
        successful_pose_valid = false;
        return;
    }
    // Product scope: detached tractor-to-kingpin guidance is intentionally
    // disabled. Do not scan a loading guide, consume a detached seed, publish
    // steering arrows, or start the one-second unreachable-X timer.
    if (!telemetry.trailer_connected || !telemetry.trailer_valid)
    {
        clear_frames();
        was_reverse = false;
        failure_started_tick = 0;
        steering_correction = 0.0;
        required_steering_valid = false;
        failed_pose_valid = false;
        successful_pose_valid = false;
        return;
    }

    struct PlannerSeed
    {
        bool valid = false;
        stage1a::PlannerMode mode = stage1a::PlannerMode::inactive;
        stage0::Pose2 target{};
        std::vector<stage1a::OrientedBox> obstacles;
    };
    static PlannerSeed seed;
    static std::filesystem::file_time_type loaded_stamp{};
    static bool logged_seed_failure = false;
    const auto build_directory = reverse_planner_state_directory();
    if (build_directory.empty())
    {
        clear_frames();
        return;
    }
    const auto path_file = build_directory / L"stage1a_best_path.csv";
    const auto diagnostics_file =
        build_directory / L"stage1a_planner_diagnostics.txt";
    const auto obstacles_file =
        build_directory / L"stage1a_known_obstacles.csv";
    std::error_code file_error;
    const auto stamp = std::filesystem::last_write_time(path_file, file_error);
    const bool entering_reverse = !was_reverse;
    was_reverse = true;
    if (file_error)
    {
        clear_frames();
        if (!logged_seed_failure)
        {
            log_line("[reverse-planner] Dynamic planner seed path is missing.",
                     SCS_LOG_TYPE_warning);
            logged_seed_failure = true;
        }
        return;
    }

    const bool seed_changed = stamp != loaded_stamp;
    if (seed_changed)
    {
        PlannerSeed next;
        std::ifstream diagnostics(diagnostics_file);
        std::string line;
        while (std::getline(diagnostics, line))
        {
            if (line == "PlannerMode=TRAILER_TO_PARKING_TARGET")
                next.mode = stage1a::PlannerMode::trailer_to_parking_target;
        }

        std::ifstream path_input(path_file);
        std::getline(path_input, line);
        std::array<double, 11> last{};
        bool have_row = false;
        while (std::getline(path_input, line))
        {
            std::replace(line.begin(), line.end(), ',', ' ');
            std::istringstream fields(line);
            std::array<double, 11> row{};
            bool complete = true;
            for (double &value : row)
                if (!(fields >> value)) { complete = false; break; }
            if (complete) { last = row; have_row = true; }
        }
        if (have_row && next.mode != stage1a::PlannerMode::inactive)
        {
            next.target = {{last[6], last[7]}, last[8]};

            std::ifstream obstacle_input(obstacles_file);
            std::getline(obstacle_input, line);
            while (std::getline(obstacle_input, line))
            {
                std::replace(line.begin(), line.end(), ',', ' ');
                std::istringstream fields(line);
                std::string label;
                stage1a::OrientedBox obstacle;
                if (fields >> label >> obstacle.center.x >> obstacle.center.y >>
                              obstacle.heading_rad >> obstacle.width_m >>
                              obstacle.length_m)
                {
                    obstacle.label = label;
                    next.obstacles.push_back(obstacle);
                }
            }
            next.valid = true;
        }
        seed = std::move(next);
        loaded_stamp = stamp;
        logged_seed_failure = false;
        if (seed.valid)
            log_line("[reverse-planner] Loaded fixed target for continuous "
                     "full-route replanning; mode=" +
                     std::string(stage1a::mode_name(seed.mode)) + ".");
    }

    if (!seed.valid)
    {
        clear_frames();
        if (!logged_seed_failure)
        {
            log_line("[reverse-planner] Dynamic planner seed is invalid; "
                     "green route cleared.", SCS_LOG_TYPE_warning);
            logged_seed_failure = true;
        }
        return;
    }

    const ULONGLONG now = GetTickCount64();

    VehicleConfiguration truck_configuration;
    VehicleConfiguration trailer_configuration;
    std::array<WheelTelemetry, maximum_config_wheels> truck_wheel_telemetry{};
    std::array<WheelTelemetry, maximum_config_wheels> trailer_wheel_telemetry{};
    {
        std::lock_guard lock(telemetry_configuration_mutex);
        truck_configuration = telemetry.truck_configuration;
        trailer_configuration = telemetry.trailer_configuration;
        truck_wheel_telemetry = telemetry.truck_wheels;
        trailer_wheel_telemetry = telemetry.trailer_wheels;
    }
    const auto truck_wheels = configured_wheels(
        truck_configuration, &truck_wheel_telemetry);
    const double truck_hook_z = truck_configuration.hook_valid
        ? static_cast<double>(truck_configuration.hook.z)
        : std::numeric_limits<double>::quiet_NaN();
    const auto tractor_spec = reverse_assist::estimate_tractor_spec(
        truck_wheels, truck_hook_z);

    const double truck_actor_heading =
        telemetry.truck.orientation.heading * 2.0 * reverse_assist::pi;
    const double truck_front_heading =
        stage1a::physical_front_heading_from_actor_rear(
            truck_actor_heading);
    const stage0::Vec2 truck_origin{
        telemetry.truck.position.x, telemetry.truck.position.z};
    const auto tractor_rear_axle = stage1a::actor_local_to_world(
        truck_origin, truck_actor_heading, 0.0, tractor_spec.rear_axle_z);

    stage1a::PlanRequest request;
    request.mode = stage1a::PlannerMode::trailer_to_parking_target;
    request.tractor_start = {tractor_rear_axle, truck_front_heading};
    request.geometry.tractor_wheelbase_m = tractor_spec.wheelbase;
    request.geometry.tractor_width_m = tractor_spec.width;
    request.geometry.tractor_length_m = tractor_spec.length;
    request.geometry.tractor_center_from_reference_m =
        -tractor_spec.center_from_rear;
    request.geometry.tractor_reference_to_hitch_m =
        -tractor_spec.hook_from_rear;
    request.tractor_start_curvature_m_inv=
        stage1a::steering_independent_route_start_curvature_m_inv();
    request.search_planner_initial_curvature = true;
    request.planner_initial_curvature_seed_limit = 5;
    request.obstacles = seed.obstacles;

    if (truck_configuration.hook_valid)
    {
        const auto configured_fifth_wheel=stage1a::actor_local_to_world(
            truck_origin,truck_actor_heading,
            static_cast<double>(truck_configuration.hook.x),
            static_cast<double>(truck_configuration.hook.z));
        const auto rear_to_fifth_wheel=stage1a::subtract(
            configured_fifth_wheel,tractor_rear_axle);
        request.geometry.tractor_reference_to_hitch_m=std::clamp(
            stage1a::dot(rear_to_fifth_wheel,
                         stage1a::forward(truck_front_heading)),-2.0,3.0);
        request.geometry.tractor_reference_to_hitch_lateral_m=std::clamp(
            stage1a::dot(rear_to_fifth_wheel,
                         stage1a::right(truck_front_heading)),-0.75,0.75);
    }

    stage0::Vec2 live_hitch = stage1a::add(
        stage1a::add(tractor_rear_axle,
            stage1a::multiply(stage1a::forward(truck_front_heading),
                              request.geometry.tractor_reference_to_hitch_m)),
        stage1a::multiply(stage1a::right(truck_front_heading),
            request.geometry.tractor_reference_to_hitch_lateral_m));
    stage0::Vec2 live_trailer_axle{};
    double trailer_front_heading = truck_front_heading;
    double live_articulation = 0.0;
    stage1a::PathSample live_parking_pose{};
    bool task_frame_overlap = false;
    bool target_available = true;
    bool target_out_of_local_range = false;
    {
        if (seed.mode != stage1a::PlannerMode::trailer_to_parking_target)
            target_available = false;
        if (!telemetry.trailer_connected || !telemetry.trailer_valid)
        {
            target_available = false;
        }
        const double trailer_actor_heading =
            telemetry.trailer.orientation.heading * 2.0 * reverse_assist::pi;
        trailer_front_heading =
            stage1a::physical_front_heading_from_actor_rear(
                trailer_actor_heading);
        live_articulation = stage0::wrap_angle(
            truck_front_heading - trailer_front_heading);
        const double trailer_hook_z = trailer_configuration.hook_valid
            ? static_cast<double>(trailer_configuration.hook.z) : 0.0;
        if (trailer_axle_identity_wheel_count!=
                trailer_configuration.wheel_count||
            !std::isfinite(trailer_axle_identity_hook_z)||
            std::abs(trailer_axle_identity_hook_z-trailer_hook_z)>0.01)
        {
            trailer_axle_filter.reset();
            trailer_axle_identity_wheel_count=
                trailer_configuration.wheel_count;
            trailer_axle_identity_hook_z=trailer_hook_z;
        }
        const auto axle_candidate=stable_trailer_axle_candidate(
            trailer_configuration,trailer_wheel_telemetry,
            reverse_assist::normalize_angle(
                trailer_front_heading - truck_front_heading),
            trailer_hook_z);
        const auto previous_grounded_mask=trailer_axle_filter.valid()
            ? trailer_axle_filter.stable().grounded_mask : 0;
        trailer_axle_filter.update(axle_candidate,now);
        if (!trailer_axle_filter.valid()) target_available=false;
        const auto trailer_spec=trailer_axle_filter.valid()
            ? trailer_axle_filter.stable() : axle_candidate;
        if (previous_grounded_mask&&trailer_axle_filter.valid()&&
            previous_grounded_mask!=trailer_spec.grounded_mask)
            log_line("[reverse-planner] Trailer support topology committed "
                     "atomically after 0.4 s stability.");
        const double trailer_hook_x = trailer_configuration.hook_valid
            ? static_cast<double>(trailer_configuration.hook.x) : 0.0;
        const stage0::Vec2 trailer_origin{
            telemetry.trailer.position.x, telemetry.trailer.position.z};
        live_hitch = stage1a::actor_local_to_world(
            trailer_origin, trailer_actor_heading,
            trailer_hook_x, trailer_hook_z);
        live_trailer_axle = stage1a::subtract(
            live_hitch,
            stage1a::multiply(stage1a::forward(trailer_front_heading),
                              trailer_spec.axle_to_hitch_m));
        request.subject_start = {live_trailer_axle, trailer_front_heading};
        request.target = seed.target;
        request.geometry.trailer_axle_to_hitch_m =
            trailer_spec.axle_to_hitch_m;
        request.geometry.trailer_axle_steering_rad =
            trailer_spec.axle_steering_rad;
        request.geometry.trailer_rear_overhang_m =
            trailer_spec.rear_overhang_m;
        request.geometry.trailer_length_m =
            trailer_spec.axle_to_hitch_m + trailer_spec.rear_overhang_m;
        request.geometry.trailer_width_m = trailer_spec.width_m;
        const auto tractor_to_hitch =
            stage1a::subtract(live_hitch, tractor_rear_axle);
        request.geometry.tractor_reference_to_hitch_m = std::clamp(
            stage1a::dot(tractor_to_hitch,
                         stage1a::forward(truck_front_heading)), -2.0, 3.0);
        request.geometry.tractor_reference_to_hitch_lateral_m = std::clamp(
            stage1a::dot(stage1a::subtract(live_hitch, tractor_rear_axle),
                         stage1a::right(truck_front_heading)), -0.75, 0.75);

        live_parking_pose.trailer_position = live_trailer_axle;
        live_parking_pose.trailer_heading = trailer_front_heading;
        if (target_available && stage1a::length(stage1a::subtract(
                seed.target.position, live_trailer_axle)) >
                stage1a::maximum_planning_chord_m)
        {
            // The native telemetry scan is deliberately local. A farther
            // fixed seed survived a scene/job transition and is not evidence
            // that a task guide is currently present.
            target_available = false;
            target_out_of_local_range = true;
        }
        if (target_available)
        {
            task_frame_overlap = stage1a::parking_guidance_phase(
                live_parking_pose, seed.target, request.geometry,
                0.01, 0.20, 15.0 * stage0::pi / 180.0);
            // The game accepts a parking region, not one mathematically exact
            // axle pose. Once roughly half the trailer is inside that region,
            // further centre-seeking creates short reverse-angle corrections.
            if (stage1a::parking_target_reached(
                    live_parking_pose, seed.target, request.geometry,
                    0.48, 0.80, 6.0 * stage0::pi / 180.0))
            {
                clear_frames();
                failure_started_tick = 0;
                return;
            }
            // The region planner starts at the exact entry pose, then expands
            // through small bounded longitudinal/lateral/heading checkpoints.
            request.target = stage1a::parking_entry_pose(
                live_parking_pose, seed.target, request.geometry,
                0.0, 0.0, 0.0);
        }
    }

    const auto append_segment = [&](PredictionFrameSet &output,
                                    stage0::Vec2 from, stage0::Vec2 to,
                                    reverse_assist::BodyKind kind) {
        const double dx = to.x - from.x;
        const double dz = to.y - from.y;
        const double segment_length = std::hypot(dx, dz);
        if (segment_length < 0.05) return;
        const int beads = std::max(2, static_cast<int>(
            std::ceil(segment_length / 0.20)) + 1);
        const double heading = std::atan2(dx, dz);
        for (int bead = 0; bead < beads &&
             output.count < output.frames.size(); ++bead)
        {
            const double t = static_cast<double>(bead) /
                             static_cast<double>(beads - 1);
            auto &frame = output.frames[output.count++];
            frame.kind = kind;
            frame.transform = make_transform(
                from.x + dx * t, telemetry.truck.position.y + 0.33,
                from.y + dz * t, heading, 0.0);
        }
    };
    const auto append_arrow = [&](PredictionFrameSet &output,
                                  stage0::Vec2 center,double heading) {
        const auto axis = stage1a::forward(heading);
        const auto lateral = stage1a::right(heading);
        const auto tip = stage1a::add(center,
            stage1a::multiply(axis, 0.55));
        const auto tail = stage1a::subtract(center,
            stage1a::multiply(axis, 0.35));
        const auto head_base = stage1a::subtract(tip,
            stage1a::multiply(axis, 0.42));
        const auto wing_a=stage1a::add(head_base,
            stage1a::multiply(lateral, 0.30));
        const auto wing_b=stage1a::subtract(head_base,
            stage1a::multiply(lateral, 0.30));
        // The prism line mesh spans local Z [-0.18,+0.18] around every
        // transform. Put each terminal mesh centre half a mesh-length away
        // from the mathematical apex so the three visible ends meet there
        // instead of crossing through it into a star shape.
        constexpr double marker_half_length_m=0.18;
        const auto inset_from_tip=[&](stage0::Vec2 endpoint) {
            const auto delta=stage1a::subtract(endpoint,tip);
            const double distance=stage1a::length(delta);
            return stage1a::add(tip,stage1a::multiply(
                delta,marker_half_length_m/std::max(1e-6,distance)));
        };
        append_segment(output, tail,
            stage1a::subtract(tip,stage1a::multiply(
                axis,marker_half_length_m)),
            reverse_assist::BodyKind::tractor);
        append_segment(output, inset_from_tip(wing_a), wing_a,
            reverse_assist::BodyKind::tractor);
        append_segment(output, inset_from_tip(wing_b), wing_b,
            reverse_assist::BodyKind::tractor);
    };
    const auto cab_side_centers = [&] {
        const auto forward = stage1a::forward(truck_front_heading);
        const auto lateral = stage1a::right(truck_front_heading);
        const double forward_offset = std::clamp(
            request.geometry.tractor_center_from_reference_m +
                request.geometry.tractor_length_m * 0.25,
            2.8, 4.8);
        const auto cab_center = stage1a::add(tractor_rear_axle,
            stage1a::multiply(forward, forward_offset));
        const double side_offset =
            request.geometry.tractor_width_m * 0.5 + 0.55;
        return std::array<stage0::Vec2, 2>{
            stage1a::subtract(cab_center,
                stage1a::multiply(lateral, side_offset)),
            stage1a::add(cab_center,
                stage1a::multiply(lateral, side_offset))};
    };
    const auto append_cab_arrows = [&](PredictionFrameSet &arrows,
                                       double correction_rad) {
        const double heading = stage1a::steering_arrow_heading_rad(
            truck_front_heading, correction_rad);
        for (const auto &center : cab_side_centers())
            append_arrow(arrows, center, heading);
    };
    const auto publish_cab_arrows = [&](double correction_rad) {
        PredictionFrameSet arrows{};
        append_cab_arrows(arrows, correction_rad);
        std::lock_guard lock(prediction_frames_mutex);
        prediction_frames = arrows;
        arrow_frame_start = 0;
        arrow_frame_count = arrows.count;
        arrow_suffix_valid = true;
    };
    const auto publish_unreachable_x = [&] {
        PredictionFrameSet warning{};
        for (const auto &center : cab_side_centers())
        {
            const auto diagonal_a = stage1a::forward(
                truck_front_heading + reverse_assist::pi / 4.0);
            const auto diagonal_b = stage1a::forward(
                truck_front_heading - reverse_assist::pi / 4.0);
            append_segment(warning,
                stage1a::subtract(center,stage1a::multiply(diagonal_a,0.55)),
                stage1a::add(center,stage1a::multiply(diagonal_a,0.55)),
                reverse_assist::BodyKind::trailer);
            append_segment(warning,
                stage1a::subtract(center,stage1a::multiply(diagonal_b,0.55)),
                stage1a::add(center,stage1a::multiply(diagonal_b,0.55)),
                reverse_assist::BodyKind::trailer);
        }
        std::lock_guard lock(prediction_frames_mutex);
        prediction_frames = warning;
        arrow_suffix_valid = false;
    };

    if (!target_available)
    {
        if (target_out_of_local_range)
        {
            clear_frames();
            failure_started_tick=0;
            failed_pose_valid=false;
            successful_pose_valid=false;
            // Remove the whole captured-state bundle. Merely rejecting it in
            // memory would allow the obsolete absolute world coordinate to
            // resurrect after the next game restart.
            std::error_code remove_path_error;
            std::error_code remove_diagnostics_error;
            std::error_code remove_obstacles_error;
            const bool removed_path=std::filesystem::remove(
                path_file,remove_path_error);
            std::filesystem::remove(
                diagnostics_file,remove_diagnostics_error);
            std::filesystem::remove(
                obstacles_file,remove_obstacles_error);
            seed.valid=false;
            logged_seed_failure=true;
            if (entering_reverse || now-last_failure_log_tick>=2000)
            {
                last_failure_log_tick=now;
                if (removed_path && !remove_path_error &&
                    !remove_diagnostics_error && !remove_obstacles_error)
                    log_line("[reverse-planner] Stale task target deleted: "
                             "outside the 50 m local telemetry radius; route, "
                             "warning markers, diagnostics and obstacle "
                             "snapshot cleared.",SCS_LOG_TYPE_warning);
                else
                    log_line("[reverse-planner] Stale task target disabled but "
                             "one or more state files could not be deleted; "
                             "no planning will run until a new verified scan.",
                             SCS_LOG_TYPE_error);
            }
            return;
        }
        // Once a trailer is attached, an unavailable task-frame contract is
        // actionable: after the same one-second persistence gate, show X
        // instead of leaving the driver with a silently blank display.
        if (request.mode==stage1a::PlannerMode::trailer_to_parking_target)
        {
            if (failure_started_tick==0) failure_started_tick=now;
            if (now - failure_started_tick >= 1000)
                publish_unreachable_x();
            else clear_frames();
        }
        else
        {
            clear_frames();
            failure_started_tick=0;
        }
        if (entering_reverse || now - last_failure_log_tick >= 2000)
        {
            last_failure_log_tick = now;
            log_line("[reverse-planner] Dynamic target unavailable: live "
                     "mode changed, but no unique native guide-verified "
                     "target of that mode is available.",
                     SCS_LOG_TYPE_warning);
        }
        return;
    }

    const auto current_steering_rad = [&] {
        return std::clamp(static_cast<double>(telemetry.steering),-1.0,1.0)*
               request.geometry.tractor_max_steer_rad;
    };
    const auto refresh_arrow_suffix = [&] {
        if (!arrow_suffix_valid) return;
        if (required_steering_valid)
            steering_correction = stage0::wrap_angle(
                required_steering_rad-current_steering_rad());
        PredictionFrameSet arrows{};
        append_cab_arrows(arrows,steering_correction);
        std::lock_guard lock(prediction_frames_mutex);
        if (arrows.count!=arrow_frame_count ||
            arrow_frame_start+arrow_frame_count!=prediction_frames.count)
            return;
        for (std::size_t index=0;index<arrows.count;++index)
            prediction_frames.frames[arrow_frame_start+index]=
                arrows.frames[index];
    };
    const bool replan_due = entering_reverse ||
                            now-last_replan_tick>=200;
    if (!replan_due)
    {
        // Arrow transforms follow the cab at telemetry frame rate while the
        // expensive full route remains on its bounded 200 ms cadence.
        refresh_arrow_suffix();
        return;
    }
    last_replan_tick=now;

    const auto pose_near=[](const stage0::Pose2 &a,const stage0::Pose2 &b) {
        return stage1a::length(stage1a::subtract(a.position,b.position))<0.10&&
               std::abs(stage0::wrap_angle(
                   a.heading_rad-b.heading_rad))<0.50*stage0::pi/180.0;
    };
    const bool repeated_successful_pose = successful_pose_valid &&
        !seed_changed &&
        pose_near(request.tractor_start,successful_tractor_pose) &&
        pose_near(request.subject_start,successful_trailer_pose) &&
        pose_near(request.target,successful_target_pose);
    if (repeated_successful_pose)
    {
        // The full route is steering-input independent. Preserve it while the
        // vehicle pose is effectively unchanged and update only cab arrows.
        refresh_arrow_suffix();
        return;
    }
    const bool repeated_failed_pose = failed_pose_valid && !seed_changed &&
        pose_near(request.tractor_start,failed_tractor_pose) &&
        pose_near(request.subject_start,failed_trailer_pose) &&
        pose_near(request.target,failed_target_pose) &&
        std::abs(request.geometry.trailer_axle_to_hitch_m-
                 failed_axle_to_hitch)<0.01 &&
        std::abs(request.geometry.trailer_axle_steering_rad-
                 failed_axle_steering)<0.001;
    if (repeated_failed_pose)
    {
        // A stationary identical pose cannot produce a different search
        // result. Keep the one-second UI timer alive without repeating the
        // worst-case zero-solution enumeration.
        if (failed_pose_was_stable)
        {
            failure_started_tick=0;
            publish_cab_arrows(steering_correction);
        }
        else if (failure_started_tick&&now-failure_started_tick>=1000)
            publish_unreachable_x();
        else
            clear_frames();
        return;
    }

    auto plan = stage1a::plan_parking_terminal_region(
        request, live_parking_pose, seed.target);
    if (!plan.found || plan.samples.size() < 2)
    {
        const bool stable_terminal_approach =
            (task_frame_overlap || stage1a::parking_entry_corridor(
                live_parking_pose, seed.target, request.geometry)) &&
            std::abs(live_articulation) <= 25.0 * stage0::pi / 180.0;
        failed_pose_valid=true;
        failed_pose_was_stable=stable_terminal_approach;
        failed_tractor_pose=request.tractor_start;
        failed_trailer_pose=request.subject_start;
        failed_target_pose=request.target;
        failed_axle_to_hitch=request.geometry.trailer_axle_to_hitch_m;
        failed_axle_steering=request.geometry.trailer_axle_steering_rad;
        if (stable_terminal_approach)
        {
            failure_started_tick = 0;
            publish_cab_arrows(steering_correction);
        }
        else
        {
            if (failure_started_tick == 0) failure_started_tick = now;
            if (now - failure_started_tick >= 1000)
                publish_unreachable_x();
            else
                clear_frames();
        }
        if (entering_reverse || now - last_failure_log_tick >= 2000)
        {
            last_failure_log_tick = now;
            std::ostringstream message;
            message << "[reverse-planner] Dynamic replan failed: reason="
                    << plan.reason << " chord="
                    << std::fixed << std::setprecision(2)
                    << stage1a::length(stage1a::subtract(
                           request.target.position,
                           request.subject_start.position))
                    << "m start=(" << request.subject_start.position.x << ','
                    << request.subject_start.position.y << ") target=("
                    << request.target.position.x << ','
                    << request.target.position.y << ") reject[start="
                    << plan.start_state_rejections << ",curve="
                    << plan.curvature_rejections << ",articulation="
                    << plan.articulation_rejections << ",obstacle="
                    << plan.obstacle_collision_rejections << ",self="
                    << plan.self_collision_rejections << ",forward="
                    << plan.forward_motion_rejections << ",other="
                    << plan.other_rejections << "].";
            log_line(message.str(), SCS_LOG_TYPE_warning);
        }
        return;
    }
    failed_pose_valid=false;
    failure_started_tick = 0;
    successful_pose_valid=true;
    successful_tractor_pose=request.tractor_start;
    successful_trailer_pose=request.subject_start;
    successful_target_pose=request.target;

    steering_correction = stage1a::steering_correction_rad(
        plan, request.geometry, telemetry.steering);
    required_steering_rad=stage0::wrap_angle(
        steering_correction+current_steering_rad());
    required_steering_valid=true;
    if (task_frame_overlap)
    {
        publish_cab_arrows(steering_correction);
        return;
    }

    std::vector<stage1a::RearTrackPair> tracks;
    tracks.reserve(plan.samples.size());
    for (const auto &sample : plan.samples)
        tracks.push_back(stage1a::tractor_rear_tracks(
            sample, request.geometry, 0.18));
    if (tracks.size() < 2)
    {
        clear_frames();
        return;
    }

    PredictionFrameSet frames{};
    const std::size_t maximum_segments_per_side =
        (frames.frames.size() - 28) / 2;
    const std::size_t stride = std::max<std::size_t>(
        1, (tracks.size() - 1 + maximum_segments_per_side - 1) /
               maximum_segments_per_side);
    const auto publish_side = [&](bool left) {
        for (std::size_t from_index = 0;
             from_index + 1 < tracks.size();) {
            const std::size_t to_index = std::min(
                tracks.size() - 1, from_index + stride);
            const auto &from = left ? tracks[from_index].left
                                    : tracks[from_index].right;
            const auto &to = left ? tracks[to_index].left
                                  : tracks[to_index].right;
            const double dx = to.x - from.x;
            const double dz = to.y - from.y;
            const double route_distance = plan.samples[from_index].distance;
            const bool solid = route_distance <= 7.0;
            const bool dashed = !solid &&
                std::fmod(route_distance - 7.0, 1.20) < 0.65;
            if ((solid || dashed) && std::hypot(dx, dz) >= 0.05 &&
                frames.count < frames.frames.size())
            {
                auto &frame = frames.frames[frames.count++];
                frame.kind = reverse_assist::BodyKind::tractor;
                frame.transform = make_transform(
                    (from.x + to.x) * 0.5,
                    telemetry.truck.position.y + 0.33,
                    (from.y + to.y) * 0.5,
                    std::atan2(dx, dz), 0.0);
            }
            from_index = to_index;
        }
    };
    publish_side(true);
    publish_side(false);
    arrow_frame_start=frames.count;
    append_cab_arrows(frames, steering_correction);
    arrow_frame_count=frames.count-arrow_frame_start;
    arrow_suffix_valid=arrow_frame_count>0;
    if (frames.count == 0)
    {
        clear_frames();
        return;
    }
    {
        std::lock_guard lock(prediction_frames_mutex);
        prediction_frames = frames;
    }
    if (entering_reverse)
        log_line("[reverse-planner] Continuous live full-route replan active; "
                 "tractor geometry comes from wheel/hook telemetry.");
}

void update_entities()
'@

$pattern = '(?s)void update_prediction_frame_targets\(\)\s*\{.*?\}\s*\r?\n\s*void update_entities\(\)'
$generated = [Text.RegularExpressions.Regex]::Replace($source, $pattern, $replacement, 1)
if ($generated -eq $source -or
    $generated -notmatch 'Continuous live full-route replan active') {
    throw 'Failed to replace the legacy prediction producer.'
}

$null = New-Item -ItemType Directory -Path $outputDirectory -Force
[IO.File]::WriteAllText($outputPath, $generated, [Text.UTF8Encoding]::new($false))
Write-Output $outputPath
