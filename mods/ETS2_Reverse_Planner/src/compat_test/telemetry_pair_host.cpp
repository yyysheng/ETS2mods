#include <windows.h>

#include <scssdk_telemetry.h>

#include <iostream>
#include <string>

namespace
{
unsigned channel_registrations = 0;
unsigned event_registrations = 0;

void SCSAPIFUNC mock_log(const scs_log_type_t, const scs_string_t message)
{
    if (message) std::cout << "log=" << message << '\n';
}

scs_result_t SCSAPIFUNC mock_register_event(
    const scs_event_t, const scs_telemetry_event_callback_t,
    const scs_context_t)
{
    ++event_registrations;
    return SCS_RESULT_ok;
}

scs_result_t SCSAPIFUNC mock_unregister_event(const scs_event_t)
{
    return SCS_RESULT_ok;
}

scs_result_t SCSAPIFUNC mock_register_channel(
    const scs_string_t, const scs_u32_t, const scs_value_type_t,
    const scs_u32_t, const scs_telemetry_channel_callback_t,
    const scs_context_t)
{
    ++channel_registrations;
    return SCS_RESULT_ok;
}

scs_result_t SCSAPIFUNC mock_unregister_channel(
    const scs_string_t, const scs_u32_t, const scs_value_type_t)
{
    return SCS_RESULT_ok;
}

using Init = scs_result_t(SCSAPIFUNC *)(
    scs_u32_t, const scs_telemetry_init_params_t *);
using Shutdown = void(SCSAPIFUNC *)();

struct Plugin
{
    HMODULE module{};
    Init init{};
    Shutdown shutdown{};
};

Plugin load(const wchar_t *path)
{
    Plugin plugin{};
    plugin.module = LoadLibraryW(path);
    if (!plugin.module) return plugin;
    plugin.init = reinterpret_cast<Init>(
        GetProcAddress(plugin.module, "scs_telemetry_init"));
    plugin.shutdown = reinterpret_cast<Shutdown>(
        GetProcAddress(plugin.module, "scs_telemetry_shutdown"));
    return plugin;
}
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4) return 2;
    const bool reverse_shutdown = std::wstring(argv[3]) == L"reverse";
    if (!reverse_shutdown && std::wstring(argv[3]) != L"forward") return 3;

    Plugin first = load(argv[1]);
    Plugin second = load(argv[2]);
    if (!first.module || !first.init || !first.shutdown ||
        !second.module || !second.init || !second.shutdown)
        return 4;

    scs_telemetry_init_params_v101_t params{};
    params.common.game_name = "Offline compatibility host";
    params.common.game_id = "eut2";
    params.common.game_version = 0;
    params.common.log = mock_log;
    params.register_for_event = mock_register_event;
    params.unregister_from_event = mock_unregister_event;
    params.register_for_channel = mock_register_channel;
    params.unregister_from_channel = mock_unregister_channel;

    const auto first_result = first.init(
        SCS_TELEMETRY_VERSION_1_01, &params);
    const auto second_result = second.init(
        SCS_TELEMETRY_VERSION_1_01, &params);
    if (first_result != SCS_RESULT_ok || second_result != SCS_RESULT_ok)
    {
        std::cerr << "init first=" << first_result
                  << " second=" << second_result << '\n';
        return 5;
    }

    if (reverse_shutdown)
    {
        second.shutdown();
        first.shutdown();
    }
    else
    {
        first.shutdown();
        second.shutdown();
    }

    const bool passed = channel_registrations != 0 &&
                        event_registrations == 8;
    std::cout << "actual-pair shutdown="
              << (reverse_shutdown ? "reverse" : "forward")
              << " channels=" << channel_registrations
              << " events=" << event_registrations
              << " result=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 6;
}
