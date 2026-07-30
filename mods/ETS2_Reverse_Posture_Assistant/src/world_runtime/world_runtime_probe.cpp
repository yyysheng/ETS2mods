#include <reshade.hpp>
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "reverse_kinematics.hpp"

namespace
{
using namespace reshade::api;
using reverse_assist::Prediction;
using reverse_assist::TrailerSpec;

HANDLE g_mapping = nullptr;
const std::byte *g_telemetry = nullptr;
HMODULE g_module = nullptr;
uint64_t g_frame = 0;
ULONGLONG g_last_log = 0;
std::mutex g_mutex;
std::atomic_bool g_reverse_active = false;
std::atomic_uint64_t g_guide_draw_calls = 0;
std::atomic_uint64_t g_camera_validation_attempts = 0;
std::atomic_uint64_t g_camera_validation_hits = 0;
thread_local bool g_drawing_guide = false;
constexpr uint64_t kMaxTrackedConstantBufferSize = 4ull * 1024ull * 1024ull;

struct ResourceInfo
{
    uint32_t width = 0;
    uint32_t height = 0;
};

struct PassInfo
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t draw_calls = 0;
    bool has_depth = false;
    bool has_color = false;
    uint64_t color_view = 0;
    uint64_t depth_view = 0;
};

struct GuideVertex
{
    float x, y, z;
    float r, g, b, a;
};

struct Matrix4
{
    float m[4][4]{};
};

struct Renderer
{
    ID3D11Device *device = nullptr;
    ID3D11VertexShader *vs = nullptr;
    ID3D11PixelShader *ps = nullptr;
    ID3D11InputLayout *layout = nullptr;
    ID3D11Buffer *vertices = nullptr;
    ID3D11Buffer *constants = nullptr;
    ID3D11BlendState *blend = nullptr;
    ID3D11DepthStencilState *depth = nullptr;
    ID3D11RasterizerState *raster = nullptr;
    ID3DDeviceContextState *context_state = nullptr;
};

struct PreferredCamera
{
    uint64_t source = 0;
    uint32_t expected_hits = 0;
    uint32_t stable_frames = 0;
};

struct BufferMirror
{
    std::vector<std::byte> bytes;
    const void *mapped_data = nullptr;
    uint64_t mapped_offset = 0;
    uint64_t mapped_size = 0;
    uint64_t updates = 0;
};

struct BoundConstantBuffer
{
    resource buffer = {0};
    uint64_t offset = 0;
    uint64_t size = UINT64_MAX;
};

std::unordered_map<uint64_t, ResourceInfo> g_resources;
std::unordered_map<uint64_t, uint64_t> g_views;
std::unordered_map<command_list *, PassInfo> g_passes;
std::unordered_map<uint64_t, uint64_t> g_viewport_draws;
std::unordered_map<uint64_t, uint32_t> g_camera_capture_counts;
std::unordered_map<uint64_t, BufferMirror> g_buffer_mirrors;
std::unordered_map<command_list *, std::array<BoundConstantBuffer, 16>> g_vs_constant_buffers;
std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>> g_camera_source_counts;
std::unordered_map<uint64_t, PreferredCamera> g_camera_candidates;
std::unordered_map<uint64_t, PreferredCamera> g_preferred_cameras;
std::unordered_set<uint64_t> g_rendered_targets;
std::unordered_map<uint64_t, uint64_t> g_guide_viewport_draws;
std::unordered_map<uint64_t, uint64_t> g_guide_target_draws;
Renderer g_renderer;
uint64_t g_constant_buffer_updates = 0;
uint64_t g_vs_constant_buffer_binds = 0;
uint64_t g_capture_eligible_draws = 0;
uint64_t g_capture_missing_bindings = 0;
uint64_t g_capture_missing_mirrors = 0;
uint64_t g_capture_stale_mirrors = 0;
uint64_t g_capture_range_overflows = 0;
uint64_t g_max_bound_offset = 0;
uint64_t g_max_bound_size = 0;

Prediction build_prediction();

uint64_t viewport_key(uint32_t width, uint32_t height)
{
    return (static_cast<uint64_t>(width) << 32) | height;
}

uint64_t mix_key(uint64_t seed, uint64_t value)
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

bool is_camera_capture_viewport(uint32_t width, uint32_t height)
{
    return (width == 2560 && height == 2880) ||
           (width == 512 && height == 1024) ||
           (width == 512 && height == 512) ||
           (width == 512 && height == 256);
}

std::filesystem::path capture_directory()
{
    wchar_t module_path[MAX_PATH]{};
    if (GetModuleFileNameW(g_module, module_path, MAX_PATH) == 0) return {};
    std::filesystem::path result(module_path);
    result = result.parent_path() / L"ETS2GroundGuideCapture";
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return error ? std::filesystem::path{} : result;
}

void copy_to_mirror(resource dest, uint64_t offset, uint64_t size, const void *data)
{
    if (data == nullptr || size == 0) return;
    std::lock_guard lock(g_mutex);
    const auto it = g_buffer_mirrors.find(dest.handle);
    if (it == g_buffer_mirrors.end() || offset >= it->second.bytes.size()) return;
    const uint64_t copy_size = std::min<uint64_t>(size, it->second.bytes.size() - offset);
    std::memcpy(it->second.bytes.data() + offset, data, static_cast<size_t>(copy_size));
    ++it->second.updates;
    ++g_constant_buffer_updates;
}

void capture_bound_constants(command_list *cmd, uint32_t primitive_count)
{
    if (!g_reverse_active.load(std::memory_order_relaxed) || primitive_count < 500) return;

    uint32_t width = 0, height = 0;
    uint32_t sample = 0;
    struct Payload
    {
        uint32_t slot = 0;
        uint64_t handle = 0;
        uint64_t offset = 0;
        std::vector<std::byte> bytes;
    };
    std::vector<Payload> payloads;
    {
        std::lock_guard lock(g_mutex);
        const auto pass_it = g_passes.find(cmd);
        if (pass_it == g_passes.end() || !pass_it->second.has_depth) return;
        width = pass_it->second.width;
        height = pass_it->second.height;
        if (!is_camera_capture_viewport(width, height)) return;
        uint32_t &count = g_camera_capture_counts[viewport_key(width, height)];
        if (count >= 3) return;

        ++g_capture_eligible_draws;

        const auto bound_it = g_vs_constant_buffers.find(cmd);
        if (bound_it == g_vs_constant_buffers.end())
        {
            ++g_capture_missing_bindings;
            return;
        }
        for (uint32_t slot = 0; slot < bound_it->second.size(); ++slot)
        {
            const BoundConstantBuffer &bound = bound_it->second[slot];
            if (bound.buffer.handle == 0) continue;
            g_max_bound_offset = std::max(g_max_bound_offset, bound.offset);
            if (bound.size != UINT64_MAX)
                g_max_bound_size = std::max(g_max_bound_size, bound.size);
            const auto mirror_it = g_buffer_mirrors.find(bound.buffer.handle);
            if (mirror_it == g_buffer_mirrors.end())
            {
                ++g_capture_missing_mirrors;
                continue;
            }
            if (mirror_it->second.updates == 0)
            {
                ++g_capture_stale_mirrors;
                continue;
            }
            if (bound.offset >= mirror_it->second.bytes.size())
            {
                ++g_capture_range_overflows;
                continue;
            }
            const uint64_t available = mirror_it->second.bytes.size() - bound.offset;
            const uint64_t byte_count = bound.size == UINT64_MAX ? available : std::min(bound.size, available);
            if (byte_count < 64 || byte_count > 65536) continue;
            Payload payload;
            payload.slot = slot;
            payload.handle = bound.buffer.handle;
            payload.offset = bound.offset;
            payload.bytes.assign(mirror_it->second.bytes.begin() + static_cast<size_t>(bound.offset),
                                 mirror_it->second.bytes.begin() + static_cast<size_t>(bound.offset + byte_count));
            payloads.push_back(std::move(payload));
        }
        if (payloads.empty()) return;
        sample = count++;
    }

    const std::filesystem::path directory = capture_directory();
    if (directory.empty()) return;
    std::ofstream manifest(directory / "manifest.txt", std::ios::app);
    manifest << width << 'x' << height << " sample=" << sample
             << " primitive_count=" << primitive_count << " buffers=" << payloads.size() << '\n';
    for (const Payload &payload : payloads)
    {
        const std::wstring filename = std::to_wstring(width) + L"x" + std::to_wstring(height) +
            L"_sample" + std::to_wstring(sample) + L"_slot" + std::to_wstring(payload.slot) +
            L"_handle" + std::to_wstring(payload.handle) + L"_offset" + std::to_wstring(payload.offset) +
            L"_bytes" + std::to_wstring(payload.bytes.size()) + L".bin";
        std::ofstream output(directory / filename, std::ios::binary);
        output.write(reinterpret_cast<const char *>(payload.bytes.data()),
                     static_cast<std::streamsize>(payload.bytes.size()));
    }

    const std::string message = "ETS2 camera constants captured: " + std::to_string(width) + "x" +
        std::to_string(height) + " sample=" + std::to_string(sample);
    reshade::log::message(reshade::log::level::info, message.c_str());
}

template <typename T>
void release_com(T *&object)
{
    if (object != nullptr) object->Release();
    object = nullptr;
}

void destroy_renderer()
{
    release_com(g_renderer.context_state);
    release_com(g_renderer.raster);
    release_com(g_renderer.depth);
    release_com(g_renderer.blend);
    release_com(g_renderer.constants);
    release_com(g_renderer.vertices);
    release_com(g_renderer.layout);
    release_com(g_renderer.ps);
    release_com(g_renderer.vs);
    release_com(g_renderer.device);
}

bool create_renderer(ID3D11Device *device)
{
    if (g_renderer.device == device && g_renderer.vs != nullptr) return true;
    destroy_renderer();
    if (device == nullptr) return false;
    device->AddRef();
    g_renderer.device = device;

    ID3D11Device1 *device1 = nullptr;
    HRESULT result = device->QueryInterface(__uuidof(ID3D11Device1),
                                             reinterpret_cast<void **>(&device1));
    if (SUCCEEDED(result))
    {
        static constexpr D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL selected_level = D3D_FEATURE_LEVEL_11_0;
        result = device1->CreateDeviceContextState(
            0, feature_levels, static_cast<UINT>(std::size(feature_levels)),
            D3D11_SDK_VERSION, __uuidof(ID3D11Device), &selected_level,
            &g_renderer.context_state);
    }
    release_com(device1);
    if (FAILED(result))
    {
        destroy_renderer();
        return false;
    }

    static constexpr char shader_source[] = R"(
cbuffer GuideConstants : register(b0)
{
    row_major float4x4 view_projection;
};
struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
};
struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(view_projection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}
float4 PSMain(PSInput input) : SV_Target
{
    return input.color;
}
)";

    ID3DBlob *vs_blob = nullptr, *ps_blob = nullptr, *errors = nullptr;
    result = D3DCompile(shader_source, sizeof(shader_source), nullptr, nullptr, nullptr,
                                "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                &vs_blob, &errors);
    if (FAILED(result))
    {
        if (errors != nullptr)
            reshade::log::message(reshade::log::level::error,
                                  static_cast<const char *>(errors->GetBufferPointer()));
        release_com(errors);
        release_com(vs_blob);
        destroy_renderer();
        return false;
    }
    release_com(errors);
    result = D3DCompile(shader_source, sizeof(shader_source), nullptr, nullptr, nullptr,
                        "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        &ps_blob, &errors);
    if (FAILED(result))
    {
        if (errors != nullptr)
            reshade::log::message(reshade::log::level::error,
                                  static_cast<const char *>(errors->GetBufferPointer()));
        release_com(errors);
        release_com(vs_blob);
        release_com(ps_blob);
        destroy_renderer();
        return false;
    }
    release_com(errors);

    result = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
                                         nullptr, &g_renderer.vs);
    if (SUCCEEDED(result))
        result = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(),
                                           nullptr, &g_renderer.ps);
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    if (SUCCEEDED(result))
        result = device->CreateInputLayout(elements, static_cast<UINT>(std::size(elements)),
                                            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
                                            &g_renderer.layout);
    release_com(vs_blob);
    release_com(ps_blob);

    D3D11_BUFFER_DESC vertex_desc{};
    vertex_desc.ByteWidth = static_cast<UINT>(sizeof(GuideVertex) * 4096);
    vertex_desc.Usage = D3D11_USAGE_DYNAMIC;
    vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(result))
        result = device->CreateBuffer(&vertex_desc, nullptr, &g_renderer.vertices);

    D3D11_BUFFER_DESC constant_desc{};
    constant_desc.ByteWidth = sizeof(Matrix4);
    constant_desc.Usage = D3D11_USAGE_DYNAMIC;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constant_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(result))
        result = device->CreateBuffer(&constant_desc, nullptr, &g_renderer.constants);

    D3D11_BLEND_DESC blend_desc{};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(result))
        result = device->CreateBlendState(&blend_desc, &g_renderer.blend);

    D3D11_DEPTH_STENCIL_DESC depth_desc{};
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth_desc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    if (SUCCEEDED(result))
        result = device->CreateDepthStencilState(&depth_desc, &g_renderer.depth);

    D3D11_RASTERIZER_DESC raster_desc{};
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.DepthClipEnable = TRUE;
    raster_desc.MultisampleEnable = TRUE;
    raster_desc.AntialiasedLineEnable = TRUE;
    if (SUCCEEDED(result))
        result = device->CreateRasterizerState(&raster_desc, &g_renderer.raster);

    if (FAILED(result))
    {
        destroy_renderer();
        return false;
    }
    reshade::log::message(reshade::log::level::info,
                          "ETS2 reverse ground guide world renderer initialized.");
    return true;
}

bool invert_matrix(const Matrix4 &input, Matrix4 &output)
{
    double work[4][8]{};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col) work[row][col] = input.m[row][col];
        work[row][row + 4] = 1.0;
    }
    for (int col = 0; col < 4; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::abs(work[row][col]) > std::abs(work[pivot][col])) pivot = row;
        if (std::abs(work[pivot][col]) < 1e-9) return false;
        if (pivot != col)
            for (int i = 0; i < 8; ++i) std::swap(work[pivot][i], work[col][i]);
        const double scale = work[col][col];
        for (int i = 0; i < 8; ++i) work[col][i] /= scale;
        for (int row = 0; row < 4; ++row)
        {
            if (row == col) continue;
            const double factor = work[row][col];
            for (int i = 0; i < 8; ++i) work[row][i] -= factor * work[col][i];
        }
    }
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            output.m[row][col] = static_cast<float>(work[row][col + 4]);
    return true;
}

Matrix4 multiply_matrix(const Matrix4 &left, const Matrix4 &right)
{
    Matrix4 result{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            for (int i = 0; i < 4; ++i)
                result.m[row][col] += left.m[row][i] * right.m[i][col];
    return result;
}

bool camera_matrices_valid(const Matrix4 &view, const Matrix4 &view_projection)
{
    ++g_camera_validation_attempts;
    Matrix4 inverse{};
    if (!invert_matrix(view, inverse)) return false;
    const Matrix4 projection = multiply_matrix(view_projection, inverse);
    for (const auto &row : view.m)
        for (float value : row)
            if (!std::isfinite(value)) return false;
    const float camera_x = inverse.m[0][3];
    const float camera_y = inverse.m[1][3];
    const float camera_z = inverse.m[2][3];
    const float distance = std::sqrt(camera_x * camera_x + camera_z * camera_z);
    const bool affine_view = std::abs(view.m[3][0]) < 0.02f &&
                             std::abs(view.m[3][1]) < 0.02f &&
                             std::abs(view.m[3][2]) < 0.02f &&
                             std::abs(view.m[3][3] - 1.0f) < 0.02f;
    const bool perspective = projection.m[0][0] > 0.15f && projection.m[0][0] < 8.0f &&
                             projection.m[1][1] > 0.15f && projection.m[1][1] < 8.0f &&
                             std::abs(std::abs(projection.m[3][2]) - 1.0f) < 0.08f &&
                             std::abs(projection.m[3][3]) < 0.08f;
    const bool valid = affine_view && perspective && std::isfinite(camera_y) &&
                       std::abs(camera_y) < 65.0f && distance < 65.0f;
    if (valid) ++g_camera_validation_hits;
    return valid;
}

uint64_t hash_camera(const Matrix4 &matrix, uint32_t width, uint32_t height)
{
    uint64_t hash = 1469598103934665603ull;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&matrix);
    for (size_t i = 0; i < sizeof(matrix); ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    hash ^= viewport_key(width, height);
    return hash;
}

bool find_camera(command_list *cmd, Matrix4 &view_projection, uint64_t &camera_source)
{
    uint32_t width = 0, height = 0;
    std::array<BoundConstantBuffer, 16> bound{};
    {
        std::lock_guard lock(g_mutex);
        const auto pass_it = g_passes.find(cmd);
        const auto bound_it = g_vs_constant_buffers.find(cmd);
        if (pass_it == g_passes.end() || bound_it == g_vs_constant_buffers.end() ||
            !pass_it->second.has_color || !pass_it->second.has_depth ||
            pass_it->second.width < 256 || pass_it->second.height < 192)
            return false;
        width = pass_it->second.width;
        height = pass_it->second.height;
        bound = bound_it->second;
    }

    for (size_t slot = 0; slot < bound.size(); ++slot)
    {
        const BoundConstantBuffer &range = bound[slot];
        if (range.buffer.handle == 0) continue;
        Matrix4 view{};
        {
            std::lock_guard lock(g_mutex);
            const auto mirror_it = g_buffer_mirrors.find(range.buffer.handle);
            if (mirror_it == g_buffer_mirrors.end() || mirror_it->second.updates == 0 ||
                range.offset + sizeof(Matrix4) * 2 > mirror_it->second.bytes.size())
                continue;
            std::memcpy(&view, mirror_it->second.bytes.data() + range.offset, sizeof(Matrix4));
            std::memcpy(&view_projection,
                        mirror_it->second.bytes.data() + range.offset + sizeof(Matrix4),
                        sizeof(Matrix4));
        }
        if (!camera_matrices_valid(view, view_projection)) continue;
        // Resource handles can rotate between frames even when the game keeps
        // using the same camera constant-buffer binding.
        camera_source = mix_key(static_cast<uint64_t>(slot), range.offset);
        return true;
    }
    return false;
}

std::vector<GuideVertex> make_vertices(const Prediction &prediction)
{
    std::vector<GuideVertex> vertices;
    vertices.reserve(prediction.boxes.size() * 24);
    constexpr float ground_height = 0.15f;
    constexpr float line_half_width = 0.065f;
    const auto append = [&](float x, float z, float r, float g, float b, float a) {
        vertices.push_back({x, ground_height, z, r, g, b, a});
    };
    for (const reverse_assist::GroundBox &box : prediction.boxes)
    {
        const float half_width = static_cast<float>(box.width * 0.5);
        const float half_length = static_cast<float>(box.length * 0.5);
        const float sine = static_cast<float>(std::sin(box.pose.heading));
        const float cosine = static_cast<float>(std::cos(box.pose.heading));
        std::array<std::array<float, 2>, 4> points{};
        const std::array<std::array<float, 2>, 4> local = {{
            {-half_width, -half_length}, {half_width, -half_length},
            {half_width, half_length}, {-half_width, half_length}
        }};
        for (size_t i = 0; i < points.size(); ++i)
        {
            points[i][0] = static_cast<float>(box.pose.x) + cosine * local[i][0] + sine * local[i][1];
            points[i][1] = static_cast<float>(box.pose.z) - sine * local[i][0] + cosine * local[i][1];
        }
        const float alpha = 0.96f;
        const bool trailer = box.kind == reverse_assist::BodyKind::trailer;
        const float r = trailer ? 1.0f : 0.16f;
        const float g = trailer ? 0.12f : 1.0f;
        const float b = trailer ? 0.02f : 0.95f;
        for (size_t edge = 0; edge < 4; ++edge)
        {
            const auto &a = points[edge];
            const auto &bpoint = points[(edge + 1) % 4];
            const float dx = bpoint[0] - a[0];
            const float dz = bpoint[1] - a[1];
            const float inverse_length = 1.0f / std::max(std::sqrt(dx * dx + dz * dz), 0.001f);
            const float ox = -dz * inverse_length * line_half_width;
            const float oz = dx * inverse_length * line_half_width;
            append(a[0] + ox, a[1] + oz, r, g, b, alpha);
            append(a[0] - ox, a[1] - oz, r, g, b, alpha);
            append(bpoint[0] + ox, bpoint[1] + oz, r, g, b, alpha);
            append(bpoint[0] + ox, bpoint[1] + oz, r, g, b, alpha);
            append(a[0] - ox, a[1] - oz, r, g, b, alpha);
            append(bpoint[0] - ox, bpoint[1] - oz, r, g, b, alpha);
        }
    }
    return vertices;
}

void render_prediction(command_list *cmd, const Matrix4 &view_projection)
{
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(cmd->get_native());
    if (context == nullptr) return;
    ID3D11Device *device = nullptr;
    context->GetDevice(&device);
    if (!create_renderer(device))
    {
        release_com(device);
        return;
    }
    release_com(device);

    const std::vector<GuideVertex> vertices = make_vertices(build_prediction());
    if (vertices.empty() || vertices.size() > 4096) return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(g_renderer.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(GuideVertex));
    context->Unmap(g_renderer.vertices, 0);
    if (FAILED(context->Map(g_renderer.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, &view_projection, sizeof(view_projection));
    context->Unmap(g_renderer.constants, 0);

    ID3D11DeviceContext1 *context1 = nullptr;
    if (FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),
                                       reinterpret_cast<void **>(&context1))))
        return;

    ID3D11RenderTargetView *render_target = nullptr;
    ID3D11DepthStencilView *depth_target = nullptr;
    context->OMGetRenderTargets(1, &render_target, &depth_target);
    UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
    context->RSGetViewports(&viewport_count, viewports.data());
    if (render_target == nullptr || viewport_count == 0)
    {
        release_com(depth_target);
        release_com(render_target);
        release_com(context1);
        return;
    }

    ID3DDeviceContextState *previous_state = nullptr;
    context1->SwapDeviceContextState(g_renderer.context_state, &previous_state);

    const UINT stride = sizeof(GuideVertex), offset = 0;
    const FLOAT blend_factor[4]{};
    context->OMSetRenderTargets(1, &render_target, depth_target);
    context->RSSetViewports(viewport_count, viewports.data());
    context->IASetInputLayout(g_renderer.layout);
    context->IASetVertexBuffers(0, 1, &g_renderer.vertices, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(g_renderer.vs, nullptr, 0);
    context->PSSetShader(g_renderer.ps, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &g_renderer.constants);
    context->OMSetBlendState(g_renderer.blend, blend_factor, UINT_MAX);
    context->OMSetDepthStencilState(g_renderer.depth, 0);
    context->RSSetState(g_renderer.raster);
    g_drawing_guide = true;
    context->Draw(static_cast<UINT>(vertices.size()), 0);
    ++g_guide_draw_calls;

    ID3DDeviceContextState *guide_state = nullptr;
    context1->SwapDeviceContextState(previous_state, &guide_state);
    g_drawing_guide = false;
    release_com(g_renderer.context_state);
    g_renderer.context_state = guide_state;
    release_com(previous_state);
    release_com(depth_target);
    release_com(render_target);
    release_com(context1);
}

void maybe_render_prediction(command_list *cmd, uint32_t primitive_count)
{
    if (!g_reverse_active.load(std::memory_order_relaxed) || primitive_count < 300) return;
    Matrix4 view_projection{};
    uint64_t camera_source = 0;
    if (!find_camera(cmd, view_projection, camera_source)) return;

    bool should_render = false;
    uint64_t viewport = 0;
    uint64_t target_size = 0;
    {
        std::lock_guard lock(g_mutex);
        PassInfo &pass = g_passes[cmd];
        if (!pass.has_color || !pass.has_depth ||
            !is_camera_capture_viewport(pass.width, pass.height)) return;

        // Texture-view handles are transient in ETS2. Group camera targets by
        // their stable render dimensions and still draw at most once per group.
        const uint64_t target_key = viewport_key(pass.width, pass.height);
        const uint32_t hits = ++g_camera_source_counts[target_key][camera_source];
        const auto preferred = g_preferred_cameras.find(target_key);
        if (preferred != g_preferred_cameras.end() &&
            preferred->second.source == camera_source &&
            g_rendered_targets.find(target_key) == g_rendered_targets.end())
        {
            const uint32_t expected = std::max(preferred->second.expected_hits, 1u);
            const uint32_t threshold = std::max(1u, (expected * 3u) / 4u);
            should_render = hits >= threshold;
            if (should_render) g_rendered_targets.insert(target_key);
        }
        if (should_render)
        {
            viewport = viewport_key(pass.width, pass.height);
            const auto view_it = g_views.find(pass.color_view);
            if (view_it != g_views.end())
            {
                const auto resource_it = g_resources.find(view_it->second);
                if (resource_it != g_resources.end())
                    target_size = viewport_key(resource_it->second.width, resource_it->second.height);
            }
        }
    }
    if (should_render)
    {
        render_prediction(cmd, view_projection);
        std::lock_guard lock(g_mutex);
        ++g_guide_viewport_draws[viewport];
        if (target_size != 0) ++g_guide_target_draws[target_size];
    }
}

template <typename T>
T read_value(size_t offset)
{
    T value{};
    if (g_telemetry != nullptr)
        std::memcpy(&value, g_telemetry + offset, sizeof(T));
    return value;
}

bool open_telemetry()
{
    if (g_telemetry != nullptr) return true;
    g_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\SCSTelemetry");
    if (g_mapping == nullptr) return false;
    g_telemetry = static_cast<const std::byte *>(MapViewOfFile(g_mapping, FILE_MAP_READ, 0, 0, 32 * 1024));
    if (g_telemetry == nullptr)
    {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
    }
    return g_telemetry != nullptr;
}

void close_telemetry()
{
    if (g_telemetry != nullptr) UnmapViewOfFile(g_telemetry);
    if (g_mapping != nullptr) CloseHandle(g_mapping);
    g_telemetry = nullptr;
    g_mapping = nullptr;
}

struct TruckGeometry
{
    double wheelbase = 3.8;
    double length = 6.8;
    double width = 2.5;
    double rear_x = 0.0;
    double rear_z = 0.0;
    double center_from_rear = -1.9;
};

TruckGeometry read_truck_geometry()
{
    TruckGeometry geometry;
    const int count = std::clamp(read_value<int32_t>(80), 0, 16);
    double front_x = 0.0, front_z = 0.0, rear_x = 0.0, rear_z = 0.0;
    double min_x = 1e9, max_x = -1e9;
    int front_count = 0, rear_count = 0;
    for (int i = 0; i < count; ++i)
    {
        const double x = read_value<float>(1740 + i * 4);
        const double z = read_value<float>(1804 + i * 4);
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        if (read_value<uint8_t>(1500 + i) != 0)
        {
            front_x += x;
            front_z += z;
            ++front_count;
        }
        else
        {
            rear_x += x;
            rear_z += z;
            ++rear_count;
        }
    }
    if (front_count == 0 || rear_count == 0) return geometry;
    front_x /= front_count;
    front_z /= front_count;
    rear_x /= rear_count;
    rear_z /= rear_count;
    geometry.wheelbase = std::clamp(std::abs(front_z - rear_z), 2.4, 6.8);
    geometry.length = std::clamp(geometry.wheelbase + 2.25, 5.2, 9.0);
    geometry.width = std::clamp(max_x - min_x + 0.42, 2.25, 2.65);
    geometry.rear_x = rear_x;
    geometry.rear_z = rear_z;
    geometry.center_from_rear = std::clamp((front_z - rear_z) * 0.5, -3.5, -1.2);
    return geometry;
}

std::vector<TrailerSpec> read_trailers(double truck_heading)
{
    std::vector<TrailerSpec> result;
    const int count = std::clamp(read_value<int32_t>(92), 0, 10);
    for (int i = 0; i < count; ++i)
    {
        const size_t base = 6000 + static_cast<size_t>(i) * 1560;
        if (read_value<uint8_t>(base + 80) == 0) continue;
        TrailerSpec trailer;
        trailer.heading = reverse_assist::normalize_angle(read_value<double>(base + 896) *
                                                           reverse_assist::pi * 2.0 - truck_heading);
        const int wheels = std::clamp(read_value<int32_t>(base + 148), 0, 16);
        if (wheels > 0)
        {
            const double hook_z = read_value<float>(base + 672);
            double wheel_z = 0.0;
            double min_x = 1e9, max_x = -1e9;
            for (int wheel = 0; wheel < wheels; ++wheel)
            {
                wheel_z += read_value<float>(base + 804 + wheel * 4);
                const double x = read_value<float>(base + 676 + wheel * 4);
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
            }
            trailer.axle_to_hitch = std::clamp(std::abs(wheel_z / wheels - hook_z), 2.5, 14.0);
            trailer.width = std::clamp(max_x - min_x + 0.55, 1.8, 2.65);
        }
        trailer.rear_overhang = trailer.axle_to_hitch < 4.2 ?
            std::clamp(trailer.axle_to_hitch * 0.28, 0.7, 1.2) :
            std::clamp(trailer.axle_to_hitch * 0.42, 2.3, 3.4);
        result.push_back(trailer);
    }
    return result;
}

Prediction build_prediction()
{
    const double heading = read_value<double>(2224) * reverse_assist::pi * 2.0;
    const double steering = std::clamp(static_cast<double>(read_value<float>(972)), -1.0, 1.0);
    const TruckGeometry truck = read_truck_geometry();
    return reverse_assist::predict_reverse_boxes(steering, truck.wheelbase, truck.length,
                                                  truck.width, read_trailers(heading),
                                                  truck.rear_x, truck.rear_z,
                                                  truck.center_from_rear);
}

void on_init_resource(device *, const resource_desc &desc, const subresource_data *initial_data,
                      resource_usage, resource resource)
{
    std::lock_guard lock(g_mutex);
    if (desc.type == resource_type::texture_2d)
    {
        g_resources[resource.handle] = {desc.texture.width, desc.texture.height};
    }
    else if (desc.type == resource_type::buffer &&
             (desc.usage & resource_usage::constant_buffer) == resource_usage::constant_buffer &&
             desc.buffer.size >= 64 && desc.buffer.size <= kMaxTrackedConstantBufferSize)
    {
        BufferMirror mirror;
        mirror.bytes.resize(static_cast<size_t>(desc.buffer.size));
        if (initial_data != nullptr && initial_data->data != nullptr)
        {
            std::memcpy(mirror.bytes.data(), initial_data->data, mirror.bytes.size());
            mirror.updates = 1;
        }
        g_buffer_mirrors[resource.handle] = std::move(mirror);
    }
}

void on_destroy_resource(device *, resource resource)
{
    std::lock_guard lock(g_mutex);
    g_resources.erase(resource.handle);
    g_buffer_mirrors.erase(resource.handle);
}

void on_map_buffer(device *, resource resource, uint64_t offset, uint64_t size, map_access, void **data)
{
    if (data == nullptr || *data == nullptr) return;
    std::lock_guard lock(g_mutex);
    const auto it = g_buffer_mirrors.find(resource.handle);
    if (it == g_buffer_mirrors.end() || offset >= it->second.bytes.size()) return;
    it->second.mapped_data = *data;
    it->second.mapped_offset = offset;
    it->second.mapped_size = size == UINT64_MAX ? it->second.bytes.size() - offset :
        std::min<uint64_t>(size, it->second.bytes.size() - offset);
}

void on_unmap_buffer(device *, resource resource)
{
    std::lock_guard lock(g_mutex);
    const auto it = g_buffer_mirrors.find(resource.handle);
    if (it == g_buffer_mirrors.end() || it->second.mapped_data == nullptr) return;
    std::memcpy(it->second.bytes.data() + it->second.mapped_offset,
                it->second.mapped_data, static_cast<size_t>(it->second.mapped_size));
    it->second.mapped_data = nullptr;
    it->second.mapped_offset = 0;
    it->second.mapped_size = 0;
    ++it->second.updates;
    ++g_constant_buffer_updates;
}

bool on_update_buffer(device *, const void *data, resource dest, uint64_t offset, uint64_t size)
{
    copy_to_mirror(dest, offset, size, data);
    return false;
}

bool on_update_buffer_command(command_list *, const void *data, resource dest, uint64_t offset, uint64_t size)
{
    copy_to_mirror(dest, offset, size, data);
    return false;
}

void on_push_descriptors(command_list *cmd, shader_stage stages, pipeline_layout, uint32_t,
                         const descriptor_table_update &update)
{
    if ((stages & shader_stage::vertex) != shader_stage::vertex ||
        update.type != descriptor_type::constant_buffer || update.descriptors == nullptr) return;
    const auto *ranges = static_cast<const buffer_range *>(update.descriptors);
    std::vector<resource> unknown;
    unknown.reserve(update.count);
    {
        std::lock_guard lock(g_mutex);
        for (uint32_t i = 0; i < update.count; ++i)
            if (ranges[i].buffer.handle != 0 && !g_buffer_mirrors.contains(ranges[i].buffer.handle))
                unknown.push_back(ranges[i].buffer);
    }
    std::vector<std::pair<uint64_t, uint64_t>> missing;
    missing.reserve(unknown.size());
    for (resource buffer : unknown)
    {
        const resource_desc desc = cmd->get_device()->get_resource_desc(buffer);
        if (desc.type == resource_type::buffer && desc.buffer.size >= 64 &&
            desc.buffer.size <= kMaxTrackedConstantBufferSize)
            missing.emplace_back(buffer.handle, desc.buffer.size);
    }
    std::lock_guard lock(g_mutex);
    for (const auto &[handle, size] : missing)
    {
        if (!g_buffer_mirrors.contains(handle))
        {
            BufferMirror mirror;
            mirror.bytes.resize(static_cast<size_t>(size));
            g_buffer_mirrors.emplace(handle, std::move(mirror));
        }
    }
    auto &bound = g_vs_constant_buffers[cmd];
    for (uint32_t i = 0; i < update.count; ++i)
    {
        const uint32_t slot = update.binding + i;
        if (slot >= bound.size()) break;
        bound[slot] = {ranges[i].buffer, ranges[i].offset, ranges[i].size};
        ++g_vs_constant_buffer_binds;
    }
}

void on_reset_command_list(command_list *cmd)
{
    std::lock_guard lock(g_mutex);
    g_passes.erase(cmd);
    g_vs_constant_buffers.erase(cmd);
}

void on_init_resource_view(device *, resource resource, resource_usage, const resource_view_desc &, resource_view view)
{
    std::lock_guard lock(g_mutex);
    g_views[view.handle] = resource.handle;
}

void on_destroy_resource_view(device *, resource_view view)
{
    std::lock_guard lock(g_mutex);
    g_views.erase(view.handle);
}

void on_bind_viewports(command_list *cmd, uint32_t first, uint32_t count, const viewport *viewports)
{
    if (first != 0 || count == 0 || viewports == nullptr) return;
    std::lock_guard lock(g_mutex);
    PassInfo &pass = g_passes[cmd];
    pass.width = static_cast<uint32_t>(std::max(viewports[0].width, 0.0f));
    pass.height = static_cast<uint32_t>(std::max(viewports[0].height, 0.0f));
}

void on_bind_targets(command_list *cmd, uint32_t count, const resource_view *targets, resource_view depth)
{
    std::lock_guard lock(g_mutex);
    PassInfo &pass = g_passes[cmd];
    pass.has_depth = depth.handle != 0;
    pass.has_color = count != 0 && targets != nullptr && targets[0].handle != 0;
    pass.color_view = pass.has_color ? targets[0].handle : 0;
    pass.depth_view = depth.handle;
}

bool on_draw(command_list *cmd, uint32_t vertex_count, uint32_t, uint32_t, uint32_t)
{
    if (g_drawing_guide) return false;
    maybe_render_prediction(cmd, vertex_count);
    capture_bound_constants(cmd, vertex_count);
    std::lock_guard lock(g_mutex);
    PassInfo &pass = g_passes[cmd];
    ++pass.draw_calls;
    if (pass.has_depth && pass.width >= 64 && pass.height >= 64)
        ++g_viewport_draws[viewport_key(pass.width, pass.height)];
    return false;
}

bool on_draw_indexed(command_list *cmd, uint32_t index_count, uint32_t, uint32_t, int32_t, uint32_t)
{
    if (g_drawing_guide) return false;
    maybe_render_prediction(cmd, index_count);
    capture_bound_constants(cmd, index_count);
    std::lock_guard lock(g_mutex);
    PassInfo &pass = g_passes[cmd];
    ++pass.draw_calls;
    if (pass.has_depth && pass.width >= 64 && pass.height >= 64)
        ++g_viewport_draws[viewport_key(pass.width, pass.height)];
    return false;
}

void on_present(command_queue *queue, swapchain *swapchain, const rect *, const rect *, uint32_t, const rect *)
{
    (void)queue;
    ++g_frame;
    {
        std::lock_guard lock(g_mutex);
        std::unordered_map<uint64_t, PreferredCamera> learned;
        for (const auto &[target, sources] : g_camera_source_counts)
        {
            PreferredCamera best{};
            for (const auto &[source, hits] : sources)
            {
                if (hits > best.expected_hits ||
                    (hits == best.expected_hits && (best.source == 0 || source < best.source)))
                    best = {source, hits, 0};
            }
            if (best.expected_hits == 0) continue;

            PreferredCamera &candidate = g_camera_candidates[target];
            if (candidate.source == best.source)
            {
                candidate.expected_hits = best.expected_hits;
                candidate.stable_frames = std::min(candidate.stable_frames + 1u, 120u);
            }
            else
            {
                candidate = {best.source, best.expected_hits, 1u};
            }
            if (candidate.stable_frames >= 2) learned[target] = candidate;
        }
        g_preferred_cameras = std::move(learned);
        g_camera_source_counts.clear();
        g_rendered_targets.clear();
    }
    if (!open_telemetry() || read_value<uint8_t>(0) == 0)
    {
        g_reverse_active.store(false, std::memory_order_relaxed);
        std::lock_guard lock(g_mutex);
        g_viewport_draws.clear();
        return;
    }
    const bool reverse = read_value<int32_t>(504) < 0 || read_value<uint8_t>(1587) != 0;
    g_reverse_active.store(reverse, std::memory_order_relaxed);
    if (!reverse)
    {
        const ULONGLONG now = GetTickCount64();
        std::lock_guard lock(g_mutex);
        if (now - g_last_log >= 2000)
        {
            g_last_log = now;
            const std::string message = "ETS2 constant-buffer tracker: mirrors=" +
                std::to_string(g_buffer_mirrors.size()) + " updates=" +
                std::to_string(g_constant_buffer_updates) + " vs_binds=" +
                std::to_string(g_vs_constant_buffer_binds);
            reshade::log::message(reshade::log::level::info, message.c_str());
        }
        g_viewport_draws.clear();
        return;
    }

    const Prediction prediction = build_prediction();
    const ULONGLONG now = GetTickCount64();
    if (now - g_last_log < 2000) return;
    g_last_log = now;

    uint32_t main_width = 0, main_height = 0;
    if (swapchain != nullptr)
    {
        const resource_desc back_buffer = swapchain->get_device()->get_resource_desc(swapchain->get_back_buffer(0));
        main_width = back_buffer.texture.width;
        main_height = back_buffer.texture.height;
    }

    std::vector<std::pair<uint64_t, uint64_t>> bins;
    std::vector<std::pair<uint64_t, uint64_t>> guide_viewports;
    std::vector<std::pair<uint64_t, uint64_t>> guide_targets;
    {
        std::lock_guard lock(g_mutex);
        bins.assign(g_viewport_draws.begin(), g_viewport_draws.end());
        guide_viewports.assign(g_guide_viewport_draws.begin(), g_guide_viewport_draws.end());
        guide_targets.assign(g_guide_target_draws.begin(), g_guide_target_draws.end());
        g_viewport_draws.clear();
        g_guide_viewport_draws.clear();
        g_guide_target_draws.clear();
        for (auto &[_, pass] : g_passes) pass.draw_calls = 0;
    }

    std::sort(bins.begin(), bins.end(), [](const auto &left, const auto &right) {
        return left.second > right.second;
    });
    const auto descending = [](const auto &left, const auto &right) {
        return left.second > right.second;
    };
    std::sort(guide_viewports.begin(), guide_viewports.end(), descending);
    std::sort(guide_targets.begin(), guide_targets.end(), descending);

    uint64_t buffer_updates = 0, buffer_binds = 0;
    uint64_t eligible_draws = 0, missing_bindings = 0, missing_mirrors = 0;
    uint64_t stale_mirrors = 0, range_overflows = 0, max_offset = 0, max_size = 0;
    size_t mirror_count = 0, preferred_camera_count = 0;
    {
        std::lock_guard lock(g_mutex);
        buffer_updates = g_constant_buffer_updates;
        buffer_binds = g_vs_constant_buffer_binds;
        mirror_count = g_buffer_mirrors.size();
        preferred_camera_count = g_preferred_cameras.size();
        eligible_draws = g_capture_eligible_draws;
        missing_bindings = g_capture_missing_bindings;
        missing_mirrors = g_capture_missing_mirrors;
        stale_mirrors = g_capture_stale_mirrors;
        range_overflows = g_capture_range_overflows;
        max_offset = g_max_bound_offset;
        max_size = g_max_bound_size;
    }
    std::string message = "ETS2 world probe: boxes=" + std::to_string(prediction.boxes.size()) +
        " guide_draws=" + std::to_string(g_guide_draw_calls.load(std::memory_order_relaxed)) +
        " cam_attempts=" + std::to_string(g_camera_validation_attempts.load(std::memory_order_relaxed)) +
        " cam_hits=" + std::to_string(g_camera_validation_hits.load(std::memory_order_relaxed)) +
        " preferred=" + std::to_string(preferred_camera_count) +
        " back_buffer=" + std::to_string(main_width) + "x" + std::to_string(main_height) + " viewports=";
    const size_t bin_count = std::min<size_t>(bins.size(), 10);
    for (size_t i = 0; i < bin_count; ++i)
    {
        const uint32_t width = static_cast<uint32_t>(bins[i].first >> 32);
        const uint32_t height = static_cast<uint32_t>(bins[i].first);
        if (i != 0) message += ",";
        message += std::to_string(width) + "x" + std::to_string(height) + ":" +
                   std::to_string(bins[i].second);
    }
    message += " guide_vp=";
    for (size_t i = 0; i < std::min<size_t>(guide_viewports.size(), 6); ++i)
    {
        if (i != 0) message += ",";
        message += std::to_string(static_cast<uint32_t>(guide_viewports[i].first >> 32)) + "x" +
                   std::to_string(static_cast<uint32_t>(guide_viewports[i].first)) + ":" +
                   std::to_string(guide_viewports[i].second);
    }
    message += " guide_rt=";
    for (size_t i = 0; i < std::min<size_t>(guide_targets.size(), 6); ++i)
    {
        if (i != 0) message += ",";
        message += std::to_string(static_cast<uint32_t>(guide_targets[i].first >> 32)) + "x" +
                   std::to_string(static_cast<uint32_t>(guide_targets[i].first)) + ":" +
                   std::to_string(guide_targets[i].second);
    }
    message += " cb_mirrors=" + std::to_string(mirror_count) +
               " cb_updates=" + std::to_string(buffer_updates) +
               " vs_cb_binds=" + std::to_string(buffer_binds) +
               " cap_eligible=" + std::to_string(eligible_draws) +
               " cap_no_bind=" + std::to_string(missing_bindings) +
               " cap_no_mirror=" + std::to_string(missing_mirrors) +
               " cap_stale=" + std::to_string(stale_mirrors) +
               " cap_overflow=" + std::to_string(range_overflows) +
               " max_offset=" + std::to_string(max_offset) +
               " max_size=" + std::to_string(max_size);
    reshade::log::message(reshade::log::level::info, message.c_str());
}
}

extern "C" __declspec(dllexport) const char *NAME = "ETS2 Reverse Posture World Guide";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Draws telemetry-derived reverse posture frames directly in the ETS2 world.";

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        if (!reshade::register_addon(module)) return FALSE;
        reshade::register_event<reshade::addon_event::init_resource>(on_init_resource);
        reshade::register_event<reshade::addon_event::destroy_resource>(on_destroy_resource);
        reshade::register_event<reshade::addon_event::map_buffer_region>(on_map_buffer);
        reshade::register_event<reshade::addon_event::unmap_buffer_region>(on_unmap_buffer);
        reshade::register_event<reshade::addon_event::update_buffer_region>(on_update_buffer);
        reshade::register_event<reshade::addon_event::update_buffer_region_command>(on_update_buffer_command);
        reshade::register_event<reshade::addon_event::init_resource_view>(on_init_resource_view);
        reshade::register_event<reshade::addon_event::destroy_resource_view>(on_destroy_resource_view);
        reshade::register_event<reshade::addon_event::bind_viewports>(on_bind_viewports);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_targets);
        reshade::register_event<reshade::addon_event::push_descriptors>(on_push_descriptors);
        reshade::register_event<reshade::addon_event::reset_command_list>(on_reset_command_list);
        reshade::register_event<reshade::addon_event::draw>(on_draw);
        reshade::register_event<reshade::addon_event::draw_indexed>(on_draw_indexed);
        reshade::register_event<reshade::addon_event::present>(on_present);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        close_telemetry();
        destroy_renderer();
        reshade::unregister_addon(module);
    }
    return TRUE;
}
