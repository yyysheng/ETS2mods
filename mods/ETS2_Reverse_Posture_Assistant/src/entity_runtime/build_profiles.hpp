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
    std::array<HookSpec, 4> enabled_hooks;
    std::array<EngineHelperSpec, 4> engine_helpers;
};

// ETS2 1.62.3.2 Steam x64, revision f8b10df2da6f.
constexpr std::array verified_162_3_2_engine_helpers{
    EngineHelperSpec{EngineHelperId::model_transfer, "model_transfer",
                     0x00345f00,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                      0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48},
                     16},
    EngineHelperSpec{EngineHelperId::model_parameter_init,
                     "model_parameter_init", 0x00426a60,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                      0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83},
                     16},
    EngineHelperSpec{EngineHelperId::set_parent, "set_parent", 0x01402130,
                     {0x48, 0x83, 0xec, 0x28, 0x4c, 0x8b, 0xc1, 0x4c,
                      0x8b, 0xca, 0x48, 0x8b, 0x49, 0x40, 0x48, 0x3b},
                     16},
    EngineHelperSpec{EngineHelperId::set_transform, "set_transform",
                     0x014021f0,
                     {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                      0xec, 0x40, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0xfa},
                     16},
};

constexpr BuildProfile current_build{
    "ets2-1.62.3.2-steam-public-f8b10df2da6f",
    "1.62.3.2",
    "6690F7ADEE3EB1701C798DCA812F180B6737B83B43F4D2E48722C42F64979812",
    std::array{
        HookSpec{HookId::model_load, "model_load", 0x0160f230,
                 {0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xec,
                  0x40, 0x80, 0xbc, 0x24, 0x80, 0x00, 0x00, 0x00}},
        HookSpec{HookId::vehicle_render_dispatch,
                 "vehicle_render_dispatch", 0x00784140,
                 {0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83,
                  0xec, 0x20, 0x48, 0x8b, 0x42, 0x18, 0x48, 0x8b}},
        HookSpec{HookId::trailer_visual_update,
                 "trailer_visual_update", 0x0062c5a0,
                 {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x70, 0x10, 0x48,
                  0x89, 0x78, 0x18, 0x55, 0x48, 0x8d, 0x68, 0xa1}},
        HookSpec{HookId::trailer_render, "trailer_render", 0x0062cd10,
                 {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                  0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30, 0x49}},
    },
    verified_162_3_2_engine_helpers,
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
    if (executable_sha256 != current_build.executable_sha256 ||
        !enabled_hook_signatures_match(current_build, reader, failed_index))
        return nullptr;
    if (exact_hash_match) *exact_hash_match = true;
    return &current_build;
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
