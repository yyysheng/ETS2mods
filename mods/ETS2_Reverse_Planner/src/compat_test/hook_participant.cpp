#include <windows.h>

#include <atomic>

#include <MinHook.h>

namespace
{
using Target = int(__fastcall *)(int);

Target original_target = nullptr;
std::atomic<bool> active{};
std::atomic<unsigned> calls{};
int contribution = 0;

int __fastcall hooked_target(int value)
{
    calls.fetch_add(1, std::memory_order_relaxed);
    const int result = original_target(value);
    return active.load(std::memory_order_acquire)
        ? result + contribution
        : result;
}
}

extern "C" __declspec(dllexport) int __stdcall install_hook(void *target,
                                                             int delta)
{
    if (!target) return -100;
    contribution = delta;
    const auto initialize = MH_Initialize();
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
        return static_cast<int>(initialize);
    const auto create = MH_CreateHook(target,
                                      reinterpret_cast<void *>(&hooked_target),
                                      reinterpret_cast<void **>(&original_target));
    if (create != MH_OK) return static_cast<int>(create);
    const auto enable = MH_EnableHook(target);
    if (enable != MH_OK) return static_cast<int>(enable);

    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&install_hook), &pinned))
        return -101;
    active.store(true, std::memory_order_release);
    return 0;
}

extern "C" __declspec(dllexport) void __stdcall deactivate_hook()
{
    active.store(false, std::memory_order_release);
}

extern "C" __declspec(dllexport) unsigned __stdcall hook_calls()
{
    return calls.load(std::memory_order_relaxed);
}
