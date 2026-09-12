#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <objidl.h>
#include <gdiplus.h>

#include "stage0_model.hpp"
#include "stage1a_planner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace
{
struct Handle
{
    HANDLE value{nullptr};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

template <class T>
bool read_remote(HANDLE process, std::uintptr_t address, T &value)
{
    SIZE_T read{};
    return ReadProcessMemory(process, reinterpret_cast<void *>(address), &value,
                             sizeof(value), &read) && read == sizeof(value);
}

bool read_remote_bytes(HANDLE process, std::uintptr_t address, void *data, std::size_t size)
{
    SIZE_T read{};
    return ReadProcessMemory(process, reinterpret_cast<void *>(address), data, size,
                             &read) && read == size;
}

std::string read_remote_ascii(HANDLE process, std::uintptr_t address,
                              std::size_t maximum = 384)
{
    if (address < 0x10000000000ULL || address > 0x7FFFFFFFFFFFULL) return {};
    std::vector<char> bytes(maximum, '\0');
    SIZE_T read{};
    if (!ReadProcessMemory(process, reinterpret_cast<void *>(address),
                           bytes.data(), bytes.size() - 1, &read) || read == 0)
        return {};
    bytes[std::min(read, bytes.size() - 1)] = '\0';
    const auto length = strnlen_s(bytes.data(), bytes.size());
    if (length == 0 || length >= bytes.size()) return {};
    for (std::size_t index = 0; index < length; ++index)
        if (static_cast<unsigned char>(bytes[index]) < 0x20 ||
            static_cast<unsigned char>(bytes[index]) > 0x7e)
            return {};
    return std::string(bytes.data(), length);
}

std::string try_read_model_resource_path(HANDLE process,
                                         std::uintptr_t resource)
{
    std::uintptr_t descriptor{}, path_pointer{};
    if (!resource || !read_remote(process, resource + 0x10, descriptor) ||
        !descriptor || !read_remote(process, descriptor + 0x28, path_pointer))
        return {};
    auto path = read_remote_ascii(process, path_pointer);
    return path.rfind("/model/", 0) == 0 ? path : std::string{};
}

std::vector<std::pair<std::uintptr_t, std::string>>
find_model_resource_paths(HANDLE process, std::uintptr_t model)
{
    std::vector<std::pair<std::uintptr_t, std::string>> paths;
    std::unordered_set<std::uintptr_t> checked;
    const auto inspect = [&](std::uintptr_t candidate) {
        if (candidate < 0x10000000000ULL || candidate > 0x7FFFFFFFFFFFULL ||
            !checked.insert(candidate).second)
            return;
        auto path = try_read_model_resource_path(process, candidate);
        if (!path.empty()) paths.emplace_back(candidate, std::move(path));
    };

    inspect(model);
    std::array<std::uintptr_t, 0x400 / sizeof(std::uintptr_t)> first{};
    if (!read_remote_bytes(process, model, first.data(), sizeof(first))) return paths;
    for (const auto pointer : first)
    {
        inspect(pointer);
        std::array<std::uintptr_t, 0x100 / sizeof(std::uintptr_t)> second{};
        if (pointer >= 0x10000000000ULL && pointer <= 0x7FFFFFFFFFFFULL &&
            read_remote_bytes(process, pointer, second.data(), sizeof(second)))
            for (const auto nested : second) inspect(nested);
    }
    return paths;
}

std::optional<DWORD> find_game_process()
{
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot.value, &entry)) return std::nullopt;
    do
    {
        if (_wcsicmp(entry.szExeFile, L"eurotrucks2.exe") == 0)
            return entry.th32ProcessID;
    } while (Process32NextW(snapshot.value, &entry));
    return std::nullopt;
}

struct ModuleInfo { std::uintptr_t base{}; std::size_t size{}; };

std::optional<ModuleInfo> find_game_module(DWORD pid)
{
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
    if (snapshot.value == INVALID_HANDLE_VALUE) return std::nullopt;
    MODULEENTRY32W entry{sizeof(entry)};
    if (!Module32FirstW(snapshot.value, &entry)) return std::nullopt;
    do
    {
        if (_wcsicmp(entry.szModule, L"eurotrucks2.exe") == 0)
            return ModuleInfo{reinterpret_cast<std::uintptr_t>(entry.modBaseAddr),
                              static_cast<std::size_t>(entry.modBaseSize)};
    } while (Module32NextW(snapshot.value, &entry));
    return std::nullopt;
}

struct SectionInfo { std::uintptr_t address{}; std::size_t size{}; };

std::optional<SectionInfo> find_text_section(HANDLE process, const ModuleInfo &module)
{
    IMAGE_DOS_HEADER dos{};
    if (!read_remote(process, module.base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE)
        return std::nullopt;
    IMAGE_NT_HEADERS64 nt{};
    if (!read_remote(process, module.base + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE) return std::nullopt;
    const auto sections = module.base + dos.e_lfanew + sizeof(DWORD) +
        sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i)
    {
        IMAGE_SECTION_HEADER section{};
        if (!read_remote(process, sections + i * sizeof(section), section))
            return std::nullopt;
        const std::string name(reinterpret_cast<char *>(section.Name),
                               reinterpret_cast<char *>(section.Name) + 5);
        if (name.rfind(".text", 0) == 0)
            return SectionInfo{module.base + section.VirtualAddress,
                               std::max(section.Misc.VirtualSize, section.SizeOfRawData)};
    }
    return std::nullopt;
}

// ETS2LA/plugin c925bd9, Windows 1.60 game_traffic signature (MIT).
constexpr std::array<int, 23> game_traffic_pattern{
    0x48,0x8b,0xd9,0x48,0x8b,0x0d,-1,-1,-1,-1,0x48,0x85,0xc9,0x74,-1,
    0x48,0x8b,0x83,-1,-1,-1,-1,0x48};

std::vector<std::uintptr_t> scan_game_traffic(HANDLE process, const SectionInfo &text)
{
    std::vector<std::uint8_t> bytes(text.size);
    if (!read_remote_bytes(process, text.address, bytes.data(), bytes.size())) return {};
    std::vector<std::uintptr_t> matches;
    for (std::size_t i = 0; i + game_traffic_pattern.size() <= bytes.size(); ++i)
    {
        bool match = true;
        for (std::size_t j = 0; j < game_traffic_pattern.size(); ++j)
            if (game_traffic_pattern[j] >= 0 &&
                bytes[i + j] != game_traffic_pattern[j]) { match = false; break; }
        if (match) matches.push_back(text.address + i);
    }
    return matches;
}

#pragma pack(push, 1)
struct Float3 { float x{}, y{}, z{}; };
struct Quat { float w{}, x{}, y{}, z{}; };
struct Placement { Float3 pos{}; std::int16_t cx{}, cz{}; Quat rot{}; };
struct AaBox { Float3 start{}, end{}; };
struct ArrayHeader
{
    std::uintptr_t vtable{};
    std::uintptr_t value{};
    std::uint64_t size{};
    std::uint64_t capacity{};
    std::uintptr_t allocator{};
};
struct PrismTransform
{
    float x{},y{},z{};
    std::int16_t sector_x{},sector_z{};
    Quat rot{};
};
#pragma pack(pop)
static_assert(sizeof(Placement) == 0x20);
static_assert(sizeof(AaBox) == 0x18);
static_assert(sizeof(ArrayHeader) == 0x28);
static_assert(sizeof(PrismTransform) == 0x20);

struct TransformHit
{
    std::uintptr_t address{};
    PrismTransform transform{};
    double world_x{},world_z{},heading{},position_error{},heading_error{};
};

struct ModelNode
{
    std::uintptr_t model{}, parent{}, vtable{};
    bool common_model_inst{};
    double x{},y{},z{},heading{};
};

Float3 rotate(Float3 value, const Quat &q)
{
    const float x2=q.x+q.x, y2=q.y+q.y, z2=q.z+q.z;
    return {
        value.x*(1-y2*q.y-z2*q.z)+value.y*(x2*q.y-z2*q.w)+value.z*(x2*q.z+y2*q.w),
        value.x*(x2*q.y+z2*q.w)+value.y*(1-x2*q.x-z2*q.z)+value.z*(y2*q.z-x2*q.w),
        value.x*(x2*q.z-y2*q.w)+value.y*(y2*q.z+x2*q.w)+value.z*(1-x2*q.x-y2*q.y)};
}

struct Telemetry
{
    bool available{};
    double x{}, y{}, z{}, heading{};
    float speed{};
    int engine_gear{};
    int gear{};
    int attached_count{};
    std::vector<stage0::Vec2> attached_trailer_positions{};
    std::vector<stage0::Pose2> attached_trailer_hook_poses{};
    double tractor_wheelbase_m{3.8};
    double tractor_width_m{2.5};
    double tractor_length_m{6.8};
    std::vector<double> trailer_axle_to_hitch_m{};
};

Telemetry read_telemetry()
{
    Telemetry t{};
    Handle mapping{OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SCSTelemetry")};
    if (!mapping.value) return t;
    const auto view = static_cast<const std::uint8_t *>(
        MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 32768));
    if (!view) return t;
    t.available = true;
    // scsTelemetryMap_t integer zone starts at 500. Offset 504 is
    // truck.engine.gear; v0.10.8/v0.10.9 intentionally use the next s32,
    // truck.displayed.gear, at 508. Automatic transmissions can report
    // engine gear 0 while stationary in reverse, so activation must use the
    // displayed value just like the two known-good plug-in versions.
    std::memcpy(&t.engine_gear, view + 504, sizeof(t.engine_gear));
    std::memcpy(&t.gear, view + 508, sizeof(t.gear));
    std::memcpy(&t.speed, view + 948, sizeof(t.speed));
    std::memcpy(&t.x, view + 2200, sizeof(t.x));
    std::memcpy(&t.y, view + 2208, sizeof(t.y));
    std::memcpy(&t.z, view + 2216, sizeof(t.z));
    double heading_turns{};
    std::memcpy(&heading_turns, view + 2224, sizeof(heading_turns));
    // SCS telemetry heading follows the actor +Z axis, which points toward the
    // vehicle rear in the observed 1.60 build. Store the physical front heading.
    t.heading = stage0::wrap_angle(heading_turns * 2.0 * stage0::pi + stage0::pi);

    // Reuse the wheel/hook geometry extraction proven by Reverse Posture
    // Assistant v0.10.8/v0.10.9. These are SCS telemetry values, not steering
    // input to this fixed-path planner.
    int truck_wheels{};
    std::memcpy(&truck_wheels,view+80,sizeof(truck_wheels));
    truck_wheels=std::clamp(truck_wheels,0,16);
    double front_z=0.0,rear_z=0.0,min_x=1e9,max_x=-1e9;
    int front_count=0,rear_count=0;
    for (int wheel=0;wheel<truck_wheels;++wheel)
    {
        float x{},z{};
        std::uint8_t steerable{};
        std::memcpy(&steerable,view+1500+wheel,sizeof(steerable));
        std::memcpy(&x,view+1740+wheel*4,sizeof(x));
        std::memcpy(&z,view+1804+wheel*4,sizeof(z));
        min_x=std::min(min_x,static_cast<double>(x));
        max_x=std::max(max_x,static_cast<double>(x));
        if (steerable) { front_z+=z; ++front_count; }
        else { rear_z+=z; ++rear_count; }
    }
    if (front_count&&rear_count)
    {
        front_z/=front_count; rear_z/=rear_count;
        t.tractor_wheelbase_m=std::clamp(std::abs(front_z-rear_z),2.4,6.8);
        t.tractor_length_m=std::clamp(t.tractor_wheelbase_m+2.25,5.2,9.0);
        t.tractor_width_m=std::clamp(max_x-min_x+0.42,2.25,2.65);
    }
    for (int i=0; i<10; ++i)
    {
        const auto base=6000+i*1560;
        if (!*(view+base+80)) continue;
        ++t.attached_count;
        double x{},y{},z{},heading_turns{};
        std::memcpy(&x,view+base+872,sizeof(x));
        std::memcpy(&y,view+base+880,sizeof(y));
        std::memcpy(&z,view+base+888,sizeof(z));
        std::memcpy(&heading_turns,view+base+896,sizeof(heading_turns));
        t.attached_trailer_positions.push_back({x,z});
        int wheels{};
        float hook_x{},hook_z{};
        std::memcpy(&wheels,view+base+148,sizeof(wheels));
        std::memcpy(&hook_x,view+base+664,sizeof(hook_x));
        std::memcpy(&hook_z,view+base+672,sizeof(hook_z));
        const double actor_heading=heading_turns*2.0*stage0::pi;
        t.attached_trailer_hook_poses.push_back({
            stage1a::actor_local_to_world({x,z},actor_heading,hook_x,hook_z),
            stage1a::physical_front_heading_from_actor_rear(actor_heading)});
        wheels=std::clamp(wheels,0,16);
        double wheel_z=0.0;
        for (int wheel=0;wheel<wheels;++wheel)
        {
            float local_z{};
            std::memcpy(&local_z,view+base+804+wheel*4,sizeof(local_z));
            wheel_z+=local_z;
        }
        const double axle_to_hitch=wheels
            ? std::clamp(std::abs(wheel_z/wheels-hook_z),2.5,14.0):8.5;
        t.trailer_axle_to_hitch_m.push_back(axle_to_hitch);
    }
    UnmapViewOfFile(view);
    return t;
}

double heading_from_quaternion(const Quat &q)
{
    const auto actor_rear = rotate({0.0f, 0.0f, 1.0f}, q);
    return stage0::wrap_angle(std::atan2(actor_rear.x, actor_rear.z)+stage0::pi);
}

std::vector<TransformHit> scan_private_transforms_near(
    HANDLE process,double anchor_x,double anchor_y,double anchor_z,
    double anchor_heading,std::size_t maximum_hits=512)
{
    std::vector<TransformHit> hits;
    // ASLR placed earlier captures in the 0x1b... band and the current process
    // in the 0x20... band. Keep a bounded user-heap window, but do not assume
    // that all ETS2 private allocations remain below 0x20000000000.
    constexpr std::uintptr_t minimum=0x10000000000ULL;
    constexpr std::uintptr_t maximum=0x40000000000ULL;
    constexpr std::size_t chunk_size=4*1024*1024;
    std::vector<std::uint8_t> bytes(chunk_size+sizeof(PrismTransform));
    auto address=minimum;
    while (address<maximum&&hits.size()<maximum_hits)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(process,reinterpret_cast<void *>(address),&info,sizeof(info))) break;
        const auto base=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto region_size=static_cast<std::size_t>(info.RegionSize);
        const bool readable=info.State==MEM_COMMIT&&info.Type==MEM_PRIVATE&&
            !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD));
        if (readable)
        {
            std::size_t region_offset=0;
            while (region_offset<region_size&&hits.size()<maximum_hits)
            {
                const auto request=std::min(chunk_size,region_size-region_offset);
                SIZE_T count{};
                if (ReadProcessMemory(process,reinterpret_cast<void *>(base+region_offset),
                    bytes.data(),request,&count)&&count>=sizeof(PrismTransform))
                {
                    for (std::size_t offset=0;
                         offset+sizeof(PrismTransform)<=count;offset+=4)
                    {
                        PrismTransform transform{};
                        std::memcpy(&transform,bytes.data()+offset,sizeof(transform));
                        const double qnorm=static_cast<double>(transform.rot.w)*transform.rot.w+
                            static_cast<double>(transform.rot.x)*transform.rot.x+
                            static_cast<double>(transform.rot.y)*transform.rot.y+
                            static_cast<double>(transform.rot.z)*transform.rot.z;
                        if (!std::isfinite(qnorm)||qnorm<0.90||qnorm>1.10||
                            !std::isfinite(transform.x)||!std::isfinite(transform.y)||
                            !std::isfinite(transform.z)) continue;
                        const double world_x=transform.x+transform.sector_x*512.0;
                        const double world_z=transform.z+transform.sector_z*512.0;
                        const double position_error=std::hypot(world_x-anchor_x,world_z-anchor_z);
                        if (position_error>2.0||std::abs(transform.y-anchor_y)>5.0) continue;
                        const double heading=heading_from_quaternion(transform.rot);
                        const double raw_error=std::abs(stage0::wrap_angle(heading-anchor_heading));
                        const double heading_error=std::min(raw_error,std::abs(stage0::pi-raw_error));
                        if (heading_error>0.40) continue;
                        hits.push_back({base+region_offset+offset,transform,world_x,world_z,
                            heading,position_error,heading_error});
                        if (hits.size()>=maximum_hits) break;
                    }
                }
                if (!request) break;
                region_offset+=request;
            }
        }
        const auto next=base+region_size;
        if (next<=address) break;
        address=next;
    }
    return hits;
}

std::vector<ModelNode> scan_model_nodes_near(
    HANDLE process,const ModuleInfo &module,const Telemetry &telemetry,
    const std::unordered_set<std::uintptr_t> &additional_vtables)
{
    std::vector<ModelNode> nodes;
    // Verified for ETS2 1.60.1.7 as a common model_inst_u vtable. It includes
    // vehicles and third-party render models, but the native unload frame was
    // not found under this vtable; exact resource identity remains mandatory.
    constexpr std::uintptr_t model_inst_vtable_rva=0x022F98A0;
    const std::uintptr_t wanted_vtable=module.base+model_inst_vtable_rva;
    constexpr std::uintptr_t minimum=0x10000000000ULL;
    constexpr std::uintptr_t maximum=0x40000000000ULL;
    constexpr std::size_t chunk_size=4*1024*1024;
    std::vector<std::uint8_t> bytes(chunk_size+0x60);
    auto address=minimum;
    while (address<maximum)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(process,reinterpret_cast<void *>(address),&info,sizeof(info))) break;
        const auto base=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const auto region_size=static_cast<std::size_t>(info.RegionSize);
        const bool readable=info.State==MEM_COMMIT&&info.Type==MEM_PRIVATE&&
            !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD));
        if (readable)
        {
            for (std::size_t region_offset=0;region_offset<region_size;)
            {
                const auto request=std::min(chunk_size,region_size-region_offset);
                SIZE_T count{};
                if (ReadProcessMemory(process,reinterpret_cast<void *>(base+region_offset),
                    bytes.data(),request,&count)&&count>=0x58)
                {
                    for (std::size_t offset=0;offset+0x58<=count;offset+=8)
                    {
                        std::uintptr_t vtable{},parent{};
                        std::memcpy(&vtable,bytes.data()+offset,sizeof(vtable));
                        const bool common_model_inst=vtable==wanted_vtable;
                        const bool game_vtable=vtable>=module.base &&
                            vtable<module.base+module.size;
                        // Resource-identity pass: accept any game-owned object
                        // with the verified placement layout. Geometry fitting
                        // below remains restricted to common_model_inst.
                        if (!game_vtable && !additional_vtables.contains(vtable))
                            continue;
                        std::memcpy(&parent,bytes.data()+offset+0x50,sizeof(parent));
                        PrismTransform transform{};
                        std::memcpy(&transform,bytes.data()+offset+0x10,sizeof(transform));
                        const double qnorm=static_cast<double>(transform.rot.w)*transform.rot.w+
                            static_cast<double>(transform.rot.x)*transform.rot.x+
                            static_cast<double>(transform.rot.y)*transform.rot.y+
                            static_cast<double>(transform.rot.z)*transform.rot.z;
                        if (!std::isfinite(qnorm)||qnorm<0.90||qnorm>1.10) continue;
                        const double world_x=transform.x+transform.sector_x*512.0;
                        const double world_z=transform.z+transform.sector_z*512.0;
                        if (std::hypot(world_x-telemetry.x,world_z-telemetry.z)>50.0||
                            std::abs(transform.y-telemetry.y)>10.0) continue;
                        nodes.push_back({base+region_offset+offset,parent,vtable,
                            common_model_inst,world_x,transform.y,
                            world_z,heading_from_quaternion(transform.rot)});
                    }
                }
                if (!request) break;
                region_offset+=request;
            }
        }
        const auto next=base+region_size;
        if (next<=address) break;
        address=next;
    }
    return nodes;
}

struct Candidate
{
    std::uintptr_t id{};
    double distance{}, x{}, y{}, z{}, heading{}, width{}, length{}, height{};
    stage0::Vec2 kingpin{};
    double approach{};
    std::wstring label{};
    bool show_coupling{};
    double speed_mps{};
};

std::string narrow_ascii(const std::wstring &value)
{
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character:value)
        result.push_back(character>=0x20&&character<=0x7e
            ?static_cast<char>(character):'?');
    return result;
}

#pragma pack(push, 1)
struct CompleteObjectLocator
{
    std::uint32_t signature{}, offset{}, cd_offset{};
    std::int32_t type_descriptor_rva{}, class_descriptor_rva{}, self_rva{};
};
#pragma pack(pop)

std::string read_rtti_name(HANDLE process, const ModuleInfo &module,
                           std::uintptr_t object)
{
    std::uintptr_t vtable{}, locator_address{};
    CompleteObjectLocator locator{};
    if (!read_remote(process,object,vtable) || !vtable ||
        !read_remote(process,vtable-sizeof(std::uintptr_t),locator_address) ||
        !read_remote(process,locator_address,locator) || locator.signature!=1 ||
        locator.type_descriptor_rva<=0) return {};
    std::array<char,128> name{};
    if (!read_remote_bytes(process,module.base+locator.type_descriptor_rva+16,
                           name.data(),name.size()-1)) return {};
    return std::string(name.data());
}

std::optional<std::uint64_t> read_constant_virtual_type(HANDLE process,
                                                        std::uintptr_t object)
{
    std::uintptr_t vtable{},type_function{};
    std::array<std::uint8_t,12> code{};
    if (!read_remote(process,object,vtable) || !vtable ||
        !read_remote(process,vtable+sizeof(std::uintptr_t),type_function) ||
        !type_function || !read_remote_bytes(process,type_function,code.data(),code.size()))
        return std::nullopt;
    // Common MSVC leaf override: mov eax, imm32; ret.
    if (code[0]==0xB8 && code[5]==0xC3)
    {
        std::uint32_t value{};
        std::memcpy(&value,code.data()+1,sizeof(value));
        return value;
    }
    return std::nullopt;
}

int png_encoder_clsid(CLSID *clsid)
{
    UINT count=0, bytes=0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (!bytes) return -1;
    std::vector<std::uint8_t> storage(bytes);
    auto *encoders=reinterpret_cast<Gdiplus::ImageCodecInfo *>(storage.data());
    Gdiplus::GetImageEncoders(count, bytes, encoders);
    for (UINT i=0; i<count; ++i)
        if (wcscmp(encoders[i].MimeType, L"image/png")==0) { *clsid=encoders[i].Clsid; return 0; }
    return -1;
}

bool render_png(const Telemetry &t, const std::vector<Candidate> &items,
                int selected_target_index,const stage1a::PlanResult &plan)
{
    using namespace Gdiplus;
    GdiplusStartupInput startup_input;
    ULONG_PTR token{};
    if (GdiplusStartup(&token, &startup_input, nullptr) != Ok) return false;
    bool ok=false;
    {
    Bitmap image(1900, 1150, PixelFormat32bppARGB);
    Graphics g(&image);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(255, 20, 25, 32));
    FontFamily family(L"Segoe UI");
    Font title(&family, 24, FontStyleBold, UnitPixel);
    Font body(&family, 17, FontStyleRegular, UnitPixel);
    SolidBrush white(Color(255,235,240,245)), muted(Color(255,165,178,190));
    SolidBrush active_green(Color(255,90,230,140));
    SolidBrush failed_brush(Color(255,255,85,95));
    SolidBrush yellow_fill(Color(255,255,184,60)), cyan_fill(Color(255,55,210,235));
    SolidBrush magenta_fill(Color(255,220,90,240));
    Pen grid(Color(255,55,68,79),1), radius_pen(Color(255,64,150,255),3);
    Pen target_pen(Color(255,255,184,60),4), heading_pen(Color(255,80,220,130),4);
    Pen approach_pen(Color(255,255,80,110),4), truck_pen(Color(255,225,235,245),4);
    Pen attached_pen(Color(255,55,210,235),5), parked_pen(Color(255,220,90,240),4);
    Pen guide_pen(Color(255,120,255,120),4);
    Pen parking_probe_pen(Color(255,255,145,45),5);
    Pen tractor_path_pen(Color(255,60,210,245),5);
    Pen trailer_path_pen(Color(255,255,190,55),6);
    parking_probe_pen.SetDashStyle(DashStyleDash);
    g.DrawString(L"Reverse Planner Stage 1A — stopped-state fixed reverse path", -1, &title,
                 PointF(42,28), &white);
    constexpr float cx=430, cy=470, scale=6.6f;
    for (int m=-50; m<=50; m+=10)
    {
        g.DrawLine(&grid,cx-330,cy-m*scale,cx+330,cy-m*scale);
        g.DrawLine(&grid,cx+m*scale,cy-330,cx+m*scale,cy+330);
    }
    g.DrawEllipse(&radius_pen,cx-330.0f,cy-330.0f,660.0f,660.0f);
    constexpr float tractor_width_m=2.5f, tractor_length_m=7.0f;
    const float tractor_half_width=tractor_width_m*scale*0.5f;
    const float tractor_half_length=tractor_length_m*scale*0.5f;
    g.DrawLine(&truck_pen,cx,cy,cx,cy-tractor_half_length-13.0f);
    g.DrawRectangle(&truck_pen,cx-tractor_half_width,cy-tractor_half_length,
                    tractor_width_m*scale,tractor_length_m*scale);
    g.DrawString(L"PLAYER TRACTOR",-1,&body,PointF(cx-70,cy+tractor_half_length+9),&white);
    g.DrawString(L"FRONT",-1,&body,PointF(cx+8,cy-tractor_half_length-29),&white);

    const stage0::Pose2 frame{{t.x,t.z},t.heading};
    for (const auto &item:items)
    {
        const auto local=stage0::world_to_local({item.x,item.z},frame);
        const double h=stage0::world_heading_to_local(item.heading,frame);
        const double fx=std::sin(h), fy=std::cos(h), sx=std::cos(h), sy=-std::sin(h);
        const double hl=item.length*0.5, hw=item.width*0.5;
        PointF p[4];
        const std::array<std::pair<double,double>,4> signs{{{1,1},{1,-1},{-1,-1},{-1,1}}};
        for (int k=0;k<4;++k)
        {
            const double lx=local.x+fx*hl*signs[k].first+sx*hw*signs[k].second;
            const double ly=local.y+fy*hl*signs[k].first+sy*hw*signs[k].second;
            p[k]=PointF(cx+static_cast<float>(lx*scale),cy-static_cast<float>(ly*scale));
        }
        const bool attached=!item.label.empty() && item.label[0]==L'A';
        const bool native_guide=!item.label.empty() && item.label[0]==L'G';
        const bool parking_probe=!item.label.empty() && item.label[0]==L'K';
        const bool traffic=!item.label.empty() &&
            (item.label[0]==L'V' || item.label[0]==L'P');
        if (native_guide)
        {
            const PointF center(cx+static_cast<float>(local.x*scale),
                                cy-static_cast<float>(local.y*scale));
            const float marker_radius=11.0f;
            g.DrawEllipse(&guide_pen,center.X-marker_radius,center.Y-marker_radius,
                          marker_radius*2,marker_radius*2);
            g.DrawLine(&guide_pen,center.X-marker_radius-5,center.Y,
                       center.X+marker_radius+5,center.Y);
            g.DrawLine(&guide_pen,center.X,center.Y-marker_radius-5,
                       center.X,center.Y+marker_radius+5);
            g.DrawString(item.label.c_str(),-1,&body,PointF(center.X+14,center.Y+10),&white);
            continue;
        }
        g.DrawPolygon(parking_probe?&parking_probe_pen:
            (attached?&attached_pen:(traffic?&parked_pen:&target_pen)),p,4);
        const PointF center(cx+static_cast<float>(local.x*scale),cy-static_cast<float>(local.y*scale));
        const PointF front(center.X+static_cast<float>(fx*hl*scale),
                           center.Y-static_cast<float>(fy*hl*scale));
        g.DrawLine(&heading_pen,center,front);
        const float arrow_length=13.0f, arrow_half_width=9.0f;
        PointF front_arrow[3]{
            PointF(front.X+static_cast<float>(fx*arrow_length),
                   front.Y-static_cast<float>(fy*arrow_length)),
            PointF(front.X-static_cast<float>(fx*7.0f)+static_cast<float>(sx*arrow_half_width),
                   front.Y+static_cast<float>(fy*7.0f)-static_cast<float>(sy*arrow_half_width)),
            PointF(front.X-static_cast<float>(fx*7.0f)-static_cast<float>(sx*arrow_half_width),
                   front.Y+static_cast<float>(fy*7.0f)+static_cast<float>(sy*arrow_half_width))};
        g.FillPolygon(attached?&cyan_fill:(traffic?&magenta_fill:&yellow_fill),front_arrow,3);
        if (item.show_coupling)
        {
            const auto kp=stage0::world_to_local(item.kingpin,frame);
            const PointF kingpin(cx+static_cast<float>(kp.x*scale),cy-static_cast<float>(kp.y*scale));
            g.FillEllipse(&white,kingpin.X-6.0f,kingpin.Y-6.0f,12.0f,12.0f);
            const double ah=stage0::world_heading_to_local(item.approach,frame);
            const PointF arrow_start(kingpin.X-static_cast<float>(std::sin(ah)*55),
                                     kingpin.Y+static_cast<float>(std::cos(ah)*55));
            g.DrawLine(&approach_pen,arrow_start,kingpin);
        }
        std::wstring label=item.label+L"  "+std::to_wstring(static_cast<int>(item.distance))+L"m";
        if (item.distance<=20.0 || attached)
            g.DrawString(label.c_str(),-1,&body,PointF(center.X+10,center.Y-12),&white);
    }

    const auto screen_point=[&](stage0::Vec2 world)
    {
        const auto local=stage0::world_to_local(world,frame);
        return PointF(cx+static_cast<float>(local.x*scale),
                      cy-static_cast<float>(local.y*scale));
    };
    const auto draw_path=[&](bool tractor,Gdiplus::Pen *pen)
    {
        if (!plan.found||plan.samples.size()<2) return;
        std::vector<PointF> points;
        points.reserve(plan.samples.size());
        for (const auto &sample:plan.samples)
            points.push_back(screen_point(tractor?sample.tractor_position:
                                                  sample.trailer_position));
        g.DrawLines(pen,points.data(),static_cast<INT>(points.size()));
        for (int marker=1;marker<=3;++marker)
        {
            const std::size_t index=std::min(points.size()-1,
                static_cast<std::size_t>(marker*(points.size()-1)/4));
            if (index==0) continue;
            const float dx=points[index].X-points[index-1].X;
            const float dy=points[index].Y-points[index-1].Y;
            const float norm=std::hypot(dx,dy);
            if (norm<0.1f) continue;
            const float ux=dx/norm,uy=dy/norm;
            PointF arrow[3]{points[index],
                PointF(points[index].X-ux*15.0f-uy*7.0f,
                       points[index].Y-uy*15.0f+ux*7.0f),
                PointF(points[index].X-ux*15.0f+uy*7.0f,
                       points[index].Y-uy*15.0f-ux*7.0f)};
            SolidBrush path_brush(tractor?Color(255,60,210,245):Color(255,255,190,55));
            g.FillPolygon(&path_brush,arrow,3);
        }
    };
    if (plan.found)
    {
        if (plan.mode==stage1a::PlannerMode::trailer_to_parking_target)
            draw_path(false,&trailer_path_pen);
        draw_path(true,&tractor_path_pen);
        const PointF start=screen_point(plan.mode==stage1a::PlannerMode::trailer_to_parking_target
            ?plan.samples.front().trailer_position:plan.samples.front().tractor_position);
        const PointF goal=screen_point(plan.mode==stage1a::PlannerMode::trailer_to_parking_target
            ?plan.samples.back().trailer_position:plan.samples.back().tractor_position);
        g.FillEllipse(&active_green,start.X-7.0f,start.Y-7.0f,14.0f,14.0f);
        g.FillEllipse(&yellow_fill,goal.X-8.0f,goal.Y-8.0f,16.0f,16.0f);
        g.DrawString(L"START",-1,&body,PointF(start.X+8,start.Y+5),&white);
        g.DrawString(L"GOAL",-1,&body,PointF(goal.X+8,goal.Y+5),&white);
    }

    float tx=830, ty=105;
    const auto line_brush=[&](const std::wstring &value, SolidBrush *brush)
    { g.DrawString(value.c_str(),-1,&body,PointF(tx,ty),brush); ty+=29; };
    const auto line=[&](const std::wstring &value) { line_brush(value,&white); };
    line(L"Observed game: ETS2 1.60.1.7");
    line(L"Speed: "+std::to_wstring(t.speed)+L" m/s");
    const std::wstring gear_label=t.gear<0 ? L"REVERSE" : L"NOT REVERSE";
    line(L"Gear state: "+gear_label+L" (SDK raw "+std::to_wstring(t.gear)+L")");
    line(L"Engine gear raw: "+std::to_wstring(t.engine_gear));
    line(L"Attached trailers: "+std::to_wstring(t.attached_count));
    const bool stage0_active=stage0::activation_valid(t.speed,t.gear);
    if (stage0_active)
        line_brush(L"Planner activation: YES (stopped + Reverse)", &active_green);
    else if (t.gear>=0)
        line_brush(L"Planner activation: NO (gear is not Reverse)", &muted);
    else
        line_brush(L"Planner activation: NO (vehicle is moving)", &muted);
    line(L"PLANNER MODE: "+std::wstring(stage1a::mode_name(plan.mode),
        stage1a::mode_name(plan.mode)+std::strlen(stage1a::mode_name(plan.mode))));
    if (plan.mode==stage1a::PlannerMode::tractor_to_target_trailer)
    {
        line(L"TARGET: TARGET TRAILER KINGPIN");
        line_brush(L"Target identity: VERIFIED; kingpin: HEURISTIC",&muted);
    }
    else if (plan.mode==stage1a::PlannerMode::trailer_to_parking_target)
    {
        line(L"TARGET: TASK PARKING K0");
        line_brush(L"K0 root/heading VERIFIED; center CALIBRATED",&muted);
    }
    if (plan.found)
    {
        line_brush(L"BEST PATH: KINEMATICALLY VALID (NOT COLLISION-SAFE)",&active_green);
        line(L"Candidates: "+std::to_wstring(plan.candidate_count)+L" / valid "+
             std::to_wstring(plan.valid_candidate_count));
        line(L"Known obstacle contours: "+std::to_wstring(plan.known_obstacle_count));
        line(L"Rejected obstacle/self/start: "+
             std::to_wstring(plan.obstacle_collision_rejections)+L" / "+
             std::to_wstring(plan.self_collision_rejections)+L" / "+
             std::to_wstring(plan.start_state_rejections));
        line(L"Rejected curvature/articulation/forward/other: "+
             std::to_wstring(plan.curvature_rejections)+L" / "+
             std::to_wstring(plan.articulation_rejections)+L" / "+
             std::to_wstring(plan.forward_motion_rejections)+L" / "+
             std::to_wstring(plan.other_rejections));
        line(L"Length: "+std::to_wstring(plan.path_length)+L" m");
        line(L"Max articulation: "+std::to_wstring(plan.max_articulation*180.0/stage0::pi)+L" deg");
        line(L"Final pos error: "+std::to_wstring(plan.final_position_error)+L" m");
        line(L"Final heading error: "+std::to_wstring(plan.final_heading_error*180.0/stage0::pi)+L" deg");
    }
    else
    {
        line_brush(L"NO_VALID_PATH: "+std::wstring(plan.reason.begin(),plan.reason.end()),
                   &failed_brush);
        line(L"Known obstacle contours: "+std::to_wstring(plan.known_obstacle_count));
        line(L"Rejected obstacle/self/start: "+
             std::to_wstring(plan.obstacle_collision_rejections)+L" / "+
             std::to_wstring(plan.self_collision_rejections)+L" / "+
             std::to_wstring(plan.start_state_rejections));
        line(L"Rejected curvature/articulation/forward/other: "+
             std::to_wstring(plan.curvature_rejections)+L" / "+
             std::to_wstring(plan.articulation_rejections)+L" / "+
             std::to_wstring(plan.forward_motion_rejections)+L" / "+
             std::to_wstring(plan.other_rejections));
    }
    const auto count_prefix=[&](wchar_t prefix)
    { return std::count_if(items.begin(),items.end(),[&](const Candidate &c)
        { return !c.label.empty() && c.label[0]==prefix; }); };
    line(L"Attached player trailers drawn: "+std::to_wstring(count_prefix(L'A')));
    line(L"Detached trailer candidates: "+std::to_wstring(count_prefix(L'D')));
    line(L"Active traffic vehicles: "+std::to_wstring(count_prefix(L'V')));
    line(L"Parked/passive vehicles: "+std::to_wstring(count_prefix(L'P')));
    line(L"Visible native guide markers: "+std::to_wstring(count_prefix(L'G')));
    line(L"Parking-guide outlines: "+std::to_wstring(count_prefix(L'K')));
    ty+=16;
    auto sorted_items=items;
    std::sort(sorted_items.begin(),sorted_items.end(),[](const Candidate &a,const Candidate &b)
        { return a.distance<b.distance; });
    for (std::size_t i=0;i<sorted_items.size() && i<15;++i)
    {
        const auto &c=sorted_items[i];
        std::wstring summary=c.label+L"  d="+std::to_wstring(c.distance)+L"m";
        if (!c.label.empty() && c.label[0]==L'G')
            summary+=L"  native loading.pmd pose  h="+std::to_wstring(c.heading);
        else if (!c.label.empty() && c.label[0]==L'K')
            summary+=(c.label.size()>1&&c.label[1]==L'?')
                ? L"  parking pose? (unverified)  h="+std::to_wstring(c.heading)
                : L"  native task parking guide (VERIFIED)  h="+
                    std::to_wstring(c.heading);
        else
            summary+=L"  W/L="+std::to_wstring(c.width)+L"/"+
                std::to_wstring(c.length)+L"  h="+std::to_wstring(c.heading);
        if (!c.label.empty() && c.label[0]==L'V')
            summary+=L"  v="+std::to_wstring(c.speed_mps)+L"m/s";
        line(summary);
    }
    ty+=16;
    if (t.attached_count==1)
        line(L"VehicleConfiguration: SINGLE_TRAILER");
    else if (selected_target_index>=0 && selected_target_index<static_cast<int>(count_prefix(L'D')))
        line(L"Result: TARGET_TRAILER_VERIFIED (native guide owner)");
    else
        line(L"Result: TARGET_TRAILER_AMBIGUOUS");
    line(L"Yellow = actor AABB-derived OBB");
    line(L"Cyan = ATTACHED PLAYER TRAILER");
    line(L"Magenta = ACTIVE/PARKED TRAFFIC VEHICLE");
    line(L"Green cross G = visible native no-collision guide pose");
    line(L"Orange dashed K0 = native task parking guide (VERIFIED)");
    line(L"Orange dashed K? = parking-pose memory candidate (UNVERIFIED)");
    line(L"Solid triangle = physical vehicle FRONT (actor -Z)");
    line(L"Green = center toward physical FRONT");
    line_brush(L"White dot = estimated kingpin", &muted);
    line_brush(L"Red = coupling-side approach into kingpin", &muted);
    line_brush(L"Kingpin/approach confidence: HEURISTIC", &muted);
    line_brush(L"Building edges: NOT ENUMERATED", &muted);

    CLSID encoder{};
    if (png_encoder_clsid(&encoder)==0)
    {
        const bool stage1_ok=image.Save(L"build\\stage1a_fixed_path.png",&encoder,nullptr)==Ok;
        const bool stage0_ok=image.Save(L"build\\stage0_live_snapshot.png",&encoder,nullptr)==Ok;
        ok=stage1_ok&&stage0_ok;
    }
    }
    GdiplusShutdown(token);
    return ok;
}

bool write_path_csv(const stage1a::PlanResult &plan)
{
    std::ofstream output("build/stage1a_best_path.csv",std::ios::trunc);
    if (!output) return false;
    output << "distance_m,tractor_x,tractor_z,tractor_heading_rad,"
              "hitch_x,hitch_z,trailer_x,trailer_z,trailer_heading_rad,"
              "articulation_rad,trailer_curvature_m_inv\n";
    output << std::fixed << std::setprecision(9);
    for (const auto &sample:plan.samples)
        output << sample.distance << ','
               << sample.tractor_position.x << ',' << sample.tractor_position.y << ','
               << sample.tractor_heading << ','
               << sample.hitch_position.x << ',' << sample.hitch_position.y << ','
               << sample.trailer_position.x << ',' << sample.trailer_position.y << ','
               << sample.trailer_heading << ',' << sample.articulation_angle << ','
               << sample.trailer_curvature << '\n';
    return output.good();
}

bool write_parking_target_seed_csv(const stage0::Pose2 &target)
{
    std::ofstream output("build/stage1a_best_path.csv",std::ios::trunc);
    if (!output) return false;
    output << "distance_m,tractor_x,tractor_z,tractor_heading_rad,"
              "hitch_x,hitch_z,trailer_x,trailer_z,trailer_heading_rad,"
              "articulation_rad,trailer_curvature_m_inv\n"
           << std::fixed << std::setprecision(9)
           << "0,0,0,0,0,0," << target.position.x << ','
           << target.position.y << ',' << target.heading_rad << ",0,0\n";
    return output.good();
}

bool write_tractor_rear_tracks_csv(const stage1a::PlanResult &plan,
                                   const Telemetry &telemetry)
{
    std::ofstream output("build/stage1a_tractor_rear_tracks.csv",std::ios::trunc);
    if (!output) return false;
    output << "distance_m,left_x,left_z,right_x,right_z,track_span_m\n"
           << std::fixed << std::setprecision(9);
    stage1a::VehicleGeometry geometry;
    geometry.tractor_width_m=telemetry.tractor_width_m;
    geometry.tractor_length_m=telemetry.tractor_length_m;
    for (const auto &sample:plan.samples)
    {
        const auto tracks=stage1a::tractor_rear_tracks(sample,geometry);
        output << sample.distance << ','
               << tracks.left.x << ',' << tracks.left.y << ','
               << tracks.right.x << ',' << tracks.right.y << ','
               << stage1a::length(stage1a::subtract(tracks.right,tracks.left))
               << '\n';
    }
    return output.good();
}

bool write_planner_diagnostics(const stage1a::PlanResult &plan,bool active,
                               int attached_count)
{
    std::ofstream output("build/stage1a_planner_diagnostics.txt",std::ios::trunc);
    if (!output) return false;
    output << std::fixed << std::setprecision(6)
           << "PlannerMode=" << stage1a::mode_name(plan.mode) << '\n'
           << "Activation=" << (active?"ACTIVE":"INACTIVE") << '\n'
           << "VehicleState=" << (attached_count==0?"TRACTOR_ONLY":
                                      "TRACTOR_WITH_ATTACHED_TRAILER") << '\n'
           << "TargetType=" << (plan.mode==stage1a::PlannerMode::tractor_to_target_trailer
                ?"TARGET_TRAILER_KINGPIN":plan.mode==stage1a::PlannerMode::trailer_to_parking_target
                ?"TASK_PARKING_K0":"NONE") << '\n'
           << "CandidateCount=" << plan.candidate_count << '\n'
           << "ValidCandidateCount=" << plan.valid_candidate_count << '\n'
           << "KnownObstacleCount=" << plan.known_obstacle_count << '\n'
           << "ObstacleCollisionRejections=" << plan.obstacle_collision_rejections << '\n'
           << "SelfCollisionRejections=" << plan.self_collision_rejections << '\n'
           << "StartStateRejections=" << plan.start_state_rejections << '\n'
           << "CurvatureRejections=" << plan.curvature_rejections << '\n'
           << "ArticulationRejections=" << plan.articulation_rejections << '\n'
           << "ForwardMotionRejections=" << plan.forward_motion_rejections << '\n'
           << "OtherRejections=" << plan.other_rejections << '\n'
           << "BestPathLength=" << plan.path_length << "m\n"
           << "MaxArticulation=" << plan.max_articulation*180.0/stage0::pi << "deg\n"
           << "MeanArticulation=" << plan.mean_articulation*180.0/stage0::pi << "deg\n"
           << "MaxCurvature=" << plan.max_curvature << "m^-1\n"
           << "CurvatureChange=" << plan.curvature_change << "m^-1\n"
           << "FinalPositionError=" << plan.final_position_error << "m\n"
           << "FinalHeadingError=" << plan.final_heading_error*180.0/stage0::pi << "deg\n"
           << "CollisionClaim=" << (plan.found
                ?"KNOWN_CONTOURS_CLEAR_IN_INCOMPLETE_MAP":"NONE") << '\n'
           << "ObstacleMap=INCOMPLETE_BUILDINGS_AND_TRAFFIC_NOT_FULLY_ENUMERATED\n"
           << "Result=" << (plan.found?"KINEMATIC_PATH_FOUND":"NO_VALID_PATH") << '\n'
           << "reason=" << plan.reason << '\n';
    return output.good();
}

bool write_obstacles_csv(const std::vector<stage1a::OrientedBox> &obstacles)
{
    std::ofstream output("build/stage1a_known_obstacles.csv",std::ios::trunc);
    if (!output) return false;
    output << "label,center_x,center_z,heading_rad,width_m,length_m\n"
           << std::fixed << std::setprecision(9);
    for (const auto &obstacle:obstacles)
        output << obstacle.label << ',' << obstacle.center.x << ','
               << obstacle.center.y << ',' << obstacle.heading_rad << ','
               << obstacle.width_m << ',' << obstacle.length_m << '\n';
    return output.good();
}
}

int wmain(int argc,wchar_t **argv)
{
    const bool parking_anchor_scan=argc>1&&
        _wcsicmp(argv[1],L"--parking-anchor-scan")==0;
    const bool verify_anchor_addresses=argc>1&&
        _wcsicmp(argv[1],L"--verify-anchor-addresses")==0;
    // Stage 1A always needs the exact native task-parking resource in attached
    // mode. The former opt-in diagnostic scan is now part of the normal
    // read-only planning pass.
    const bool parking_guide_scan=true;
    int selected_target_index=-1;
    const auto pid = find_game_process();
    if (!pid) { std::cerr << "BLOCKER_GAME_NOT_RUNNING\n"; return 2; }
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, *pid)};
    if (!process.value) { std::cerr << "BLOCKER_OPEN_PROCESS error=" << GetLastError() << "\n"; return 3; }
    const auto module = find_game_module(*pid);
    const auto text = module ? find_text_section(process.value, *module) : std::nullopt;
    if (!module || !text) { std::cerr << "BLOCKER_PE_LAYOUT\n"; return 4; }
    const auto matches = scan_game_traffic(process.value, *text);
    std::cout << "[ReversePlanner Stage1A]\nSignature: game_traffic matches=" << matches.size() << "\n";
    if (matches.size() != 1) { std::cout << "Result: BLOCKER_ENTITY_ENUMERATION\n"; return 5; }

    std::int32_t displacement{};
    if (!read_remote(process.value, matches[0] + 6, displacement)) return 6;
    const auto holder = matches[0] + 10 + displacement;
    std::uintptr_t game_traffic{};
    if (!read_remote(process.value, holder, game_traffic) || !game_traffic)
    { std::cout << "Result: BLOCKER_GAME_TRAFFIC_NULL\n"; return 7; }

    std::cout << "ParkingModelResourceProbe:\n"
              << "  primaryTaskParkingPath=/model/symbol/unload.pmd\n"
              << "  alternateActivationPath=/model/activation/parking.pmd\n"
              << "  instanceLookup=TARGETED_STATE_DIFFERENTIAL_REQUIRED\n";

    const auto telemetry = read_telemetry();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Player:\n  telemetry=" << (telemetry.available ? "available" : "missing")
              << "\n  speed_mps=" << telemetry.speed
              << "\n  engineGear=" << telemetry.engine_gear
              << "\n  displayedGear=" << telemetry.gear
              << "\n  attachedTrailerCount=" << telemetry.attached_count
              << "\n  frontHeading_rad=" << telemetry.heading
              << "\n  position=(" << telemetry.x << ',' << telemetry.y << ',' << telemetry.z << ")\n";
    for (std::size_t index=0;index<telemetry.attached_trailer_hook_poses.size();++index)
    {
        const auto &hook=telemetry.attached_trailer_hook_poses[index];
        std::cout << "  attachedTrailerHook[" << index << "]=(("
                  << hook.position.x << ',' << hook.position.y
                  << "),heading=" << hook.heading_rad << ")\n";
    }
    std::cout << "Activation:\n  stopped=" << (std::abs(telemetry.speed) <= stage0::stop_speed_epsilon_mps)
              << "\n  reverseGear=" << (telemetry.gear < 0)
              << "\n  valid=" << stage0::activation_valid(telemetry.speed, telemetry.gear) << "\n";

    ArrayHeader trailers{};
    if (!read_remote(process.value, game_traffic + 0x230, trailers) ||
        trailers.size > 128 || trailers.capacity < trailers.size || !trailers.value)
    { std::cout << "Result: BLOCKER_TRAILER_ARRAY_VALIDATION\n"; return 8; }
    std::cout << "Nearby scan:\n  radius_m=" << stage0::local_scan_radius_m
              << "\n  rawPlayerTrailers=" << trailers.size << "\n";

    std::vector<std::uintptr_t> pointers(trailers.size);
    if (!pointers.empty() && !read_remote_bytes(process.value, trailers.value,
        pointers.data(), pointers.size()*sizeof(pointers[0]))) return 9;
    int nearby = 0;
    int attached_player_trailers = 0;
    int native_guide_count = 0;
    int native_guide_owner_count = 0;
    int native_guide_owner_index = -1;
    std::unordered_set<std::uintptr_t> native_guide_model_vtables;
    std::vector<Candidate> candidates;

    // The delivery/parking target belongs to the player vehicle/job state, not
    // to the attached trailer's loading-guide branch. Resolve the player truck
    // through game_traffic::traffic_player_vehicles_1 and inspect only the
    // small object reachable from physics_vehicle + 0xF68. This is deliberately
    // bounded: one direct object and at most 64 immediate pointer children.
    std::uintptr_t player_vehicle{},player_wrapper{},physics_truck{},truck_f68{};
    ArrayHeader player_vehicles{};
    if (read_remote(process.value,game_traffic+0x0208,player_vehicles) &&
        player_vehicles.size<=16 && player_vehicles.capacity>=player_vehicles.size &&
        (!player_vehicles.size || player_vehicles.value))
    {
        std::vector<std::uintptr_t> player_entries(player_vehicles.size);
        if (player_entries.empty() || read_remote_bytes(process.value,player_vehicles.value,
            player_entries.data(),player_entries.size()*sizeof(player_entries[0])))
        {
            double best_distance=10.0;
            for (const auto entry:player_entries)
            {
                Placement placement{};
                if (!entry || !read_remote(process.value,entry+0x28,placement)) continue;
                const double world_x=placement.pos.x+placement.cx*512.0;
                const double world_z=placement.pos.z+placement.cz*512.0;
                const double dx=world_x-telemetry.x,dz=world_z-telemetry.z;
                const double distance=std::sqrt(dx*dx+dz*dz);
                if (!std::isfinite(distance)||distance>=best_distance) continue;
                std::uintptr_t wrapper{},physics{};
                if (!read_remote(process.value,entry+0x0208,wrapper) || !wrapper ||
                    !read_remote(process.value,wrapper+0x08,physics) || !physics) continue;
                player_vehicle=entry;
                player_wrapper=wrapper;
                physics_truck=physics;
                best_distance=distance;
            }
        }
    }
    if (physics_truck) read_remote(process.value,physics_truck+0x0F68,truck_f68);
    std::cout << "PlayerTruckTargetProbe:\n  playerVehicle=0x" << std::hex
              << player_vehicle << "\n  wrapper=0x" << player_wrapper
              << "\n  physicsTruck=0x" << physics_truck
              << "\n  truckF68=0x" << truck_f68 << std::dec
              << "\n  playerVehicleSlots=" << player_vehicles.size << "\n";

    struct ParkingPoseProbe
    {
        double x{},y{},z{},heading{},distance{},local_x{},local_y{};
        std::uintptr_t owner{};
        std::size_t offset{};
        int depth{};
    };
    std::vector<ParkingPoseProbe> parking_poses;
    const auto scan_parking_transforms=[&](std::uintptr_t owner,int depth)
    {
        std::array<std::uint8_t,0x800> bytes{};
        if (!owner || !read_remote_bytes(process.value,owner,bytes.data(),bytes.size())) return;
        for (std::size_t candidate_offset=0;
             candidate_offset+sizeof(PrismTransform)<=bytes.size();candidate_offset+=4)
        {
            PrismTransform transform{};
            std::memcpy(&transform,bytes.data()+candidate_offset,sizeof(transform));
            const double qnorm=static_cast<double>(transform.rot.w)*transform.rot.w+
                static_cast<double>(transform.rot.x)*transform.rot.x+
                static_cast<double>(transform.rot.y)*transform.rot.y+
                static_cast<double>(transform.rot.z)*transform.rot.z;
            if (!std::isfinite(qnorm)||qnorm<0.90||qnorm>1.10||
                !std::isfinite(transform.x)||!std::isfinite(transform.y)||
                !std::isfinite(transform.z)) continue;
            const double world_x=transform.x+transform.sector_x*512.0;
            const double world_z=transform.z+transform.sector_z*512.0;
            const auto local=stage0::world_to_local({world_x,world_z},
                {{telemetry.x,telemetry.z},telemetry.heading});
            const double distance=std::hypot(local.x,local.y);
            // User-observed parking guide: left/front, approximately 5-20 m.
            if (distance<5.0||distance>22.0||local.x>=-0.5||local.y<=0.5||
                std::abs(transform.y-telemetry.y)>8.0) continue;
            bool duplicate=false;
            for (const auto &known:parking_poses)
                if (std::hypot(world_x-known.x,world_z-known.z)<0.75)
                    { duplicate=true; break; }
            if (duplicate) continue;
            parking_poses.push_back({world_x,transform.y,world_z,
                heading_from_quaternion(transform.rot),distance,local.x,local.y,
                owner,candidate_offset,depth});
        }
    };
    scan_parking_transforms(truck_f68,0);
    if (truck_f68)
    {
        std::array<std::uintptr_t,0x180/sizeof(std::uintptr_t)> links{};
        if (read_remote_bytes(process.value,truck_f68,links.data(),sizeof(links)))
        {
            std::unordered_set<std::uintptr_t> seen_links;
            int inspected=0;
            for (const auto link:links)
            {
                if (inspected>=64) break;
                if (link<0x10000000000ULL||link>0x7FFFFFFFFFFFULL||
                    !seen_links.insert(link).second) continue;
                ++inspected;
                const auto link_rtti=read_rtti_name(process.value,*module,link);
                std::cout << "  child[" << inspected-1 << "]=0x" << std::hex << link
                          << std::dec << " rtti="
                          << (link_rtti.empty()?"<unreadable>":link_rtti) << "\n";
                scan_parking_transforms(link,1);
            }
            std::cout << "  immediatePointerChildrenInspected=" << inspected << "\n";
        }
    }
    // ETS2LA 1.60 labels game_traffic + 0x7B8 as base_ctrl. It is the next
    // narrow candidate for active world triggers (garage/hotel/delivery-like
    // areas). Probe the controller and one pointer level without assuming a
    // parking-target layout.
    std::uintptr_t base_ctrl{};
    read_remote(process.value,game_traffic+0x07B8,base_ctrl);
    std::cout << "BaseControlTriggerProbe:\n  baseCtrl=0x" << std::hex << base_ctrl
              << std::dec << "\n  rtti=";
    if (base_ctrl)
    {
        const auto base_rtti=read_rtti_name(process.value,*module,base_ctrl);
        std::cout << (base_rtti.empty()?"<unreadable>":base_rtti) << "\n";
        scan_parking_transforms(base_ctrl,0);
        std::array<std::uintptr_t,0x300/sizeof(std::uintptr_t)> links{};
        if (read_remote_bytes(process.value,base_ctrl,links.data(),sizeof(links)))
        {
            std::unordered_set<std::uintptr_t> seen_links;
            int inspected=0;
            for (const auto link:links)
            {
                if (inspected>=64) break;
                if (link<0x10000000000ULL||link>0x7FFFFFFFFFFFULL||
                    !seen_links.insert(link).second) continue;
                ++inspected;
                scan_parking_transforms(link,1);
            }
            std::cout << "  immediatePointerChildrenInspected=" << inspected << "\n";
        }
    }
    else std::cout << "<null>\n";
    for (std::size_t i=0;i<parking_poses.size();++i)
    {
        const auto &pose=parking_poses[i];
        std::cout << "ParkingGuidePoseCandidate[" << i << "]:\n  owner=0x" << std::hex
                  << pose.owner << "\n  offset=0x" << pose.offset << std::dec
                  << "\n  pointerDepth=" << pose.depth
                  << "\n  world=(" << pose.x << ',' << pose.y << ',' << pose.z << ')'
                  << "\n  local=(" << pose.local_x << ',' << pose.local_y << ')'
                  << "\n  distance_m=" << pose.distance
                  << "\n  heading_rad=" << pose.heading
                  << "\n  confidence=DIAGNOSTIC_UNVERIFIED\n";
        // A standard semitrailer-sized outline makes orientation visually
        // checkable; dimensions are illustrative until the owning structure is
        // proven to be the native parking frame.
        candidates.push_back({pose.owner,pose.distance,pose.x,pose.y,pose.z,
            pose.heading,2.6,13.7,0.0,{pose.x,pose.z},0.0,
            L"K?"+std::to_wstring(i),false,0.0});
    }
    for (std::size_t i=0; i<pointers.size(); ++i)
    {
        std::array<std::uint8_t, 0x128> object{};
        if (!pointers[i] || !read_remote_bytes(process.value, pointers[i], object.data(), object.size())) continue;
        std::uintptr_t player_object_wrapper{}, physics_trailer{}, native_guide_model{};
        std::memcpy(&player_object_wrapper,object.data()+0xF0,sizeof(player_object_wrapper));
        if (player_object_wrapper)
            read_remote(process.value,player_object_wrapper+0x08,physics_trailer);
        std::uint8_t guide_local_branch{},guide_visible{};
        std::uintptr_t trailer_f68{};
        std::array<float,7> guide_local{};
        if (physics_trailer)
        {
            read_remote(process.value,physics_trailer+0x0F68,trailer_f68);
            read_remote(process.value,physics_trailer+0x1110,native_guide_model);
            read_remote(process.value,physics_trailer+0x10F8,guide_local_branch);
            read_remote(process.value,physics_trailer+0x1120,guide_visible);
            read_remote_bytes(process.value,physics_trailer+0x1124,
                              guide_local.data(),guide_local.size()*sizeof(guide_local[0]));
        }
        Placement placement{}; AaBox box{};
        std::memcpy(&placement, object.data()+0x28, sizeof(placement));
        std::memcpy(&box, object.data()+0x48, sizeof(box));
        const Float3 base{placement.cx*512.0f+placement.pos.x, placement.pos.y,
                          placement.cz*512.0f+placement.pos.z};
        bool attached_to_player=false;
        for (const auto &attached:telemetry.attached_trailer_positions)
        {
            const double adx=base.x-attached.x, adz=base.z-attached.y;
            if (adx*adx+adz*adz<=9.0) { attached_to_player=true; break; }
        }
        const Float3 local_center{box.start.x+(box.end.x-box.start.x)*0.5f,
                                  box.start.y+(box.end.y-box.start.y)*0.5f,
                                  box.start.z+(box.end.z-box.start.z)*0.5f};
        const auto offset=rotate(local_center, placement.rot);
        const Float3 center{base.x+offset.x,base.y+offset.y,base.z+offset.z};
        const double dx=center.x-telemetry.x, dz=center.z-telemetry.z;
        const double distance=std::sqrt(dx*dx+dz*dz);
        if (distance > stage0::local_scan_radius_m) continue;
        const double width=std::abs(box.end.x-box.start.x);
        const double height=std::abs(box.end.y-box.start.y);
        const double length=std::abs(box.end.z-box.start.z);
        const double heading=heading_from_quaternion(placement.rot);
        const auto target=stage0::estimate_trailer_target({center.x,center.z},heading,{width,length});
        std::cout << "NativeGuideProbe[" << i << "]:\n  wrapper=0x" << std::hex
                  << player_object_wrapper << "\n  physicsTrailer=0x" << physics_trailer
                  << "\n  trailerF68=0x" << trailer_f68
                  << "\n  model=0x" << native_guide_model << std::dec
                  << "\n  localBranch=" << static_cast<int>(guide_local_branch)
                  << "\n  visible=" << static_cast<int>(guide_visible)
                  << "\n  localQuat=(" << guide_local[0] << ',' << guide_local[1] << ','
                  << guide_local[2] << ',' << guide_local[3] << ')'
                  << "\n  localPosition=(" << guide_local[4] << ',' << guide_local[5]
                  << ',' << guide_local[6] << ")\n";
        if (native_guide_model)
        {
            std::uintptr_t guide_model_vtable{};
            if (read_remote(process.value,native_guide_model,guide_model_vtable) &&
                guide_model_vtable>=module->base &&
                guide_model_vtable<module->base+module->size)
                native_guide_model_vtables.insert(guide_model_vtable);
            const auto guide_paths =
                find_model_resource_paths(process.value, native_guide_model);
            std::cout << "  modelResourcePaths=" << guide_paths.size() << "\n";
            for (const auto &[resource, path] : guide_paths)
                std::cout << "  modelResource=0x" << std::hex << resource
                          << std::dec << " path=" << path << "\n";
        }
        if (guide_visible && native_guide_model)
        {
            std::array<std::uint8_t,0x240> model_bytes{};
            if (read_remote_bytes(process.value,native_guide_model,model_bytes.data(),model_bytes.size()))
            {
                for (std::size_t candidate_offset=0;
                     candidate_offset+sizeof(PrismTransform)<=model_bytes.size();
                     candidate_offset+=4)
                {
                    PrismTransform transform{};
                    std::memcpy(&transform,model_bytes.data()+candidate_offset,sizeof(transform));
                    const double qnorm=static_cast<double>(transform.rot.w)*transform.rot.w+
                        static_cast<double>(transform.rot.x)*transform.rot.x+
                        static_cast<double>(transform.rot.y)*transform.rot.y+
                        static_cast<double>(transform.rot.z)*transform.rot.z;
                    if (!std::isfinite(qnorm)||qnorm<0.8||qnorm>1.2||
                        !std::isfinite(transform.x)||!std::isfinite(transform.y)||
                        !std::isfinite(transform.z)) continue;
                    const double world_x=transform.x+transform.sector_x*512.0;
                    const double world_z=transform.z+transform.sector_z*512.0;
                    const double mdx=world_x-center.x,mdz=world_z-center.z;
                    if (mdx*mdx+mdz*mdz>2500.0 || std::abs(transform.y-center.y)>20.0) continue;
                    std::cout << "NativeGuideTransformCandidate:\n  offset=0x" << std::hex
                              << candidate_offset << std::dec
                              << "\n  world=(" << world_x << ',' << transform.y << ',' << world_z << ')'
                              << "\n  distanceFromTrailerCenter_m=" << std::sqrt(mdx*mdx+mdz*mdz)
                              << "\n  quat=(" << transform.rot.w << ',' << transform.rot.x << ','
                              << transform.rot.y << ',' << transform.rot.z << ")\n";
                    if (candidate_offset==0x10)
                    {
                        const double guide_dx=world_x-telemetry.x;
                        const double guide_dz=world_z-telemetry.z;
                        candidates.push_back({native_guide_model,
                            std::sqrt(guide_dx*guide_dx+guide_dz*guide_dz),
                            world_x,transform.y,world_z,
                            heading_from_quaternion(transform.rot),0.0,0.0,0.0,
                            {world_x,world_z},0.0,
                            L"G"+std::to_wstring(native_guide_count++),false,0.0});
                    }
                }
            }
        }
        if (trailer_f68)
        {
            std::array<std::uint8_t,0x800> target_bytes{};
            if (read_remote_bytes(process.value,trailer_f68,target_bytes.data(),target_bytes.size()))
            {
                for (std::size_t candidate_offset=0;
                     candidate_offset+sizeof(PrismTransform)<=target_bytes.size();
                     candidate_offset+=4)
                {
                    PrismTransform transform{};
                    std::memcpy(&transform,target_bytes.data()+candidate_offset,sizeof(transform));
                    const double qnorm=static_cast<double>(transform.rot.w)*transform.rot.w+
                        static_cast<double>(transform.rot.x)*transform.rot.x+
                        static_cast<double>(transform.rot.y)*transform.rot.y+
                        static_cast<double>(transform.rot.z)*transform.rot.z;
                    if (!std::isfinite(qnorm)||qnorm<0.8||qnorm>1.2||
                        !std::isfinite(transform.x)||!std::isfinite(transform.y)||
                        !std::isfinite(transform.z)) continue;
                    const double world_x=transform.x+transform.sector_x*512.0;
                    const double world_z=transform.z+transform.sector_z*512.0;
                    const double tx=world_x-telemetry.x,tz=world_z-telemetry.z;
                    const double td=std::sqrt(tx*tx+tz*tz);
                    if (td<3.0||td>30.0||std::abs(transform.y-telemetry.y)>10.0) continue;
                    const auto local=stage0::world_to_local({world_x,world_z},
                        {{telemetry.x,telemetry.z},telemetry.heading});
                    std::cout << "TrailerF68TransformCandidate:\n  offset=0x" << std::hex
                              << candidate_offset << std::dec
                              << "\n  world=(" << world_x << ',' << transform.y << ',' << world_z << ')'
                              << "\n  local=(" << local.x << ',' << local.y << ')'
                              << "\n  distance_m=" << td
                              << "\n  quat=(" << transform.rot.w << ',' << transform.rot.x << ','
                              << transform.rot.y << ',' << transform.rot.z << ")\n";
                }
            }
        }
        if (attached_to_player)
        {
            std::cout << "AttachedTrailer[" << attached_player_trailers << "]:\n  entityId=0x"
                      << std::hex << pointers[i] << std::dec
                      << "\n  actorBase=(" << base.x << ',' << base.y << ',' << base.z << ')'
                      << "\n  center=(" << center.x << ',' << center.y << ',' << center.z << ')'
                      << "\n  heading_rad=" << heading
                      << "\n  bounds=(w=" << width << ",l=" << length << ",h=" << height << ")\n";
            candidates.push_back({pointers[i],distance,center.x,center.y,center.z,heading,
                                  width,length,height,target.kingpin,
                                  target.coupling_approach_heading_rad,
                                  L"A"+std::to_wstring(attached_player_trailers),false,0.0});
            ++attached_player_trailers;
            continue;
        }
        const bool native_guide_owner=guide_visible!=0 && native_guide_model!=0;
        if (native_guide_owner)
        {
            ++native_guide_owner_count;
            native_guide_owner_index=nearby;
        }
        std::cout << "TrailerCandidate[" << nearby << "]:\n  entityId=0x" << std::hex << pointers[i] << std::dec
                  << "\n  attached=false\n  distance_m=" << distance
                  << "\n  center=(" << center.x << ',' << center.y << ',' << center.z << ')'
                  << "\n  heading_rad=" << heading
                  << "\n  bounds=(w=" << width << ",l=" << length << ",h=" << height << ')'
                  << "\n  kingpin=(" << target.kingpin.x << ',' << target.kingpin.y << ')'
                  << "\n  couplingApproachHeading_rad=" << target.coupling_approach_heading_rad
                  << "\n  targetSelectionSource=NEARBY_PLAYER_TRAILER_LIST"
                  << "\n  targetSelectionConfidence=HEURISTIC"
                  << "\n  kingpinConfidence=HEURISTIC\n";
        candidates.push_back({pointers[i],distance,center.x,center.y,center.z,heading,
                              width,length,height,target.kingpin,
                              target.coupling_approach_heading_rad,
                              L"D"+std::to_wstring(nearby)+
                                  (native_guide_owner?L" TARGET (native guide verified)":L""),
                              native_guide_owner,0.0});
        ++nearby;
    }
    if (native_guide_owner_count==1)
        selected_target_index=native_guide_owner_index;

    if (parking_guide_scan)
    {
        const auto nodes=scan_model_nodes_near(process.value,*module,telemetry,
                                               native_guide_model_vtables);
        std::size_t unparented_node_count=0;
        struct IdentifiedParkingNode
        {
            const ModelNode *node{};
            std::string path;
        };
        std::vector<IdentifiedParkingNode> identified_parking_nodes;
        for (const auto &node : nodes)
        {
            if (node.common_model_inst && !node.parent) ++unparented_node_count;
            for (const auto &[resource, path] :
                 find_model_resource_paths(process.value, node.model))
            {
                (void)resource;
                if (path == "/model/symbol/loading.pmd" ||
                    path == "/model/symbol/loading.pmg" ||
                    path == "/model/symbol/unload.pmd" ||
                    path == "/model/symbol/unload.pmg" ||
                    path == "/model/activation/parking.pmd" ||
                    path == "/model/activation/parking.pmg" ||
                    path == "/model/activation/unload_select.pmd" ||
                    path == "/model/activation/unload_select.pmg")
                    identified_parking_nodes.push_back({&node, path});
            }
        }
        std::unordered_set<std::uintptr_t> exact_model_instances;
        for (const auto &match : identified_parking_nodes)
            exact_model_instances.insert(match.node->model);
        std::cout << "NativeGuideResourceIdentityScan:\n  allNearbyModelNodes="
                  << nodes.size() << "\n  exactResourceMatches="
                  << identified_parking_nodes.size()
                  << "\n  uniqueExactModelInstances="
                  << exact_model_instances.size() << "\n";
        for (std::size_t index=0; index<identified_parking_nodes.size(); ++index)
        {
            const auto &match=identified_parking_nodes[index];
            std::cout << "  match[" << index << "] model=0x" << std::hex
                      << match.node->model << " parent=0x" << match.node->parent
                      << " vtable=0x" << match.node->vtable
                      << std::dec << " world=(" << match.node->x << ','
                      << match.node->y << ',' << match.node->z << ") heading="
                      << match.node->heading << " path=" << match.path << "\n";
        }
        std::vector<const ModelNode *> unload_roots;
        for (const auto &match : identified_parking_nodes)
            if (match.path == "/model/symbol/unload.pmd" &&
                std::find(unload_roots.begin(),unload_roots.end(),match.node)==
                    unload_roots.end())
                unload_roots.push_back(match.node);
        const auto attached_for_target=std::find_if(candidates.begin(),candidates.end(),
            [](const Candidate &candidate)
            { return !candidate.label.empty()&&candidate.label[0]==L'A'; });
        const Candidate *target_geometry=nullptr;
        const char *target_geometry_source=nullptr;
        if (attached_for_target!=candidates.end())
        {
            target_geometry=&*attached_for_target;
            target_geometry_source="ATTACHED_PLAYER_TRAILER";
        }
        else
        {
            const auto verified_detached=std::find_if(candidates.begin(),candidates.end(),
                [](const Candidate &candidate)
                { return !candidate.label.empty() && candidate.label[0]==L'D' &&
                         candidate.label.find(L"TARGET")!=std::wstring::npos; });
            if (verified_detached!=candidates.end())
            {
                target_geometry=&*verified_detached;
                target_geometry_source="NATIVE_GUIDE_VERIFIED_TARGET_TRAILER";
            }
        }
        if (unload_roots.size()==1 && target_geometry)
        {
            // Single-scene calibration: when the frame was green, its root was
            // 0.543 m behind the attached trailer's rear bound. The resource
            // root and directed heading are verified; this center offset must
            // remain explicitly CALIBRATED until repeated at another target.
            constexpr double unload_root_rear_margin_m=0.543;
            const auto *root=unload_roots.front();
            const double target_width=target_geometry->width;
            const double target_length=target_geometry->length;
            const double center_offset=target_length*0.5+
                                       unload_root_rear_margin_m;
            const double center_x=root->x+std::sin(root->heading)*center_offset;
            const double center_z=root->z+std::cos(root->heading)*center_offset;
            const double distance=std::hypot(center_x-telemetry.x,
                                              center_z-telemetry.z);
            candidates.push_back({root->model,distance,center_x,root->y,center_z,
                root->heading,target_width,target_length,
                0.10,{center_x,center_z},stage0::wrap_angle(root->heading+stage0::pi),
                L"K0",false,0.0});
            std::cout << "VerifiedTaskParkingTarget:\n"
                      << "  resourceIdentity=VERIFIED\n"
                      << "  rootPose=VERIFIED\n"
                      << "  centerOffsetSource=GREEN_DOCKED_SINGLE_SCENE_CALIBRATION\n"
                      << "  targetGeometrySource=" << target_geometry_source << "\n"
                      << "  centerOffsetConfidence=CALIBRATED\n"
                      << "  center=(" << center_x << ',' << root->y << ','
                      << center_z << ")\n"
                      << "  targetHeading=" << root->heading << "\n"
                      << "  targetHeadingConfidence=VERIFIED_BY_DOCKED_ALIGNMENT\n"
                      << "  entryHeading=" << stage0::wrap_angle(root->heading+stage0::pi)
                      << "\n  entryDirectionConfidence=HEURISTIC\n";
        }
        struct GuidePattern
        {
            bool valid{};
            double score{},x{},z{},heading{},rail_separation{},marker_span{};
            std::size_t count{};
        } best;
        for (const auto &seed:nodes)
        {
            if (seed.parent) continue;
            if (!seed.common_model_inst) continue;
            std::vector<const ModelNode *> group;
            for (const auto &node:nodes)
            {
                if (node.parent) continue;
                if (!node.common_model_inst) continue;
                if (std::hypot(node.x-seed.x,node.z-seed.z)>4.5) continue;
                const double raw=std::abs(stage0::wrap_angle(node.heading-seed.heading));
                if (std::min(raw,std::abs(stage0::pi-raw))<=0.12) group.push_back(&node);
            }
            if (group.size()<12||group.size()>40) continue;
            double sin2=0.0,cos2=0.0,mean_x=0.0,mean_z=0.0;
            for (const auto *node:group)
            {
                sin2+=std::sin(2.0*node->heading);
                cos2+=std::cos(2.0*node->heading);
                mean_x+=node->x; mean_z+=node->z;
            }
            mean_x/=group.size(); mean_z/=group.size();
            double heading=0.5*std::atan2(sin2,cos2);
            const double sx=std::cos(heading),sz=-std::sin(heading);
            const double fx=std::sin(heading),fz=std::cos(heading);
            std::vector<double> side,longitudinal;
            side.reserve(group.size()); longitudinal.reserve(group.size());
            for (const auto *node:group)
            {
                side.push_back((node->x-mean_x)*sx+(node->z-mean_z)*sz);
                longitudinal.push_back((node->x-mean_x)*fx+(node->z-mean_z)*fz);
            }
            std::vector<double> sorted_side=side;
            std::sort(sorted_side.begin(),sorted_side.end());
            std::size_t split=0;
            double largest_gap=0.0;
            for (std::size_t i=1;i<sorted_side.size();++i)
                if (sorted_side[i]-sorted_side[i-1]>largest_gap)
                    { largest_gap=sorted_side[i]-sorted_side[i-1]; split=i; }
            if (split<5||sorted_side.size()-split<5||largest_gap<1.0) continue;
            const double low_mean=std::accumulate(sorted_side.begin(),
                sorted_side.begin()+split,0.0)/split;
            const double high_mean=std::accumulate(sorted_side.begin()+split,
                sorted_side.end(),0.0)/(sorted_side.size()-split);
            const double separation=high_mean-low_mean;
            const auto [long_min,long_max]=std::minmax_element(longitudinal.begin(),
                                                               longitudinal.end());
            const double span=*long_max-*long_min;
            if (separation<2.5||separation>3.3||span<1.2||span>4.5) continue;
            const double side_center=(low_mean+high_mean)*0.5;
            const double long_center=(*long_min+*long_max)*0.5;
            const double center_x=mean_x+side_center*sx+long_center*fx;
            const double center_z=mean_z+side_center*sz+long_center*fz;
            const double score=group.size()-std::abs(separation-2.9)*8.0-
                std::abs(span-3.0)*2.0;
            if (!best.valid||score>best.score)
                best={true,score,center_x,center_z,heading,separation,span,group.size()};
        }
        std::cout << "NativeParkingGuidePatternScan:\n  unparentedModelNodes="
                  << unparented_node_count << "\n";
        if (best.valid)
        {
            std::cout << "  result=REJECTED_GENERIC_MODEL_NODE_PATTERN"
                      << "\n  markerNodes=" << best.count
                      << "\n  center=(" << best.x << ',' << telemetry.y << ',' << best.z << ')'
                      << "\n  heading_rad=" << best.heading
                      << "\n  railSeparation_m=" << best.rail_separation
                      << "\n  markerSpan_m=" << best.marker_span
                      << "\n  rejectionReason=CONFOUNDED_BY_OTHER_MOD_MODEL_INSTANCES"
                      << "\n  confidence=REJECTED_NOT_A_PARKING_GUIDE\n";
        }
        else std::cout << "  result=NO_UNIQUE_PARKING_GUIDE_PATTERN\n";
    }

    if (parking_anchor_scan)
    {
        std::ofstream anchor_output("build/stage0_live_scan_green_anchor.txt",
                                    std::ios::trunc);
        const auto attached=std::find_if(candidates.begin(),candidates.end(),
            [](const Candidate &candidate)
            { return !candidate.label.empty()&&candidate.label[0]==L'A'; });
        if (attached!=candidates.end())
        {
            const auto hits=scan_private_transforms_near(process.value,attached->x,
                attached->y,attached->z,attached->heading);
            std::cout << "ParkingAnchorTransformScan:\n  matches=" << hits.size()
                      << "\n  anchorCenter=(" << attached->x << ',' << attached->y
                      << ',' << attached->z << ")\n  anchorHeading=" << attached->heading << "\n";
            if (anchor_output)
                anchor_output << std::setprecision(12)
                              << "ParkingAnchorTransformScan:\n  matches="
                              << hits.size() << "\n  anchorCenter=("
                              << attached->x << ',' << attached->y << ','
                              << attached->z << ")\n  anchorHeading="
                              << attached->heading << "\n";
            for (std::size_t i=0;i<hits.size();++i)
            {
                const auto &hit=hits[i];
                std::cout << "  hit[" << i << "] address=0x" << std::hex
                          << hit.address << std::dec
                          << " world=(" << hit.world_x << ',' << hit.transform.y
                          << ',' << hit.world_z << ") heading=" << hit.heading
                          << " positionError=" << hit.position_error
                          << " headingErrorModuloPi=" << hit.heading_error << "\n";
                if (anchor_output)
                    anchor_output << "  hit[" << i << "] address=0x"
                                  << std::hex << hit.address << std::dec
                                  << " world=(" << hit.world_x << ','
                                  << hit.transform.y << ',' << hit.world_z
                                  << ") heading=" << hit.heading
                                  << " positionError=" << hit.position_error
                                  << " headingErrorModuloPi="
                                  << hit.heading_error << "\n";
            }
        }
        else std::cout << "ParkingAnchorTransformScan:\n  blocker=NO_ATTACHED_TRAILER\n";
    }
    if (verify_anchor_addresses)
    {
        std::ifstream baseline("build/stage0_live_scan_green_anchor.txt");
        struct BaselineHit { std::uintptr_t address{}; double x{},y{},z{},heading{}; };
        std::vector<BaselineHit> baseline_hits;
        double baseline_anchor_x{},baseline_anchor_y{},baseline_anchor_z{},baseline_anchor_heading{};
        std::string line;
        while (std::getline(baseline,line))
        {
            if (sscanf_s(line.c_str(),"  anchorCenter=(%lf,%lf,%lf)",
                &baseline_anchor_x,&baseline_anchor_y,&baseline_anchor_z)==3) continue;
            if (sscanf_s(line.c_str(),"  anchorHeading=%lf",&baseline_anchor_heading)==1)
                continue;
            const auto marker=line.find(" address=0x");
            if (line.rfind("  hit[",0)!=0||marker==std::string::npos) continue;
            BaselineHit hit{};
            unsigned long long raw_address{};
            if (sscanf_s(line.c_str(),
                "  hit[%*u] address=0x%llx world=(%lf,%lf,%lf) heading=%lf",
                &raw_address,&hit.x,&hit.y,&hit.z,&hit.heading)==5)
            {
                hit.address=static_cast<std::uintptr_t>(raw_address);
                baseline_hits.push_back(hit);
            }
        }
        std::cout << "ParkingAnchorAddressVerification:\n  baselineAddresses="
                  << baseline_hits.size() << "\n";
        const auto attached=std::find_if(candidates.begin(),candidates.end(),
            [](const Candidate &candidate)
            { return !candidate.label.empty()&&candidate.label[0]==L'A'; });
        std::vector<std::array<double,4>> stable_root_poses;
        for (std::size_t i=0;i<baseline_hits.size();++i)
        {
            const auto &baseline_hit=baseline_hits[i];
            PrismTransform transform{};
            if (!read_remote(process.value,baseline_hit.address,transform))
            {
                std::cout << "  verify[" << i << "] address=0x" << std::hex
                          << baseline_hit.address << std::dec << " unreadable\n";
                continue;
            }
            const double world_x=transform.x+transform.sector_x*512.0;
            const double world_z=transform.z+transform.sector_z*512.0;
            const double delta_from_green=std::hypot(world_x-baseline_hit.x,
                                                       world_z-baseline_hit.z);
            const double distance_to_trailer=attached==candidates.end()
                ? -1.0:std::hypot(world_x-attached->x,world_z-attached->z);
            std::uintptr_t model_vtable{},parent{};
            read_remote(process.value,baseline_hit.address-0x10,model_vtable);
            read_remote(process.value,baseline_hit.address+0x40,parent);
            const bool vtable_in_game=model_vtable>=module->base&&
                model_vtable<module->base+module->size;
            const double distance_to_green_anchor=std::hypot(
                world_x-baseline_anchor_x,world_z-baseline_anchor_z);
            if (delta_from_green<0.05&&!vtable_in_game&&distance_to_green_anchor<1.5)
                stable_root_poses.push_back({world_x,transform.y,world_z,
                                             heading_from_quaternion(transform.rot)});
            std::cout << "  verify[" << i << "] address=0x" << std::hex
                      << baseline_hit.address << " modelBase=0x"
                      << baseline_hit.address-0x10 << " vtable=0x" << model_vtable
                      << " parent=0x" << parent << std::dec
                      << " vtableInGame=" << vtable_in_game
                      << " world=(" << world_x << ',' << transform.y << ',' << world_z
                      << ") heading=" << heading_from_quaternion(transform.rot)
                      << " deltaFromGreen=" << delta_from_green
                      << " distanceToCurrentTrailer=" << distance_to_trailer << "\n";
            if (delta_from_green < 0.05 && vtable_in_game)
            {
                const auto model_base = baseline_hit.address - 0x10;
                const auto resource_paths =
                    find_model_resource_paths(process.value, model_base);
                std::cout << "    stationaryModelResourcePaths="
                          << resource_paths.size() << "\n";
                for (const auto &[resource, path] : resource_paths)
                    std::cout << "    resource=0x" << std::hex << resource
                              << std::dec << " path=" << path << "\n";
            }
        }
        if (stable_root_poses.size()>=2)
        {
            double x=0.0,y=0.0,z=0.0,sin2=0.0,cos2=0.0;
            for (const auto &pose:stable_root_poses)
            {
                x+=pose[0]; y+=pose[1]; z+=pose[2];
                sin2+=std::sin(2.0*pose[3]); cos2+=std::cos(2.0*pose[3]);
            }
            x/=stable_root_poses.size(); y/=stable_root_poses.size();
            z/=stable_root_poses.size();
            double heading=0.5*std::atan2(sin2,cos2);
            if (std::abs(stage0::wrap_angle(heading-baseline_anchor_heading))>stage0::pi*0.5)
                heading=stage0::wrap_angle(heading+stage0::pi);
            std::cout << "  stablePoseCopies=" << stable_root_poses.size()
                      << "\n  stableCenter=(" << x << ',' << y << ',' << z << ')'
                      << "\n  stableHeading=" << heading
                      << "\n  result=REJECTED_WITHOUT_NATIVE_RESOURCE_IDENTITY\n";
        }
    }

    // Dynamic road traffic is not represented by traffic_player_trailers.
    // Enumerate the two live spawned-vehicle arrays used by ETS2LA 1.60.
    int active_traffic_count=0;
    std::unordered_set<std::uintptr_t> seen_active_vehicles;
    const auto scan_active_array=[&](std::uintptr_t array_offset)
    {
        ArrayHeader active{};
        if (!read_remote(process.value,game_traffic+array_offset,active) ||
            active.size>512 || active.capacity<active.size ||
            (active.size && !active.value)) return;
        struct SpawnedVehicle { std::uintptr_t vehicle{}; std::uint64_t reserved{}; };
        std::vector<SpawnedVehicle> entries(active.size);
        if (!entries.empty() && !read_remote_bytes(process.value,active.value,
            entries.data(),entries.size()*sizeof(entries[0]))) return;
        for (const auto &entry:entries)
        {
            if (!entry.vehicle || !seen_active_vehicles.insert(entry.vehicle).second) continue;
            std::array<std::uint8_t,0x240> actor{};
            if (!read_remote_bytes(process.value,entry.vehicle,actor.data(),actor.size())) continue;
            Placement placement{}; AaBox box{}; std::uintptr_t physics{};
            std::memcpy(&placement,actor.data()+0x28,sizeof(placement));
            std::memcpy(&box,actor.data()+0x48,sizeof(box));
            std::memcpy(&physics,actor.data()+0x238,sizeof(physics));
            const double width=std::abs(box.end.x-box.start.x);
            const double height=std::abs(box.end.y-box.start.y);
            const double length=std::abs(box.end.z-box.start.z);
            if (!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(length)||
                width<0.5||width>5.0||height<0.5||height>6.0||length<0.5||length>25.0) continue;
            const Float3 base{placement.cx*512.0f+placement.pos.x,placement.pos.y,
                              placement.cz*512.0f+placement.pos.z};
            const Float3 local_center{box.start.x+(box.end.x-box.start.x)*0.5f,
                                      box.start.y+(box.end.y-box.start.y)*0.5f,
                                      box.start.z+(box.end.z-box.start.z)*0.5f};
            const auto offset=rotate(local_center,placement.rot);
            const Float3 center{base.x+offset.x,base.y+offset.y,base.z+offset.z};
            const double dx=center.x-telemetry.x,dz=center.z-telemetry.z;
            const double distance=std::sqrt(dx*dx+dz*dz);
            if (!std::isfinite(distance)||distance>stage0::local_scan_radius_m) continue;
            float speed{};
            if (physics) read_remote(process.value,physics+0x70,speed);
            const double heading=heading_from_quaternion(placement.rot);
            candidates.push_back({entry.vehicle,distance,center.x,center.y,center.z,
                                  heading,width,length,height,{center.x,center.z},0.0,
                                  L"V"+std::to_wstring(active_traffic_count),false,speed});
            std::cout << "ActiveTrafficVehicle[" << active_traffic_count << "]:\n  entityId=0x"
                      << std::hex << entry.vehicle << std::dec
                      << "\n  distance_m=" << distance
                      << "\n  center=(" << center.x << ',' << center.y << ',' << center.z << ')'
                      << "\n  heading_rad=" << heading
                      << "\n  speed_mps=" << speed
                      << "\n  bounds=(w=" << width << ",l=" << length << ",h=" << height << ")\n";
            ++active_traffic_count;
        }
    };
    scan_active_array(0x00f0);
    scan_active_array(0x0118);

    // These two arrays contain direct traffic_ai_vehicle pointers and can retain
    // stopped vehicles that have already left the spawned rendering queues.
    const auto scan_ai_pointer_array=[&](std::uintptr_t array_offset)
    {
        ArrayHeader active{};
        if (!read_remote(process.value,game_traffic+array_offset,active) ||
            active.size>512 || active.capacity<active.size ||
            (active.size && !active.value)) return;
        std::vector<std::uintptr_t> entries(active.size);
        if (!entries.empty() && !read_remote_bytes(process.value,active.value,
            entries.data(),entries.size()*sizeof(entries[0]))) return;
        for (const auto vehicle:entries)
        {
            if (!vehicle || !seen_active_vehicles.insert(vehicle).second) continue;
            std::array<std::uint8_t,0x240> actor{};
            if (!read_remote_bytes(process.value,vehicle,actor.data(),actor.size())) continue;
            Placement placement{}; AaBox box{}; std::uintptr_t physics{};
            std::memcpy(&placement,actor.data()+0x28,sizeof(placement));
            std::memcpy(&box,actor.data()+0x48,sizeof(box));
            std::memcpy(&physics,actor.data()+0x238,sizeof(physics));
            const double width=std::abs(box.end.x-box.start.x);
            const double height=std::abs(box.end.y-box.start.y);
            const double length=std::abs(box.end.z-box.start.z);
            if (!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(length)||
                width<0.5||width>5.0||height<0.5||height>6.0||length<0.5||length>25.0) continue;
            const Float3 base{placement.cx*512.0f+placement.pos.x,placement.pos.y,
                              placement.cz*512.0f+placement.pos.z};
            const Float3 local_center{box.start.x+(box.end.x-box.start.x)*0.5f,
                                      box.start.y+(box.end.y-box.start.y)*0.5f,
                                      box.start.z+(box.end.z-box.start.z)*0.5f};
            const auto offset=rotate(local_center,placement.rot);
            const Float3 center{base.x+offset.x,base.y+offset.y,base.z+offset.z};
            const double dx=center.x-telemetry.x,dz=center.z-telemetry.z;
            const double distance=std::sqrt(dx*dx+dz*dz);
            if (!std::isfinite(distance)||distance>stage0::local_scan_radius_m) continue;
            float speed{};
            if (physics) read_remote(process.value,physics+0x70,speed);
            const double heading=heading_from_quaternion(placement.rot);
            candidates.push_back({vehicle,distance,center.x,center.y,center.z,
                heading,width,length,height,{center.x,center.z},0.0,
                L"V"+std::to_wstring(active_traffic_count),false,speed});
            std::cout << "AiTrafficVehicle[" << active_traffic_count << "]:\n  entityId=0x"
                      << std::hex << vehicle << std::dec
                      << "\n  sourceOffset=0x" << std::hex << array_offset << std::dec
                      << "\n  distance_m=" << distance
                      << "\n  center=(" << center.x << ',' << center.y << ',' << center.z << ')'
                      << "\n  heading_rad=" << heading
                      << "\n  speed_mps=" << speed
                      << "\n  bounds=(w=" << width << ",l=" << length << ",h=" << height << ")\n";
            ++active_traffic_count;
        }
    };
    scan_ai_pointer_array(0x0168);
    scan_ai_pointer_array(0x0190);

    // traffic_objects_1 also retains stopped AI actors which may be absent from
    // the two spawned-vehicle arrays. RTTI is used only as a type filter; no
    // remote method is invoked. Multi-inheritance parked trailers have their
    // actor at +0x38.
    int parked_actor_count=0;
    std::unordered_map<std::string,int> traffic_object_classes;
    ArrayHeader traffic_objects{};
    if (read_remote(process.value,game_traffic+0x01e0,traffic_objects) &&
        traffic_objects.size<=1024 && traffic_objects.capacity>=traffic_objects.size &&
        (!traffic_objects.size || traffic_objects.value))
    {
        std::vector<std::uintptr_t> objects(traffic_objects.size);
        if (objects.empty() || read_remote_bytes(process.value,traffic_objects.value,
            objects.data(),objects.size()*sizeof(objects[0])))
        {
            for (const auto object:objects)
            {
                if (!object) continue;
                const auto rtti=read_rtti_name(process.value,*module,object);
                ++traffic_object_classes[rtti.empty() ? "<unreadable-rtti>" : rtti];
                const bool is_parked=rtti.find("traffic_parked")!=std::string::npos;
                const bool is_ai_vehicle=rtti.find("traffic_ai_vehicle")!=std::string::npos;
                const auto virtual_type=read_constant_virtual_type(process.value,object);
                const std::array<std::uintptr_t,2> actor_offsets{0x00,0x38};
                for (const auto actor_offset:actor_offsets)
                {
                if (virtual_type && (*virtual_type==5 || *virtual_type==6) &&
                    actor_offset!=0x00) continue;
                if (is_parked && rtti.find("traffic_parked_trailer")!=std::string::npos &&
                    actor_offset!=0x38) continue;
                if ((!rtti.empty() && !is_parked && !is_ai_vehicle) ||
                    (is_ai_vehicle && actor_offset!=0x00)) continue;
                Placement placement{}; AaBox box{};
                if (!read_remote(process.value,object+actor_offset+0x28,placement) ||
                    !read_remote(process.value,object+actor_offset+0x48,box)) continue;
                const double width=std::abs(box.end.x-box.start.x);
                const double height=std::abs(box.end.y-box.start.y);
                const double length=std::abs(box.end.z-box.start.z);
                if (!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(length)||
                    width<1.4||width>3.2||height<0.8||height>5.0||length<2.5||length>18.0) continue;
                const Float3 base{placement.cx*512.0f+placement.pos.x,placement.pos.y,
                                  placement.cz*512.0f+placement.pos.z};
                const Float3 local_center{box.start.x+(box.end.x-box.start.x)*0.5f,
                                          box.start.y+(box.end.y-box.start.y)*0.5f,
                                          box.start.z+(box.end.z-box.start.z)*0.5f};
                const auto offset=rotate(local_center,placement.rot);
                const Float3 center{base.x+offset.x,base.y+offset.y,base.z+offset.z};
                const double dx=center.x-telemetry.x,dz=center.z-telemetry.z;
                const double distance=std::sqrt(dx*dx+dz*dz);
                if (!std::isfinite(distance)||distance>stage0::local_scan_radius_m ||
                    std::abs(center.y-telemetry.y)>6.0) continue;
                bool duplicate=false;
                for (const auto &existing:candidates)
                {
                    const double ex=center.x-existing.x,ez=center.z-existing.z;
                    if (ex*ex+ez*ez<1.0) { duplicate=true; break; }
                }
                if (duplicate) continue;
                const double heading=heading_from_quaternion(placement.rot);
                std::uintptr_t parked_model_base{},model_holder{},physics_holder{};
                std::uintptr_t parked_model{},parked_physics{};
                if (virtual_type && (*virtual_type==5 || *virtual_type==6))
                {
                    read_remote(process.value,object+actor_offset+0x80,parked_model_base);
                    read_remote(process.value,object+actor_offset+0x90,model_holder);
                    read_remote(process.value,object+actor_offset+0x98,physics_holder);
                    if (model_holder) read_remote(process.value,model_holder,parked_model);
                    if (physics_holder) read_remote(process.value,physics_holder,parked_physics);
                }
                float speed{};
                if (is_ai_vehicle)
                {
                    std::uintptr_t physics{};
                    if (read_remote(process.value,object+actor_offset+0x238,physics) && physics)
                        read_remote(process.value,physics+0x70,speed);
                }
                const std::wstring label=is_ai_vehicle
                    ? L"V"+std::to_wstring(active_traffic_count)
                    : L"P?"+std::to_wstring(parked_actor_count);
                candidates.push_back({object,distance,center.x,center.y,center.z,heading,
                    width,length,height,{center.x,center.z},0.0,
                    label,false,speed});
                std::cout << (is_ai_vehicle ? "TrafficObjectAiVehicle[" : "ParkedTrafficActor[")
                          << (is_ai_vehicle ? active_traffic_count : parked_actor_count)
                          << "]:\n  entityId=0x"
                          << std::hex << object << std::dec
                          << "\n  class=" << rtti
                          << "\n  virtualType="
                          << (virtual_type ? std::to_string(*virtual_type) : "UNKNOWN")
                          << "\n  actorOffset=0x" << std::hex << actor_offset << std::dec
                          << "\n  parkedModelBase=0x" << std::hex << parked_model_base
                          << "\n  parkedModel=0x" << parked_model
                          << "\n  parkedPhysics=0x" << parked_physics << std::dec
                          << "\n  classification="
                          << (is_ai_vehicle ? "AI_VEHICLE" : "UNVERIFIED_VEHICLE_SHAPED_ACTOR")
                          << "\n  distance_m=" << distance
                          << "\n  center=(" << center.x << ',' << center.y << ',' << center.z << ')'
                          << "\n  heading_rad=" << heading
                          << "\n  speed_mps=" << speed
                          << "\n  bounds=(w=" << width << ",l=" << length << ",h=" << height << ")\n";
                if (is_ai_vehicle) ++active_traffic_count;
                else ++parked_actor_count;
                }
            }
        }
    }
    std::cout << "Traffic object table:\n  slots=" << traffic_objects.size << "\n";
    for (const auto &[name,count]:traffic_object_classes)
        std::cout << "  class[" << name << "]=" << count << "\n";
    std::cout << "Nearby trailers:\n  count=" << nearby << "\n";
    std::cout << "Attached player trailers matched:\n  count=" << attached_player_trailers << "\n";
    std::cout << "Active traffic vehicles:\n  count=" << active_traffic_count << "\n";
    std::cout << "Parked/passive traffic actors:\n  count=" << parked_actor_count << "\n";

    const auto verified_target=std::find_if(candidates.begin(),candidates.end(),
        [](const Candidate &candidate)
        { return !candidate.label.empty()&&candidate.label[0]==L'D'&&
                 candidate.label.find(L"TARGET")!=std::wstring::npos; });
    const auto attached_target=std::find_if(candidates.begin(),candidates.end(),
        [](const Candidate &candidate)
        { return !candidate.label.empty()&&candidate.label[0]==L'A'; });
    const auto parking_target=std::find_if(candidates.begin(),candidates.end(),
        [](const Candidate &candidate) { return candidate.label==L"K0"; });
    const int parking_target_count=static_cast<int>(std::count_if(
        candidates.begin(),candidates.end(),
        [](const Candidate &candidate) { return candidate.label==L"K0"; }));
    const bool planner_active=stage0::activation_valid(telemetry.speed,telemetry.gear);
    const auto mode_decision=stage1a::select_mode(
        planner_active,telemetry.attached_count,
        native_guide_owner_count==1&&verified_target!=candidates.end(),
        attached_player_trailers==1&&attached_target!=candidates.end(),
        parking_target_count);

    stage1a::PlanResult fixed_path;
    std::optional<stage0::Pose2> perceived_parking_target;
    std::vector<stage1a::OrientedBox> live_obstacles;
    fixed_path.mode=mode_decision.mode;
    // Perception output is independent of planner activation. Keep exporting
    // known contours while in a forward gear or while a target is unavailable,
    // so an inactive scan does not silently look like an empty environment.
    for (const auto &candidate:candidates)
    {
        if (candidate.width<=0.0||candidate.length<=0.0||candidate.label.empty())
            continue;
        const wchar_t kind=candidate.label[0];
        if (kind!=L'D'&&kind!=L'V'&&kind!=L'P') continue;
        if (mode_decision.mode==stage1a::PlannerMode::tractor_to_target_trailer&&
            verified_target!=candidates.end() && &candidate==&*verified_target) continue;
        live_obstacles.push_back({{candidate.x,candidate.z},candidate.heading,
            candidate.width,candidate.length,narrow_ascii(candidate.label)});
    }
    fixed_path.known_obstacle_count=static_cast<int>(live_obstacles.size());
    if (mode_decision.mode==stage1a::PlannerMode::inactive)
    {
        fixed_path.reason=mode_decision.reason;
    }
    else
    {
        stage1a::PlanRequest request;
        request.mode=mode_decision.mode;
        request.tractor_start={{telemetry.x,telemetry.z},telemetry.heading};
        request.geometry.tractor_wheelbase_m=telemetry.tractor_wheelbase_m;
        request.geometry.tractor_width_m=telemetry.tractor_width_m;
        request.geometry.tractor_length_m=telemetry.tractor_length_m;
        if (mode_decision.mode==stage1a::PlannerMode::tractor_to_target_trailer)
        {
            request.subject_start=request.tractor_start;
            // Stage 0's coupling_approach_heading is the reverse-motion
            // tangent into the kingpin. The tractor's physical-front heading
            // at coupling is therefore the opposite direction.
            request.target={verified_target->kingpin,
                stage0::wrap_angle(verified_target->approach+stage0::pi)};
        }
        else
        {
            const double lt=!telemetry.trailer_axle_to_hitch_m.empty()
                ?telemetry.trailer_axle_to_hitch_m.front()
                :std::clamp(attached_target->length*0.65,2.5,14.0);
            const double rear_overhang=lt<4.2
                ?std::clamp(lt*0.28,0.7,1.2)
                :std::clamp(lt*0.42,2.3,3.4);
            const double center_from_axle=(lt-rear_overhang)*0.5;
            const auto attached_forward=stage1a::forward(attached_target->heading);
            const auto parking_forward=stage1a::forward(parking_target->heading);
            const stage0::Vec2 attached_axle{
                attached_target->x-attached_forward.x*center_from_axle,
                attached_target->z-attached_forward.y*center_from_axle};
            const stage0::Vec2 parking_axle{
                parking_target->x-parking_forward.x*center_from_axle,
                parking_target->z-parking_forward.y*center_from_axle};
            request.subject_start={attached_axle,attached_target->heading};
            request.target={parking_axle,parking_target->heading};
            perceived_parking_target=request.target;
            request.geometry.trailer_axle_to_hitch_m=lt;
            request.geometry.trailer_width_m=attached_target->width;
            request.geometry.trailer_length_m=attached_target->length;
            request.geometry.trailer_rear_overhang_m=rear_overhang;
            const auto current_hitch=stage1a::add(attached_axle,
                stage1a::multiply(attached_forward,lt));
            const auto tractor_to_hitch=stage1a::subtract(current_hitch,
                                                          request.tractor_start.position);
            request.geometry.tractor_reference_to_hitch_m=std::clamp(
                stage1a::dot(tractor_to_hitch,
                             stage1a::forward(request.tractor_start.heading_rad)),
                -8.0,8.0);
            request.geometry.tractor_reference_to_hitch_lateral_m=std::clamp(
                stage1a::dot(tractor_to_hitch,
                             stage1a::right(request.tractor_start.heading_rad)),
                -0.75,0.75);
        }
        request.obstacles=live_obstacles;
        fixed_path=stage1a::plan(request);
    }

    std::cout << "Stage1AFixedPathPlanner:\n"
              << "  PlannerMode=" << stage1a::mode_name(fixed_path.mode) << "\n"
              << "  Activation=" << (planner_active?"ACTIVE":"INACTIVE") << "\n"
              << "  VehicleState=" << (telemetry.attached_count==0
                    ?"TRACTOR_ONLY":"TRACTOR_WITH_ATTACHED_TRAILER") << "\n"
              << "  TargetType="
              << (fixed_path.mode==stage1a::PlannerMode::tractor_to_target_trailer
                    ?"TARGET_TRAILER_KINGPIN":
                 fixed_path.mode==stage1a::PlannerMode::trailer_to_parking_target
                    ?"TASK_PARKING_K0":"NONE") << "\n"
              << "  TargetConfidence="
              << (fixed_path.mode==stage1a::PlannerMode::tractor_to_target_trailer
                    ?"IDENTITY_VERIFIED_KINGPIN_HEURISTIC":
                 fixed_path.mode==stage1a::PlannerMode::trailer_to_parking_target
                    ?"ROOT_HEADING_VERIFIED_CENTER_CALIBRATED":"NONE") << "\n";
    if (fixed_path.found)
    {
        const auto &start=fixed_path.samples.front();
        const auto &goal=fixed_path.samples.back();
        const auto start_position=fixed_path.mode==stage1a::PlannerMode::tractor_to_target_trailer
            ?start.tractor_position:start.trailer_position;
        const auto goal_position=fixed_path.mode==stage1a::PlannerMode::tractor_to_target_trailer
            ?goal.tractor_position:goal.trailer_position;
        const double start_heading=fixed_path.mode==stage1a::PlannerMode::tractor_to_target_trailer
            ?start.tractor_heading:start.trailer_heading;
        const double goal_heading=fixed_path.mode==stage1a::PlannerMode::tractor_to_target_trailer
            ?goal.tractor_heading:goal.trailer_heading;
        std::cout << "  StartPosition=(" << start_position.x << ',' << start_position.y << ")\n"
                  << "  StartHeading=" << start_heading << "\n"
                  << "  TargetPosition=(" << goal_position.x << ',' << goal_position.y << ")\n"
                  << "  TargetHeading=" << goal_heading << "\n"
                  << "  CandidateCount=" << fixed_path.candidate_count << "\n"
                  << "  ValidCandidateCount=" << fixed_path.valid_candidate_count << "\n"
                  << "  KnownObstacleCount=" << fixed_path.known_obstacle_count << "\n"
                  << "  ObstacleCollisionRejections=" << fixed_path.obstacle_collision_rejections << "\n"
                  << "  SelfCollisionRejections=" << fixed_path.self_collision_rejections << "\n"
                  << "  StartStateRejections=" << fixed_path.start_state_rejections << "\n"
                  << "  CurvatureRejections=" << fixed_path.curvature_rejections << "\n"
                  << "  ArticulationRejections=" << fixed_path.articulation_rejections << "\n"
                  << "  ForwardMotionRejections=" << fixed_path.forward_motion_rejections << "\n"
                  << "  OtherRejections=" << fixed_path.other_rejections << "\n"
                  << "  BestPathLength=" << fixed_path.path_length << "m\n"
                  << "  MaxArticulation=" << fixed_path.max_articulation*180.0/stage0::pi << "deg\n"
                  << "  MeanArticulation=" << fixed_path.mean_articulation*180.0/stage0::pi << "deg\n"
                  << "  InitialTractorPositionError="
                  << fixed_path.initial_tractor_position_error << "m\n"
                  << "  InitialTractorHeadingError="
                  << fixed_path.initial_tractor_heading_error*180.0/stage0::pi
                  << "deg\n"
                  << "  MaxCurvature=" << fixed_path.max_curvature << "m^-1\n"
                  << "  CurvatureChange=" << fixed_path.curvature_change << "m^-1\n"
                  << "  FinalPositionError=" << fixed_path.final_position_error << "m\n"
                  << "  FinalHeadingError=" << fixed_path.final_heading_error*180.0/stage0::pi << "deg\n"
                  << "  CollisionClaim=KNOWN_CONTOURS_CLEAR_IN_INCOMPLETE_MAP\n"
                  << "  Result=KINEMATIC_PATH_FOUND\n";
    }
    else
    {
        std::cout << "  CandidateCount=" << fixed_path.candidate_count << "\n"
                  << "  ValidCandidateCount=" << fixed_path.valid_candidate_count << "\n"
                  << "  KnownObstacleCount=" << fixed_path.known_obstacle_count << "\n"
                  << "  ObstacleCollisionRejections=" << fixed_path.obstacle_collision_rejections << "\n"
                  << "  SelfCollisionRejections=" << fixed_path.self_collision_rejections << "\n"
                  << "  StartStateRejections=" << fixed_path.start_state_rejections << "\n"
                  << "  CurvatureRejections=" << fixed_path.curvature_rejections << "\n"
                  << "  ArticulationRejections=" << fixed_path.articulation_rejections << "\n"
                  << "  ForwardMotionRejections=" << fixed_path.forward_motion_rejections << "\n"
                  << "  OtherRejections=" << fixed_path.other_rejections << "\n"
                  << "  Result=NO_VALID_PATH\n"
                  << "  reason=" << fixed_path.reason << "\n";
    }
    // These files are also the live in-game runtime's last verified target
    // seed. A diagnostic scan that cannot uniquely see the task frame must
    // still produce its PNG, but must not erase a previously verified target.
    // A uniquely identified task frame remains a valid runtime target even
    // when the trailer is already inside it and no reverse curve is needed.
    // Persist that target independently from path feasibility, otherwise the
    // previous detached-trailer seed survives the attach transition.
    const bool target_only_seed=!fixed_path.found&&
        fixed_path.mode==stage1a::PlannerMode::trailer_to_parking_target&&
        perceived_parking_target.has_value();
    const bool seed_updated=fixed_path.found||target_only_seed;
    const bool diagnostics_written=seed_updated&&write_planner_diagnostics(
        fixed_path,planner_active,telemetry.attached_count);
    const bool path_csv_written=fixed_path.found
        ?write_path_csv(fixed_path)
        :(target_only_seed&&write_parking_target_seed_csv(
            *perceived_parking_target));
    const bool rear_tracks_csv_written=fixed_path.found&&
        write_tractor_rear_tracks_csv(fixed_path,telemetry);
    const bool obstacle_csv_written=seed_updated&&
        write_obstacles_csv(live_obstacles);
    std::cout << "  DiagnosticsFile="
              << (diagnostics_written?"build/stage1a_planner_diagnostics.txt":
                  seed_updated?"FAILED":"PRESERVED_NO_VALID_PATH")
              << "\n  PathDataCsv="
              << (path_csv_written?"build/stage1a_best_path.csv":
                  seed_updated?"FAILED":"PRESERVED_NO_VALID_PATH") << "\n";
    std::cout << "  ObstacleDataCsv="
              << (obstacle_csv_written?"build/stage1a_known_obstacles.csv":
                  seed_updated?"FAILED":"PRESERVED_NO_VALID_PATH") << "\n";
    std::cout << "  TractorRearTracksCsv="
              << (rear_tracks_csv_written
                    ?"build/stage1a_tractor_rear_tracks.csv":
                    target_only_seed?"NOT_REQUIRED_TARGET_ONLY":
                    seed_updated?"FAILED":"PRESERVED_NO_VALID_PATH") << "\n";
    if (attached_player_trailers==1)
        std::cout << "Result: ATTACHED_TRAILER_VERIFIED\n";
    else if (selected_target_index>=0 && selected_target_index<nearby)
        std::cout << "TargetSelection:\n  index=" << selected_target_index
                  << "\n  source=NATIVE_LOADING_GUIDE_OWNER"
                  << "\n  confidence=VERIFIED"
                  << "\n  visibleGuideOwnerCount=" << native_guide_owner_count
                  << "\nResult: TARGET_TRAILER_VERIFIED\n";
    else if (nearby == 0)
        std::cout << "Result: NO_TARGET\n";
    else
        std::cout << "Result: TARGET_TRAILER_AMBIGUOUS\n";
    std::cout << "SnapshotPng: " << (render_png(telemetry,candidates,selected_target_index,
                                                 fixed_path)
        ? "build/stage1a_fixed_path.png" : "FAILED") << "\n";
    return 0;
}
