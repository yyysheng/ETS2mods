#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#pragma pack(push, 1)
struct Quat { float w{}, x{}, y{}, z{}; };
struct PrismTransform {
    float x{}, y{}, z{};
    std::int16_t sector_x{}, sector_z{};
    Quat rot{};
};
#pragma pack(pop)

struct Sample {
    double seconds{};
    float speed{};
    int gear{};
    int attached{};
    double truck_x{}, truck_y{}, truck_z{}, truck_heading_turns{};
    std::uint8_t guide_visible{};
    std::uintptr_t trailer_f68{};
    PrismTransform guide{};
    float hook_x{}, hook_y{}, hook_z{};
    double trailer_x{}, trailer_y{}, trailer_z{}, trailer_heading_turns{};
};

static DWORD game_pid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{sizeof(e)};
    DWORD result = 0;
    if (Process32FirstW(snap, &e)) do {
        if (_wcsicmp(e.szExeFile, L"eurotrucks2.exe") == 0) { result = e.th32ProcessID; break; }
    } while (Process32NextW(snap, &e));
    CloseHandle(snap);
    return result;
}

template<class T> static bool rpm(HANDLE process, std::uintptr_t address, T &value) {
    SIZE_T read{};
    return ReadProcessMemory(process, reinterpret_cast<const void *>(address),
                             &value, sizeof(value), &read) && read == sizeof(value);
}

static int attached_count(const std::uint8_t *view) {
    int count = 0;
    for (int i = 0; i < 10; ++i) if (*(view + 6000 + i * 1560 + 80)) ++count;
    return count;
}

static void write_header(std::ofstream &out) {
    out << "seconds,speed,gear,attached,guide_visible,trailer_f68,"
           "truck_x,truck_y,truck_z,truck_heading_turns,"
           "guide_x,guide_y,guide_z,guide_sector_x,guide_sector_z,"
           "hook_x,hook_y,hook_z,trailer_x,trailer_y,trailer_z,trailer_heading_turns\n";
}

static void write_sample(std::ofstream &out, const Sample &s) {
    out << std::fixed << std::setprecision(6)
        << s.seconds << ',' << s.speed << ',' << s.gear << ',' << s.attached << ','
        << static_cast<int>(s.guide_visible) << ",0x" << std::hex << s.trailer_f68 << std::dec << ','
        << s.truck_x << ',' << s.truck_y << ',' << s.truck_z << ',' << s.truck_heading_turns << ','
        << s.guide.x << ',' << s.guide.y << ',' << s.guide.z << ','
        << s.guide.sector_x << ',' << s.guide.sector_z << ','
        << s.hook_x << ',' << s.hook_y << ',' << s.hook_z << ','
        << s.trailer_x << ',' << s.trailer_y << ',' << s.trailer_z << ','
        << s.trailer_heading_turns << '\n';
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 4) {
        std::cerr << "usage: CouplingTransitionMonitor <physicsTrailerHex> <guideModelHex> <output.csv>\n";
        return 2;
    }
    const auto physics = static_cast<std::uintptr_t>(std::stoull(argv[1], nullptr, 16));
    const auto model = static_cast<std::uintptr_t>(std::stoull(argv[2], nullptr, 16));
    const DWORD pid = game_pid();
    if (!pid) { std::cerr << "game not found\n"; return 3; }
    HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SCSTelemetry");
    if (!process || !mapping) { std::cerr << "open failed\n"; return 4; }
    auto *view = static_cast<const std::uint8_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 32768));
    if (!view) { std::cerr << "map failed\n"; return 5; }
    std::ofstream out(argv[3]);
    write_header(out);
    std::deque<Sample> before;
    const auto start = std::chrono::steady_clock::now();
    bool transition = false;
    auto transition_time = start;
    int previous_attached = attached_count(view);
    std::cout << "READY initialAttached=" << previous_attached << std::endl;
    while (std::chrono::steady_clock::now() - start < std::chrono::minutes(15)) {
        const auto now = std::chrono::steady_clock::now();
        Sample s{};
        s.seconds = std::chrono::duration<double>(now - start).count();
        std::memcpy(&s.gear, view + 508, sizeof(s.gear));
        std::memcpy(&s.speed, view + 948, sizeof(s.speed));
        std::memcpy(&s.truck_x, view + 2200, sizeof(s.truck_x));
        std::memcpy(&s.truck_y, view + 2208, sizeof(s.truck_y));
        std::memcpy(&s.truck_z, view + 2216, sizeof(s.truck_z));
        std::memcpy(&s.truck_heading_turns, view + 2224, sizeof(s.truck_heading_turns));
        s.attached = attached_count(view);
        rpm(process, physics + 0x1120, s.guide_visible);
        rpm(process, physics + 0x0f68, s.trailer_f68);
        rpm(process, model + 0x10, s.guide);
        if (s.attached > 0) {
            const auto base = 6000;
            std::memcpy(&s.hook_x, view + base + 664, sizeof(s.hook_x));
            std::memcpy(&s.hook_y, view + base + 668, sizeof(s.hook_y));
            std::memcpy(&s.hook_z, view + base + 672, sizeof(s.hook_z));
            std::memcpy(&s.trailer_x, view + base + 872, sizeof(s.trailer_x));
            std::memcpy(&s.trailer_y, view + base + 880, sizeof(s.trailer_y));
            std::memcpy(&s.trailer_z, view + base + 888, sizeof(s.trailer_z));
            std::memcpy(&s.trailer_heading_turns, view + base + 896, sizeof(s.trailer_heading_turns));
        }
        if (!transition) {
            before.push_back(s);
            while (before.size() > 300) before.pop_front();
            if (previous_attached == 0 && s.attached > 0) {
                transition = true;
                transition_time = now;
                for (const auto &old : before) write_sample(out, old);
                std::cout << "CAPTURE transitionAt=" << s.seconds << std::endl;
            }
        } else {
            write_sample(out, s);
            if (now - transition_time >= std::chrono::seconds(3)) break;
        }
        previous_attached = s.attached;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    out.flush();
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(process);
    std::cout << (transition ? "COMPLETE" : "TIMEOUT") << std::endl;
    return transition ? 0 : 6;
}
