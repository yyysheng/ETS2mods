#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace reverse_assist::compatibility
{
enum class HookId
{
    model_load,
    vehicle_render_dispatch,
    trailer_visual_update,
    trailer_render,
};

enum class EngineHelperId
{
    model_transfer,
    model_parameter_init,
    set_parent,
    set_transform,
};

struct HookSpec
{
    HookId id;
    std::string_view name;
    std::uintptr_t rva;
    std::array<std::uint8_t, 16> signature;
};

struct EngineHelperSpec
{
    EngineHelperId id;
    std::string_view name;
    std::uintptr_t rva;
    std::array<std::uint8_t, 16> signature;
    std::size_t signature_size;
};

struct BuildProfile
{
    std::string_view id;
    std::string_view game_version;
    std::string_view executable_sha256;
    bool signature_compatible_fallback;
    std::array<HookSpec, 4> enabled_hooks;
    std::array<EngineHelperSpec, 4> engine_helpers;
};

// Installed Steam public 1.61.1.9 build (commit 05126873bfae). The model
// transfer and scene-node helpers are checked in addition to the four hooks:
// they are called by the native model lifecycle after profile selection.
constexpr std::array verified_161_1_9_engine_helpers{
    EngineHelperSpec{EngineHelperId::model_transfer, "model_transfer",
                     0x0033cc00,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                      0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48},
                     16},
    EngineHelperSpec{EngineHelperId::model_parameter_init,
                     "model_parameter_init", 0x0041ccb0,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                      0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83},
                     16},
    EngineHelperSpec{EngineHelperId::set_parent, "set_parent", 0x013ce580,
                     {0x48, 0x83, 0xec, 0x28, 0x4c, 0x8b, 0xc1, 0x4c,
                      0x8b, 0xca, 0x48, 0x8b, 0x49, 0x40, 0x48, 0x3b},
                     16},
    EngineHelperSpec{EngineHelperId::set_transform, "set_transform",
                     0x013ce640,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                      0xec, 0x40, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0xfa},
                     16},
};

constexpr std::array verified_161_1_1_engine_helpers{
    EngineHelperSpec{EngineHelperId::model_transfer, "model_transfer",
                     0x0033cb90,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74}, 8},
    EngineHelperSpec{EngineHelperId::model_parameter_init,
                     "model_parameter_init", 0x0041cc20,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c}, 8},
    EngineHelperSpec{EngineHelperId::set_parent, "set_parent", 0x013cc750,
                     {0x48, 0x83, 0xec, 0x28, 0x4c, 0x8b, 0xc1}, 7},
    EngineHelperSpec{EngineHelperId::set_transform, "set_transform",
                     0x013cc810,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83}, 8},
};

constexpr std::array verified_161_1_0_engine_helpers{
    EngineHelperSpec{EngineHelperId::model_transfer, "model_transfer",
                     0x0033cb90,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74}, 8},
    EngineHelperSpec{EngineHelperId::model_parameter_init,
                     "model_parameter_init", 0x0041cc20,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c}, 8},
    EngineHelperSpec{EngineHelperId::set_parent, "set_parent", 0x013cbca0,
                     {0x48, 0x83, 0xec, 0x28, 0x4c, 0x8b, 0xc1}, 7},
    EngineHelperSpec{EngineHelperId::set_transform, "set_transform",
                     0x013cbd60,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83}, 8},
};

constexpr std::array<EngineHelperSpec, 4> no_engine_helpers{};

constexpr std::array verified_160_1_7_hooks{
    HookSpec{HookId::model_load, "model_load", 0x015184f0,
             {0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xec,
              0x40, 0x80, 0xbc, 0x24, 0x80, 0x00, 0x00, 0x00}},
    HookSpec{HookId::vehicle_render_dispatch, "vehicle_render_dispatch",
             0x00772020,
             {0x40, 0x53, 0x57, 0x48, 0x83, 0xec, 0x28, 0x48,
              0x8b, 0x42, 0x18, 0x48, 0x8b, 0xda, 0x48, 0x8b}},
    HookSpec{HookId::trailer_visual_update, "trailer_visual_update",
             0x00614190,
             {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x70, 0x10, 0x48,
              0x89, 0x78, 0x18, 0x55, 0x48, 0x8d, 0x68, 0xa1}},
    HookSpec{HookId::trailer_render, "trailer_render", 0x006148f0,
             {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
              0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30, 0x49}},
};

// Steam public 1.61.1.1 (6949e633e779). All four sites were identified in
// the installed executable; the vehicle dispatch stack frame changed in 1.61.
constexpr std::array verified_161_1_1_hooks{
    HookSpec{HookId::model_load, "model_load", 0x015d99f0,
             {0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xec,
              0x40, 0x80, 0xbc, 0x24, 0x80, 0x00, 0x00, 0x00}},
    HookSpec{HookId::vehicle_render_dispatch, "vehicle_render_dispatch",
             0x00798720,
             {0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83,
              0xec, 0x20, 0x48, 0x8b, 0x42, 0x18, 0x48, 0x8b}},
    HookSpec{HookId::trailer_visual_update, "trailer_visual_update",
             0x00637620,
             {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x70, 0x10, 0x48,
              0x89, 0x78, 0x18, 0x55, 0x48, 0x8d, 0x68, 0xa1}},
    HookSpec{HookId::trailer_render, "trailer_render", 0x00637d90,
             {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
              0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30, 0x49}},
};

// Steam public 1.61.1.0 (f02fca2efa75). All four sites were identified in
// the installed executable; the vehicle dispatch stack frame changed in 1.61.
constexpr std::array verified_161_1_0_hooks{
    HookSpec{HookId::model_load, "model_load", 0x015d8f40,
             {0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xec,
              0x40, 0x80, 0xbc, 0x24, 0x80, 0x00, 0x00, 0x00}},
    HookSpec{HookId::vehicle_render_dispatch, "vehicle_render_dispatch",
             0x00798570,
             {0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83,
              0xec, 0x20, 0x48, 0x8b, 0x42, 0x18, 0x48, 0x8b}},
    HookSpec{HookId::trailer_visual_update, "trailer_visual_update",
             0x00637500,
             {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x70, 0x10, 0x48,
              0x89, 0x78, 0x18, 0x55, 0x48, 0x8d, 0x68, 0xa1}},
    HookSpec{HookId::trailer_render, "trailer_render", 0x00637c70,
             {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
              0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30, 0x49}},
};

// Exact profiles are listed before the 1.60 signature-compatible fallback.
// The fallback admits another 1.60 binary only when every enabled hook keeps
// the verified RVA and bytes; 1.61 profiles also carry active helper entries.
constexpr std::array build_profiles{
    BuildProfile{
        "ets2-1.61.1.9-steam-public-05126873bfae",
        "1.61.1.9",
        "B70C2357CC56EB5834EE47FEA3C60B2F2D4591383FF2AC479CFD3CD254301B8C",
        false,
        std::array{
            HookSpec{HookId::model_load, "model_load", 0x015db820,
                     {0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xec,
                      0x40, 0x80, 0xbc, 0x24, 0x80, 0x00, 0x00, 0x00}},
            HookSpec{HookId::vehicle_render_dispatch,
                     "vehicle_render_dispatch", 0x00798b30,
                     {0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83,
                      0xec, 0x20, 0x48, 0x8b, 0x42, 0x18, 0x48, 0x8b}},
            HookSpec{HookId::trailer_visual_update,
                     "trailer_visual_update", 0x00637940,
                     {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x70, 0x10, 0x48,
                      0x89, 0x78, 0x18, 0x55, 0x48, 0x8d, 0x68, 0xa1}},
            HookSpec{HookId::trailer_render, "trailer_render", 0x006380b0,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                      0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30, 0x49}},
        },
        verified_161_1_9_engine_helpers,
    },
    BuildProfile{
        "ets2-1.61.1.1-steam-public-6949e633e779",
        "1.61.1.1",
        "2014BFCC850A06108F4CA3C233A5BB6D31D7D83346C5D449F94C8E0675E5199A",
        false,
        verified_161_1_1_hooks,
        verified_161_1_1_engine_helpers,
    },
    BuildProfile{
        "ets2-1.61.1.0-steam-public-f02fca2efa75",
        "1.61.1.0",
        "4DCB548CAAD924254A60AF7C3BD1DB69DCAF42F7ADF19B5BB5D77EBA2814AF21",
        false,
        verified_161_1_0_hooks,
        verified_161_1_0_engine_helpers,
    },
    BuildProfile{
        "ets2-1.60.1.7-steam-public-23966373",
        "1.60.1.7",
        "B7DFFE6B27402C7DB6DFD52CF982CD5BF292584138B35E3EB8EFB311814AB3F8",
        false,
        verified_160_1_7_hooks,
        no_engine_helpers,
    },
    BuildProfile{
        "ets2-1.60-compatible-hook-layout",
        "1.60.x signature-compatible",
        "",
        true,
        verified_160_1_7_hooks,
        no_engine_helpers,
    },
};

template <typename SignatureReader>
bool enabled_hook_signatures_match(const BuildProfile &profile,
                                   SignatureReader &&reader,
                                   std::size_t *failed_index = nullptr)
{
    for (std::size_t index = 0; index < profile.enabled_hooks.size(); ++index)
    {
        const auto &hook = profile.enabled_hooks[index];
        if (!reader(hook.rva, hook.signature.data(), hook.signature.size()))
        {
            if (failed_index) *failed_index = index;
            return false;
        }
    }
    return true;
}

template <typename SignatureReader>
const BuildProfile *select_build_profile(std::string_view executable_sha256,
                                         SignatureReader &&reader,
                                         bool *exact_hash_match = nullptr,
                                         std::size_t *failed_index = nullptr)
{
    if (exact_hash_match) *exact_hash_match = false;
    for (const auto &profile : build_profiles)
    {
        if (profile.executable_sha256.empty() ||
            profile.executable_sha256 != executable_sha256)
            continue;
        if (!enabled_hook_signatures_match(profile, reader, failed_index))
            return nullptr;
        if (exact_hash_match) *exact_hash_match = true;
        return &profile;
    }
    for (const auto &profile : build_profiles)
    {
        if (!profile.signature_compatible_fallback) continue;
        if (enabled_hook_signatures_match(profile, reader, failed_index))
            return &profile;
    }
    return nullptr;
}

constexpr const HookSpec *find_hook(const BuildProfile &profile, HookId id)
{
    for (const auto &hook : profile.enabled_hooks)
        if (hook.id == id) return &hook;
    return nullptr;
}

template <typename SignatureReader>
bool engine_helper_signatures_match(const BuildProfile &profile,
                                    SignatureReader &&reader,
                                    std::size_t *failed_index = nullptr)
{
    for (std::size_t index = 0; index < profile.engine_helpers.size(); ++index)
    {
        const auto &helper = profile.engine_helpers[index];
        if (helper.rva == 0 || helper.signature_size == 0 ||
            helper.signature_size > helper.signature.size() ||
            !reader(helper.rva, helper.signature.data(), helper.signature_size))
        {
            if (failed_index) *failed_index = index;
            return false;
        }
    }
    return true;
}

constexpr const EngineHelperSpec *find_engine_helper(const BuildProfile &profile,
                                                      EngineHelperId id)
{
    for (const auto &helper : profile.engine_helpers)
        if (helper.id == id) return &helper;
    return nullptr;
}
}
