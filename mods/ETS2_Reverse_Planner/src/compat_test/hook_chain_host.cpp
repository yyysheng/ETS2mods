#include <windows.h>

#include <iostream>
#include <string>

extern "C" __declspec(dllexport) __declspec(noinline)
int __fastcall compat_target(int value)
{
    volatile int runtime_bias = 7;
    return value + runtime_bias;
}

namespace
{
using Target = int(__fastcall *)(int);
using Install = int(__stdcall *)(void *, int);
using Deactivate = void(__stdcall *)();
using Calls = unsigned(__stdcall *)();

struct Participant
{
    HMODULE module{};
    Install install{};
    Deactivate deactivate{};
    Calls calls{};
};

Participant load(const wchar_t *path)
{
    Participant result{};
    result.module = LoadLibraryW(path);
    if (!result.module) return result;
    result.install = reinterpret_cast<Install>(
        GetProcAddress(result.module, "install_hook"));
    result.deactivate = reinterpret_cast<Deactivate>(
        GetProcAddress(result.module, "deactivate_hook"));
    result.calls = reinterpret_cast<Calls>(
        GetProcAddress(result.module, "hook_calls"));
    return result;
}

bool valid(const Participant &participant)
{
    return participant.module && participant.install &&
           participant.deactivate && participant.calls;
}
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2 || (std::wstring(argv[1]) != L"legacy-first" &&
                      std::wstring(argv[1]) != L"planner-first"))
        return 2;

    auto *target = reinterpret_cast<Target>(
        GetProcAddress(GetModuleHandleW(nullptr), "compat_target"));
    if (!target || target(5) != 12) return 3;

    const bool legacy_first = std::wstring(argv[1]) == L"legacy-first";
    Participant first = load(legacy_first ? L"LegacyHookClient.dll"
                                          : L"PlannerHookClient.dll");
    Participant second = load(legacy_first ? L"PlannerHookClient.dll"
                                           : L"LegacyHookClient.dll");
    if (!valid(first) || !valid(second)) return 4;

    const int first_delta = legacy_first ? 100 : 1000;
    const int second_delta = legacy_first ? 1000 : 100;
    const int first_status = first.install(reinterpret_cast<void *>(target),
                                           first_delta);
    const int second_status = second.install(reinterpret_cast<void *>(target),
                                             second_delta);
    if (first_status != 0 || second_status != 0)
    {
        std::cerr << "install first=" << first_status
                  << " second=" << second_status << '\n';
        return 5;
    }

    const int both = target(5);
    const unsigned first_calls = first.calls();
    const unsigned second_calls = second.calls();
    second.deactivate();
    const int first_only = target(5);
    first.deactivate();
    const int dormant = target(5);

    const bool passed = both == 1112 && first_only == 12 + first_delta &&
                        dormant == 12 && first_calls == 1 &&
                        second_calls == 1;
    std::cout << "order=" << (legacy_first ? "legacy-first" : "planner-first")
              << " both=" << both << " first_only=" << first_only
              << " dormant=" << dormant << " calls=" << first_calls
              << ',' << second_calls << " result="
              << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 6;
}
