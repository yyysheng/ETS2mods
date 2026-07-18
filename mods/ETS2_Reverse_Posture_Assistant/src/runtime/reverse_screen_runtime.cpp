#include <reshade.hpp>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr int kWidth = 1021;
constexpr int kHeight = 577;
constexpr double kPi = 3.14159265358979323846;

reshade::api::resource g_path_target = { 0 };
HANDLE g_mapping = nullptr;
const std::byte *g_telemetry = nullptr;
HANDLE g_environment_mapping = nullptr;
const std::byte *g_environment = nullptr;
HMODULE g_module = nullptr;
bool g_environment_started = false;
ULONGLONG g_environment_start_attempt = 0;
ULONGLONG g_environment_sequence_tick = 0;
uint32_t g_environment_sequence = 0;
std::vector<uint32_t> g_pixels(kWidth * kHeight);
uint64_t g_frame = 0;
bool g_logged_update = false;

struct Pose { double x, y, heading; };
struct Trailer { double heading, length, rear_overhang, width; };
struct Point { double x, y; };
struct EnvironmentSegment { float x1, y1, x2, y2, width; uint32_t color, kind; };
struct View { double scale = 25.0, x = kWidth * 0.5, y = 68.0; };
View g_view;

template <typename T>
T read_value(size_t offset)
{
    T value{};
    if (g_telemetry != nullptr)
        std::memcpy(&value, g_telemetry + offset, sizeof(T));
    return value;
}

double normalize_angle(double value)
{
    while (value > kPi) value -= kPi * 2.0;
    while (value < -kPi) value += kPi * 2.0;
    return value;
}

bool open_telemetry()
{
    if (g_telemetry != nullptr) return true;
    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SCSTelemetry");
    if (g_mapping == nullptr) return false;
    g_telemetry = static_cast<const std::byte *>(MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, 32 * 1024));
    if (g_telemetry == nullptr) { CloseHandle(g_mapping); g_mapping = nullptr; }
    return g_telemetry != nullptr;
}

void close_telemetry()
{
    if (g_telemetry != nullptr) UnmapViewOfFile(g_telemetry);
    if (g_mapping != nullptr) CloseHandle(g_mapping);
    g_telemetry = nullptr;
    g_mapping = nullptr;
}

bool open_environment()
{
    if (g_environment != nullptr) return true;
    g_environment_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\ETS2ReverseEnvironment");
    if (g_environment_mapping == nullptr) return false;
    g_environment = static_cast<const std::byte *>(MapViewOfFile(g_environment_mapping, FILE_MAP_READ, 0, 0, 0));
    if (g_environment == nullptr) { CloseHandle(g_environment_mapping); g_environment_mapping = nullptr; }
    return g_environment != nullptr;
}

void close_environment()
{
    if (g_environment != nullptr) UnmapViewOfFile(g_environment);
    if (g_environment_mapping != nullptr) CloseHandle(g_environment_mapping);
    g_environment = nullptr;
    g_environment_mapping = nullptr;
}

void start_environment_service()
{
    if (g_module == nullptr) return;
    const ULONGLONG now = GetTickCount64();
    if (g_environment_started && g_environment != nullptr) return;
    if (now - g_environment_start_attempt < 5000) return;
    g_environment_started = false;
    g_environment_start_attempt = now;

    wchar_t module_path[MAX_PATH]{};
    if (GetModuleFileNameW(g_module, module_path, MAX_PATH) == 0) return;
    std::wstring directory(module_path);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    directory.resize(slash);
    const std::wstring executable = directory + L"\\ETS2ReverseEnvironment.exe";

    std::wstring game_directory = directory;
    for (int i = 0; i < 2; ++i)
    {
        const size_t parent = game_directory.find_last_of(L"\\/");
        if (parent == std::wstring::npos) return;
        game_directory.resize(parent);
    }
    std::wstring command = L"\"" + executable + L"\" \"" + game_directory + L"\" " +
        std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION process{};
    if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
    {
        g_environment_started = true;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        reshade::log::message(reshade::log::level::info, "ETS2 terrain environment service started.");
    }
}

double truck_wheelbase()
{
    const int count = std::clamp(read_value<int32_t>(80), 0, 16);
    double front = 0, rear = 0;
    int front_count = 0, rear_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const double z = read_value<float>(1804 + i * 4);
        if (read_value<uint8_t>(1500 + i) != 0) { front += z; ++front_count; }
        else { rear += z; ++rear_count; }
    }
    if (front_count == 0 || rear_count == 0) return 3.8;
    const double result = std::abs(front / front_count - rear / rear_count);
    return result >= 2.4 && result <= 6.8 ? result : 3.8;
}

std::vector<Trailer> read_trailers(double truck_heading)
{
    std::vector<Trailer> result;
    const int count = std::clamp(read_value<int32_t>(92), 0, 10);
    for (int i = 0; i < count; ++i)
    {
        const size_t base = 6000 + i * 1560;
        if (read_value<uint8_t>(base + 80) == 0) continue;
        const double heading = read_value<double>(base + 896) * kPi * 2.0;
        const int wheel_count = std::clamp(read_value<int32_t>(base + 148), 0, 16);
        double length = 7.2;
        double width = 2.55;
        if (wheel_count > 0)
        {
            const double hook_z = read_value<float>(base + 672);
            double wheel_z = 0;
            double min_x = std::numeric_limits<double>::max();
            double max_x = std::numeric_limits<double>::lowest();
            for (int wheel = 0; wheel < wheel_count; ++wheel)
            {
                wheel_z += read_value<float>(base + 804 + wheel * 4);
                const double wheel_x = read_value<float>(base + 676 + wheel * 4);
                min_x = std::min(min_x, wheel_x);
                max_x = std::max(max_x, wheel_x);
            }
            const double estimate = std::abs(wheel_z / wheel_count - hook_z);
            if (estimate >= 2.5 && estimate <= 14.0) length = estimate;
            const double width_estimate = max_x - min_x + 0.55;
            if (width_estimate >= 1.8 && width_estimate <= 2.65) width = width_estimate;
        }
        const double rear_overhang = length < 4.2 ? std::clamp(length * 0.28, 0.7, 1.2)
                                                  : std::clamp(length * 0.42, 2.3, 3.4);
        result.push_back({ normalize_angle(heading - truck_heading), length, rear_overhang, width });
    }
    return result;
}

void put_pixel(int x, int y, uint32_t color)
{
    if (x >= 0 && x < kWidth && y >= 0 && y < kHeight)
        g_pixels[static_cast<size_t>(y) * kWidth + x] = color;
}

void blend_pixel(int x, int y, uint32_t color, uint8_t alpha)
{
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
    uint32_t &destination = g_pixels[static_cast<size_t>(y) * kWidth + x];
    const uint32_t destination_alpha = destination >> 24;
    const uint32_t inverse = 255u - alpha;
    const uint32_t output_alpha = alpha + destination_alpha * inverse / 255u;
    if (output_alpha == 0) return;
    const auto channel = [&](int shift) {
        const uint32_t source_value = (color >> shift) & 0xFFu;
        const uint32_t destination_value = (destination >> shift) & 0xFFu;
        return ((source_value * alpha + destination_value * destination_alpha * inverse / 255u) / output_alpha) << shift;
    };
    destination = (output_alpha << 24) | channel(16) | channel(8) | channel(0);
}

void line(int x0, int y0, int x1, int y1, uint32_t color, int radius)
{
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;)
    {
        for (int oy = -radius; oy <= radius; ++oy)
            for (int ox = -radius; ox <= radius; ++ox)
                if (ox * ox + oy * oy <= radius * radius) put_pixel(x0 + ox, y0 + oy, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

bool clip_line_to_screen(int &x0, int &y0, int &x1, int &y1)
{
    constexpr int left = 1, right = 2, top = 4, bottom = 8;
    const auto code = [](int x, int y) {
        int result = 0;
        if (x < 0) result |= left; else if (x >= kWidth) result |= right;
        if (y < 0) result |= top; else if (y >= kHeight) result |= bottom;
        return result;
    };

    int code0 = code(x0, y0), code1 = code(x1, y1);
    while (true)
    {
        if ((code0 | code1) == 0) return true;
        if ((code0 & code1) != 0) return false;
        const int outside = code0 != 0 ? code0 : code1;
        double x = 0, y = 0;
        if (outside & top)
        {
            if (y1 == y0) return false;
            y = 0; x = x0 + (x1 - x0) * (0.0 - y0) / (y1 - y0);
        }
        else if (outside & bottom)
        {
            if (y1 == y0) return false;
            y = kHeight - 1; x = x0 + (x1 - x0) * (static_cast<double>(kHeight - 1) - y0) / (y1 - y0);
        }
        else if (outside & right)
        {
            if (x1 == x0) return false;
            x = kWidth - 1; y = y0 + (y1 - y0) * (static_cast<double>(kWidth - 1) - x0) / (x1 - x0);
        }
        else
        {
            if (x1 == x0) return false;
            x = 0; y = y0 + (y1 - y0) * (0.0 - x0) / (x1 - x0);
        }
        if (outside == code0)
        {
            x0 = static_cast<int>(std::lround(x)); y0 = static_cast<int>(std::lround(y)); code0 = code(x0, y0);
        }
        else
        {
            x1 = static_cast<int>(std::lround(x)); y1 = static_cast<int>(std::lround(y)); code1 = code(x1, y1);
        }
    }
}

void rect(int left, int top, int right, int bottom, uint32_t color)
{
    left = std::max(left, 0); top = std::max(top, 0);
    right = std::min(right, kWidth); bottom = std::min(bottom, kHeight);
    for (int y = top; y < bottom; ++y)
        std::fill(g_pixels.begin() + static_cast<size_t>(y) * kWidth + left,
                  g_pixels.begin() + static_cast<size_t>(y) * kWidth + right, color);
}

std::pair<int, int> screen(double x, double y)
{
    return { static_cast<int>(std::lround(g_view.x + x * g_view.scale)),
             static_cast<int>(std::lround(g_view.y + y * g_view.scale)) };
}

std::array<Point, 4> trailer_corners(const Pose &axle, const Trailer &trailer)
{
    const Point direction{ std::sin(axle.heading), std::cos(axle.heading) };
    const Point lateral{ std::cos(axle.heading) * trailer.width * 0.5,
                         -std::sin(axle.heading) * trailer.width * 0.5 };
    const Point front{ axle.x - direction.x * trailer.length, axle.y - direction.y * trailer.length };
    const Point rear{ axle.x + direction.x * trailer.rear_overhang, axle.y + direction.y * trailer.rear_overhang };
    return { Point{front.x + lateral.x, front.y + lateral.y},
             Point{front.x - lateral.x, front.y - lateral.y},
             Point{rear.x - lateral.x, rear.y - lateral.y},
             Point{rear.x + lateral.x, rear.y + lateral.y} };
}

bool point_in_quad(double x, double y, const std::array<std::pair<int, int>, 4> &p)
{
    bool inside = false;
    for (int i = 0, j = 3; i < 4; j = i++)
    {
        const double yi = p[i].second, yj = p[j].second;
        if ((yi > y) != (yj > y) && x < (p[j].first - p[i].first) * (y - yi) / (yj - yi) + p[i].first)
            inside = !inside;
    }
    return inside;
}

void filled_quad(const std::array<Point, 4> &points, uint32_t color, uint8_t alpha)
{
    std::array<std::pair<int, int>, 4> p;
    for (size_t i = 0; i < p.size(); ++i) p[i] = screen(points[i].x, points[i].y);
    int left = kWidth - 1, right = 0, top = kHeight - 1, bottom = 0;
    for (const auto &point : p)
    {
        left = std::min(left, point.first); right = std::max(right, point.first);
        top = std::min(top, point.second); bottom = std::max(bottom, point.second);
    }
    left = std::max(left, 0); right = std::min(right, kWidth - 1);
    top = std::max(top, 0); bottom = std::min(bottom, kHeight - 1);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            if (point_in_quad(x + 0.5, y + 0.5, p)) blend_pixel(x, y, color, alpha);
}

void outline_quad(const std::array<Point, 4> &points, uint32_t color, int thickness)
{
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto a = screen(points[i].x, points[i].y);
        const auto b = screen(points[(i + 1) % points.size()].x, points[(i + 1) % points.size()].y);
        line(a.first, a.second, b.first, b.second, color, thickness);
    }
}

void fit_view(const std::vector<Pose> &tractor, const std::vector<std::vector<Pose>> &paths,
              const std::vector<Trailer> &trailers)
{
    double min_x = -1.5, max_x = 1.5, min_y = -2.8, max_y = 1.0;
    auto include = [&](double x, double y) {
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    };
    for (const Pose &pose : tractor)
        for (double side : { -1.35, 1.35 }) include(pose.x + std::cos(pose.heading) * side, pose.y - std::sin(pose.heading) * side);
    for (size_t i = 0; i < paths.size(); ++i)
        for (const Pose &pose : paths[i])
            for (const Point &point : trailer_corners(pose, trailers[i])) include(point.x, point.y);
    constexpr double left = 28, right = kWidth - 28, top = 62, bottom = kHeight - 22;
    const double span_x = std::max(max_x - min_x, 1.0), span_y = std::max(max_y - min_y, 1.0);
    g_view.scale = std::clamp(std::min((right - left) / span_x, (bottom - top) / span_y), 14.0, 46.0);
    g_view.x = left + ((right - left) - span_x * g_view.scale) * 0.5 - min_x * g_view.scale;
    g_view.y = top + ((bottom - top) - span_y * g_view.scale) * 0.5 - min_y * g_view.scale;
}

void draw_environment()
{
    if (!open_environment()) return;
    constexpr uint32_t magic = 0x45545345;
    constexpr size_t header_size = 64;
    constexpr size_t segment_size = 28;
    if (read_value<uint8_t>(0) == 0) return;

    uint32_t environment_magic{};
    uint32_t sequence_before{};
    int32_t count{};
    std::memcpy(&environment_magic, g_environment, sizeof(environment_magic));
    std::memcpy(&sequence_before, g_environment + 8, sizeof(sequence_before));
    std::memcpy(&count, g_environment + 12, sizeof(count));
    if (environment_magic != magic || sequence_before == 0 || count < 0 || count > 12000) return;

    const ULONGLONG now = GetTickCount64();
    if (sequence_before != g_environment_sequence)
    {
        g_environment_sequence = sequence_before;
        g_environment_sequence_tick = now;
    }
    else if (g_environment_sequence_tick != 0 && now - g_environment_sequence_tick > 5000)
    {
        close_environment();
        g_environment_started = false;
        g_environment_sequence = 0;
        g_environment_sequence_tick = 0;
        reshade::log::message(reshade::log::level::warning,
            "ETS2 terrain environment heartbeat stopped; reconnecting service.");
        return;
    }

    for (int32_t i = 0; i < count; ++i)
    {
        EnvironmentSegment segment{};
        std::memcpy(&segment, g_environment + header_size + static_cast<size_t>(i) * segment_size, sizeof(segment));
        if (!std::isfinite(segment.x1) || !std::isfinite(segment.y1) ||
            !std::isfinite(segment.x2) || !std::isfinite(segment.y2)) continue;
        const auto a = screen(segment.x1, segment.y1);
        const auto b = screen(segment.x2, segment.y2);
        int x0 = a.first, y0 = a.second, x1 = b.first, y1 = b.second;
        if (!clip_line_to_screen(x0, y0, x1, y1)) continue;
        const int radius = segment.kind == 3 ? 1 : 2;
        const uint32_t color = segment.kind >= 10 ? 0xFFE06A4F :
                               segment.kind == 1 ? 0xFF426875 :
                               segment.kind == 2 ? 0xFF587681 : 0xFF3D6959;
        line(x0, y0, x1, y1, color, radius);
    }

    uint32_t sequence_after{};
    std::memcpy(&sequence_after, g_environment + 8, sizeof(sequence_after));
    if (sequence_before != sequence_after)
        reshade::log::message(reshade::log::level::debug, "ETS2 terrain layer refreshed during a frame; next frame will be consistent.");
}

void path_edges(const std::vector<Pose> &poses, double width, uint32_t color, int thickness)
{
    for (size_t i = 1; i < poses.size(); ++i)
    {
        for (double side : { -width * 0.5, width * 0.5 })
        {
            const Pose &a = poses[i - 1], &b = poses[i];
            const auto pa = screen(a.x + std::cos(a.heading) * side, a.y - std::sin(a.heading) * side);
            const auto pb = screen(b.x + std::cos(b.heading) * side, b.y - std::sin(b.heading) * side);
            line(pa.first, pa.second, pb.first, pb.second, color, thickness);
        }
    }
}

void draw_vehicle(const std::vector<Pose> &trailer_axles, const std::vector<Trailer> &trailers,
                  const std::vector<uint32_t> &colors)
{
    for (size_t index = trailers.size(); index-- > 0;)
    {
        const auto body = trailer_corners(trailer_axles[index], trailers[index]);
        filled_quad(body, 0xFF405260, 72);
        outline_quad(body, colors[index], 2);
        const auto rear_left = screen(body[2].x, body[2].y);
        const auto rear_right = screen(body[3].x, body[3].y);
        line(rear_left.first, rear_left.second, rear_right.first, rear_right.second, colors[index], 4);
    }
    const std::array<Point, 4> cab{ Point{-1.25, 0.1}, Point{1.25, 0.1}, Point{1.25, -2.7}, Point{-1.25, -2.7} };
    filled_quad(cab, 0xFFE3E8EC, 72);
    outline_quad(cab, 0xFFEDF3F6, 2);
    const std::array<Point, 4> windshield{ Point{-0.9, -1.8}, Point{0.9, -1.8}, Point{0.9, -2.35}, Point{-0.9, -2.35} };
    filled_quad(windshield, 0xFF39758D, 96);
}

void render_frame()
{
    std::fill(g_pixels.begin(), g_pixels.end(), 0xFF0C1014);

    const double truck_heading = read_value<double>(2224) * kPi * 2.0;
    const auto trailers = read_trailers(truck_heading);
    const double steering = std::clamp(static_cast<double>(read_value<float>(972)), -1.0, 1.0);
    const double curvature = std::tan(steering * 38.0 * kPi / 180.0) / truck_wheelbase();
    constexpr int samples = 41;
    constexpr double step = 0.20;

    std::vector<Pose> tractor{ { 0, 0, 0 } };
    std::vector<std::vector<Pose>> trailer_paths(trailers.size());
    std::vector<Pose> trailer_axles;
    double hitch_x = 0, hitch_y = 0;
    for (size_t i = 0; i < trailers.size(); ++i)
    {
        hitch_x += std::sin(trailers[i].heading) * trailers[i].length;
        hitch_y += std::cos(trailers[i].heading) * trailers[i].length;
        trailer_axles.push_back({ hitch_x, hitch_y, trailers[i].heading });
        trailer_paths[i].push_back(trailer_axles.back());
    }
    const std::vector<Pose> current_trailer_axles = trailer_axles;

    double x = 0, y = 0, heading = 0;
    for (int sample = 1; sample < samples; ++sample)
    {
        heading -= curvature * step;
        x += std::sin(heading) * step;
        y += std::cos(heading) * step;
        tractor.push_back({ x, y, heading });
        hitch_x = x; hitch_y = y;
        double parent_heading = heading;
        double parent_distance = step;
        bool articulation_limit = false;
        for (size_t i = 0; i < trailers.size(); ++i)
        {
            double trailer_heading = trailer_axles[i].heading;
            const double articulation = normalize_angle(parent_heading - trailer_heading);
            if (std::abs(articulation) > 72.0 * kPi / 180.0)
            {
                articulation_limit = true;
                break;
            }
            trailer_heading = normalize_angle(trailer_heading - std::sin(articulation) * parent_distance / trailers[i].length);
            const double axle_x = hitch_x + std::sin(trailer_heading) * trailers[i].length;
            const double axle_y = hitch_y + std::cos(trailer_heading) * trailers[i].length;
            trailer_axles[i] = { axle_x, axle_y, trailer_heading };
            trailer_paths[i].push_back(trailer_axles[i]);
            hitch_x = axle_x; hitch_y = axle_y;
            parent_distance *= std::cos(articulation);
            parent_heading = trailer_heading;
        }
        if (articulation_limit)
        {
            tractor.pop_back();
            for (auto &path : trailer_paths)
                if (path.size() > tractor.size()) path.pop_back();
            break;
        }
    }

    fit_view(tractor, trailer_paths, trailers);

    draw_environment();

    for (int gx = 0; gx < kWidth; gx += 48)
        line(gx, 0, gx, kHeight - 1, 0xFF182129, 0);
    for (int gy = 0; gy < kHeight; gy += 48)
        line(0, gy, kWidth - 1, gy, 0xFF182129, 0);

    std::vector<uint32_t> trailer_colors;
    trailer_colors.reserve(trailers.size());
    for (size_t i = 0; i < trailer_paths.size(); ++i)
    {
        const double angle = std::abs(trailers[i].heading) * 180.0 / kPi;
        const uint32_t color = angle > 42.0 ? 0xFFE74C3C : (angle > 32.0 ? 0xFFFF9F2D : 0xFFFFD23F);
        trailer_colors.push_back(color);

        for (size_t sample = 6; sample < trailer_paths[i].size(); sample += 7)
        {
            const auto ghost = trailer_corners(trailer_paths[i][sample], trailers[i]);
            filled_quad(ghost, color, 19);
            outline_quad(ghost, 0xFF53616C, 0);
        }

        std::vector<Pose> rear_path;
        rear_path.reserve(trailer_paths[i].size());
        for (const Pose &axle : trailer_paths[i])
        {
            const double rear_x = axle.x + std::sin(axle.heading) * trailers[i].rear_overhang;
            const double rear_y = axle.y + std::cos(axle.heading) * trailers[i].rear_overhang;
            rear_path.push_back({ rear_x, rear_y, axle.heading });
        }
        for (size_t sample = 1; sample < rear_path.size(); ++sample)
        {
            const Pose &a = rear_path[sample - 1], &b = rear_path[sample];
            const std::array<Point, 4> swept{
                Point{a.x + std::cos(a.heading) * trailers[i].width * 0.5, a.y - std::sin(a.heading) * trailers[i].width * 0.5},
                Point{a.x - std::cos(a.heading) * trailers[i].width * 0.5, a.y + std::sin(a.heading) * trailers[i].width * 0.5},
                Point{b.x - std::cos(b.heading) * trailers[i].width * 0.5, b.y + std::sin(b.heading) * trailers[i].width * 0.5},
                Point{b.x + std::cos(b.heading) * trailers[i].width * 0.5, b.y - std::sin(b.heading) * trailers[i].width * 0.5} };
            filled_quad(swept, color, 22);
        }
        path_edges(rear_path, trailers[i].width, color, 3);
        const auto final_body = trailer_corners(trailer_paths[i].back(), trailers[i]);
        const auto left = screen(final_body[2].x, final_body[2].y);
        const auto right = screen(final_body[3].x, final_body[3].y);
        line(left.first, left.second, right.first, right.second, color, 4);
    }
    path_edges(tractor, 2.5, 0xFF35DC7D, 2);
    draw_vehicle(current_trailer_axles, trailers, trailer_colors);
}

void on_init_resource(reshade::api::device *, const reshade::api::resource_desc &desc,
                      const reshade::api::subresource_data *, reshade::api::resource_usage,
                      reshade::api::resource resource)
{
    if (desc.type == reshade::api::resource_type::texture_2d &&
        desc.texture.width == kWidth && desc.texture.height == kHeight)
    {
        g_path_target = resource;
        reshade::log::message(reshade::log::level::info, "ETS2 reverse screen texture captured (1021x577).");
    }
}

void on_destroy_resource(reshade::api::device *, reshade::api::resource resource)
{
    if (resource.handle == g_path_target.handle) g_path_target = { 0 };
}

void on_present(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain, const reshade::api::rect *,
                const reshade::api::rect *, uint32_t, const reshade::api::rect *)
{
    ++g_frame;
    if (!g_environment_started || g_environment == nullptr) start_environment_service();
    reshade::api::device *device = queue->get_device();
    const bool telemetry_ready = open_telemetry() && read_value<uint8_t>(0) != 0;
    const bool reverse = telemetry_ready && (read_value<int32_t>(504) < 0 || read_value<uint8_t>(1587) != 0);

    if (!reverse || g_path_target.handle == 0 || (g_frame % 3) != 0) return;
    render_frame();
    reshade::api::subresource_data path_data{ g_pixels.data(), kWidth * 4u, kWidth * kHeight * 4u };
    device->update_texture_region(path_data, g_path_target, 0, nullptr);
    if (!g_logged_update)
    {
        g_logged_update = true;
        reshade::log::message(reshade::log::level::info, "ETS2 independent 2D reverse planner is active; camera input is untouched.");
    }
}
}

extern "C" __declspec(dllexport) const char *NAME = "ETS2 Reverse Posture Assistant";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Displays telemetry-based tractor and trailer reverse posture paths on supported original infotainment screens.";

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        if (!reshade::register_addon(module)) return FALSE;
        reshade::register_event<reshade::addon_event::init_resource>(on_init_resource);
        reshade::register_event<reshade::addon_event::destroy_resource>(on_destroy_resource);
        reshade::register_event<reshade::addon_event::present>(on_present);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        close_telemetry();
        close_environment();
        reshade::unregister_addon(module);
    }
    return TRUE;
}
