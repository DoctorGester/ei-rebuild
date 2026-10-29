// The new renderer's frame: the switch, the Direct3D 11 device behind sokol_gfx, the off-screen
// target the frame is drawn into (the game's resolution) and reading it back for captures. See render.h and docs/notes/renderer.md.
#include "render.h"
#include "render_internal.h"
#include "render_perf.h"
#include "render_ui.h"

#include "../hook/log.h"
#include "ei/hook.h"
#include "ei/x87.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <float.h>
#if defined(EI_RENDER_VALIDATION)
#include <d3d11on12.h>
#include <d3d12.h>
#endif

#include "render_state.h"
#include "../game/texture.h"
#include "../game/app.h"
#include "../game/game_singletons.h"
#include "../game/model.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace render {

namespace {

using namespace detail;

// --- The switch ------------------------------------------------------------------------------------

bool g_configured = false;
bool g_smooth = false;
bool g_smoothCamera = false;
#if defined(EI_RENDER_VALIDATION)
bool g_removable = false;   // "removable" (harness build only): the device is made through D3D11On12
bool g_checkAll = false;    // "checkall" (harness build only): every read-back check runs; else the first few of each kind, then every 16th
#endif

// EI_RENDER's words (render.h).
void ParseSpec(const char* spec) {
    g_smooth = false;
    g_smoothCamera = false;
#if defined(EI_RENDER_VALIDATION)
    g_removable = false;
    g_checkAll = false;
#endif
    std::string s = spec ? spec : "";
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t comma = s.find(',', start);
        std::string word = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!word.empty() && word.front() == ' ') word.erase(word.begin());
        while (!word.empty() && word.back() == ' ') word.pop_back();
        if (word == "new" || word == "present" || word == "cpuwindows") {}
        else if (word == "smooth") g_smooth = true;
        else if (word == "smoothcamera") g_smoothCamera = true;
#if defined(EI_RENDER_VALIDATION)
        else if (word == "removable") g_removable = true;
        else if (word == "checkall") g_checkAll = true;
#endif
        else if (!word.empty()) ei::Log("render: unknown option '%s' in EI_RENDER", word.c_str());
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    g_configured = true;
}

std::string EnvironmentSpec() {
    char spec[256];
    DWORD n = GetEnvironmentVariableA("EI_RENDER", spec, sizeof(spec));
    if (n > 0 && n < sizeof(spec)) return std::string(spec);
    return std::string();
}

void EnsureConfigured() {
    if (g_configured) return;
    ParseSpec(EnvironmentSpec().c_str());
}

// --- Game memory -----------------------------------------------------------------------------------

std::uint32_t GameClearColour() { return g_sceneRenderer->fogColour_; }

sg_color FromD3DColour(std::uint32_t c) {
    return sg_color{static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                    static_cast<float>(c & 0xFF) / 255.0f, 1.0f};
}


ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
int g_deviceSamples = 1;   // the most samples up to kSmoothSamples the device draws the frame with

void SokolLog(const char* tag, std::uint32_t level, std::uint32_t item, const char* message, std::uint32_t line,
              const char* /*file*/, void* /*user*/) {
    static const char* const kLevels[] = {"panic", "error", "warning", "info"};
    // Each message at most 10 times (a full pool would otherwise log every frame).
    static unsigned char counts[1024];
    if (level > 0 && item < 1024 && counts[item]++ >= 10) {
        counts[item] = 11;
        return;
    }
    ei::Log("render: %s %s %u (line %u): %s", tag ? tag : "sg", level < 4 ? kLevels[level] : "?", item, line,
            message ? message : "");
    // sokol cannot go on after a panic (it would abort): the game ends with the message.
    if (level == 0) Fatal("The renderer stopped: sokol_gfx panic %u (%s).", item, message ? message : "");
}

#if defined(EI_RENDER_VALIDATION)
// --- A device the harness can remove --------------------------------------------------------------
// EI_RENDER "removable" (harness build only): render::RemoveDevice.

ID3D12Device* g_d3d12 = nullptr;
ID3D12CommandQueue* g_queue = nullptr;
bool g_removeAsked = false;   // render::RemoveDevice: removed at the next present (RemoveIfAsked)
int g_removals = 0;

void ReleaseRemovable() {
    if (g_queue) g_queue->Release();
    g_queue = nullptr;
    if (g_d3d12) {
        ULONG left = g_d3d12->Release();
        if (left) ei::Log("render: the Direct3D 12 device under the removable one keeps %lu references", static_cast<unsigned long>(left));
    }
    g_d3d12 = nullptr;
}

HRESULT CreateRemovableDevice(const D3D_FEATURE_LEVEL* levels, UINT count, D3D_FEATURE_LEVEL* got) {
    HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    auto create12 = d3d12 ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
    auto create11on12 =
        d3d11 ? reinterpret_cast<PFN_D3D11ON12_CREATE_DEVICE>(GetProcAddress(d3d11, "D3D11On12CreateDevice")) : nullptr;
    if (!create12 || !create11on12) {
        ei::Log("render: removable device: no D3D12CreateDevice or D3D11On12CreateDevice");
        return E_NOINTERFACE;
    }
    HRESULT hr = create12(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_d3d12));
    if (FAILED(hr)) {
        ei::Log("render: removable device: D3D12CreateDevice failed (%08lX)", static_cast<unsigned long>(hr));
        g_d3d12 = nullptr;
        return hr;
    }
    // Direct3D 12 devices are one per adapter while one lives: a removed one still referenced
    // somewhere would come back here.
    HRESULT removed = g_d3d12->GetDeviceRemovedReason();
    if (FAILED(removed)) {
        ei::Log("render: removable device: D3D12CreateDevice gave a removed device (%08lX)", static_cast<unsigned long>(removed));
        ReleaseRemovable();
        return removed;
    }
    D3D12_COMMAND_QUEUE_DESC queue{};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = g_d3d12->CreateCommandQueue(&queue, IID_PPV_ARGS(&g_queue));
    if (SUCCEEDED(hr)) {
        IUnknown* queues[] = {g_queue};
        hr = create11on12(g_d3d12, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, count, queues, 1, 0, &g_device, &g_context, got);
    }
    if (FAILED(hr)) {
        ei::Log("render: removable device: D3D11On12CreateDevice failed (%08lX)", static_cast<unsigned long>(hr));
        g_device = nullptr;
        g_context = nullptr;
        ReleaseRemovable();
    }
    return hr;
}
#endif

const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
constexpr UINT kLevelCount = static_cast<UINT>(sizeof(kLevels) / sizeof(kLevels[0]));

HRESULT CreatePlainDevice(ID3D11Device** device, ID3D11DeviceContext** context, D3D_FEATURE_LEVEL* got) {
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels, kLevelCount,
                             D3D11_SDK_VERSION, device, got, context);
}

// The device made ahead (render::PrepareDevice): on a thread of its own while the game starts up.
struct PreparedDevice {
    HANDLE thread = nullptr;
    HRESULT result = E_FAIL;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL got{};
};
PreparedDevice g_prepared;

DWORD WINAPI PrepareDeviceThread(void*) {
    g_prepared.result = CreatePlainDevice(&g_prepared.device, &g_prepared.context, &g_prepared.got);
    return 0;
}

bool TakePreparedDevice(HRESULT* result, D3D_FEATURE_LEVEL* got) {
    if (!g_prepared.thread) return false;
    WaitForSingleObject(g_prepared.thread, INFINITE);
    CloseHandle(g_prepared.thread);
    g_prepared.thread = nullptr;
    *result = g_prepared.result;
    g_device = g_prepared.device;
    g_context = g_prepared.context;
    *got = g_prepared.got;
    return true;
}

// Why the last CreateDevice failed (for the fatal error's message).
char g_deviceError[160] = "";

bool CreateDevice() {
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = S_OK;
#if defined(EI_RENDER_VALIDATION)
    if (g_removable) hr = CreateRemovableDevice(kLevels, kLevelCount, &got);
    else
#endif
        if (!TakePreparedDevice(&hr, &got)) hr = CreatePlainDevice(&g_device, &g_context, &got);
    if (FAILED(hr)) {
        std::snprintf(g_deviceError, sizeof(g_deviceError), "no Direct3D 11 device (D3D11CreateDevice failed, %08lX)",
                      static_cast<unsigned long>(hr));
        ei::Log("render: %s", g_deviceError);
        return false;
    }
    sg_desc desc{};
    desc.environment.defaults.color_format = kColourFormat;
    desc.environment.defaults.depth_format = kDepthFormat;
    desc.environment.defaults.sample_count = 1;
    desc.environment.d3d11.device = g_device;
    desc.environment.d3d11.device_context = g_context;
    desc.logger.func = &SokolLog;
    // The game's textures, canvases and the images replaced within a frame are far more than sokol's
    // default pools (128 images, 256 views); a full pool made sg_make_image fail.
    desc.image_pool_size = 16384;
    desc.view_pool_size = 16384;
    desc.buffer_pool_size = 512;
    desc.pipeline_pool_size = 1024;
    desc.sampler_pool_size = 256;
    sg_setup(&desc);
    if (!sg_isvalid()) {
        std::snprintf(g_deviceError, sizeof(g_deviceError), "sokol_gfx could not be set up on the Direct3D 11 device");
        ei::Log("render: %s", g_deviceError);
        g_context->Release();
        g_device->Release();
        g_context = nullptr;
        g_device = nullptr;
#if defined(EI_RENDER_VALIDATION)
        ReleaseRemovable();
#endif
        return false;
    }
#if defined(EI_RENDER_VALIDATION)
    if (g_d3d12)
        ei::Log("render: Direct3D 11 device ready (feature level %X), through D3D11On12 on a Direct3D 12 device: removable",
                static_cast<unsigned>(got));
    else
#endif
        ei::Log("render: Direct3D 11 device ready (feature level %X)", static_cast<unsigned>(got));
    g_deviceSamples = SupportedSamples(g_device);
    ProtectDevice(g_device, g_context);
    return true;
}

// --- The off-screen target ---------------------------------------------------------------------------

int g_width = 0, g_height = 0;
sg_image g_colour{}, g_depth{};
sg_view g_colourAttachment{}, g_colourTexture{}, g_depthAttachment{};
bool g_haveFrame = false;
// With the smooth option the world's runs of commands draw into a multisampled target of
// g_targetSamples (render_smooth.cpp) whose colour each such pass resolves into g_colour; the frame
// target's depth has a texture view then (copied into the multisampled one before a world run).
int g_targetSamples = 1;
sg_image g_multiColour{}, g_multiDepth{};
sg_view g_multiColourAttachment{}, g_multiDepthAttachment{}, g_multiDepthTexture{}, g_resolveAttachment{}, g_depthTexture{};

void DestroyMultisampled() {
    for (sg_view* v : {&g_multiColourAttachment, &g_multiDepthAttachment, &g_multiDepthTexture, &g_resolveAttachment, &g_depthTexture}) {
        if (v->id) sg_destroy_view(*v);
        *v = sg_view{};
    }
    if (g_multiColour.id) sg_destroy_image(g_multiColour);
    if (g_multiDepth.id) sg_destroy_image(g_multiDepth);
    g_multiColour = g_multiDepth = sg_image{};
    g_targetSamples = 1;
}

void DestroyTarget() {
    DestroyMultisampled();
    if (g_colourAttachment.id) sg_destroy_view(g_colourAttachment);
    if (g_colourTexture.id) sg_destroy_view(g_colourTexture);
    if (g_depthAttachment.id) sg_destroy_view(g_depthAttachment);
    if (g_colour.id) sg_destroy_image(g_colour);
    if (g_depth.id) sg_destroy_image(g_depth);
    g_colourAttachment = g_colourTexture = g_depthAttachment = sg_view{};
    g_colour = g_depth = sg_image{};
    g_width = g_height = 0;
    g_haveFrame = false;
}

// The multisampled colour and depth of `samples`; false when the device refused them.
bool MakeMultisampled(int width, int height, int samples) {
    sg_image_desc colour{};
    colour.usage.color_attachment = true;
    colour.width = width;
    colour.height = height;
    colour.pixel_format = kColourFormat;
    colour.sample_count = samples;
    colour.label = "ei-frame-colour-multisampled";
    g_multiColour = sg_make_image(&colour);
    sg_image_desc depth{};
    depth.usage.depth_stencil_attachment = true;
    depth.width = width;
    depth.height = height;
    depth.pixel_format = kDepthFormat;
    depth.sample_count = samples;
    depth.label = "ei-frame-depth-multisampled";
    g_multiDepth = sg_make_image(&depth);
    sg_view_desc v{};
    v.color_attachment.image = g_multiColour;
    g_multiColourAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.depth_stencil_attachment.image = g_multiDepth;
    g_multiDepthAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = g_multiDepth;
    g_multiDepthTexture = sg_make_view(&v);
    v = sg_view_desc{};
    v.resolve_attachment.image = g_colour;
    g_resolveAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = g_depth;
    g_depthTexture = sg_make_view(&v);
    bool ok = sg_query_image_state(g_multiColour) == SG_RESOURCESTATE_VALID &&
              sg_query_image_state(g_multiDepth) == SG_RESOURCESTATE_VALID;
    for (sg_view view : {g_multiColourAttachment, g_multiDepthAttachment, g_multiDepthTexture, g_resolveAttachment, g_depthTexture})
        ok = ok && sg_query_view_state(view) == SG_RESOURCESTATE_VALID;
    ok = ok && PrepareSmoothCopies(samples);
    if (!ok) {
        DestroyMultisampled();
        return false;
    }
    g_targetSamples = samples;
    return true;
}

// The frame target of the game's size; the multisampled one beside it when the smooth option is on.
// The option's change takes effect at a frame's first pass (`mayChange`).
bool EnsureTarget(int width, int height, bool mayChange) {
    int samples = g_smooth ? g_deviceSamples : 1;
    if (width == g_width && height == g_height && g_colour.id && (samples == g_targetSamples || !mayChange)) return true;
    DestroyTarget();
    sg_image_desc colour{};
    colour.usage.color_attachment = true;
    colour.usage.resolve_attachment = samples > 1;
    colour.width = width;
    colour.height = height;
    colour.pixel_format = kColourFormat;
    colour.sample_count = 1;
    colour.label = "ei-frame-colour";
    g_colour = sg_make_image(&colour);
    sg_image_desc depth{};
    depth.usage.depth_stencil_attachment = true;
    depth.width = width;
    depth.height = height;
    depth.pixel_format = kDepthFormat;
    depth.sample_count = 1;
    depth.label = "ei-frame-depth";
    g_depth = sg_make_image(&depth);
    sg_view_desc v{};
    v.color_attachment.image = g_colour;
    g_colourAttachment = sg_make_view(&v);
    v = sg_view_desc{};
    v.texture.image = g_colour;
    g_colourTexture = sg_make_view(&v);
    v = sg_view_desc{};
    v.depth_stencil_attachment.image = g_depth;
    g_depthAttachment = sg_make_view(&v);
    if (sg_query_image_state(g_colour) != SG_RESOURCESTATE_VALID || sg_query_image_state(g_depth) != SG_RESOURCESTATE_VALID) {
        ei::Log("render: cannot create the %dx%d frame target", width, height);
        DestroyTarget();
        return false;
    }
    g_width = width;
    g_height = height;
    if (samples > 1 && !MakeMultisampled(width, height, samples)) {
        ei::Log("render: the smooth option: no %d-sample %dx%d target; the world is drawn without multisampling", samples, width, height);
        g_deviceSamples = 1;
    }
    if (g_smooth)
        ei::Log("render: the smooth option: the world drawn with %d samples into the %dx%d frame, its models sampled "
                "anisotropically (%u)", g_targetSamples, width, height, kAnisotropy);
    return true;
}

// --- The frame -----------------------------------------------------------------------------------------

bool g_frameDrawn = false;
bool g_started = false;
std::uint32_t g_frameNumber = 0;
std::uint32_t g_commits = 0;

// The device, sokol and the pipelines at the first use. The game cannot run without them: a failure ends it.
void MakePipelines(const char* what) {
    if (!CreateTerrainShader()) Fatal("%s: the terrain shader could not be made.", what);
    if (!CreateMeshShader()) Fatal("%s: the mesh shader could not be made.", what);
    if (!CreateBlitPipeline()) Fatal("%s: the blit pipeline could not be made.", what);
}

} // namespace

void detail::Start() {
    EnsureConfigured();
    if (g_started) return;
    FpuGuard fpu;
    if (!CreateDevice()) Fatal("Can't start the renderer: %s.", g_deviceError);
    MakePipelines("Can't start the renderer");
    g_started = true;
}

namespace {

// --- A lost device -------------------------------------------------------------------------------------

bool g_deviceLost = false;     // seen (a DXGI error, GetDeviceRemovedReason): made again at the present
bool g_simulateLoss = false;   // render::LoseDevice
bool g_simulationTaken = false;   // the loss being reported is LoseDevice's
int g_losses = 0, g_recoveries = 0;
long g_lastLossResult = 0, g_lastLossReason = 0;   // what reported the last loss, GetDeviceRemovedReason then
bool g_lastLossSimulated = false;

// Everything made on the lost device is released (sokol's resources through each module, then sg_shutdown),
// the device and context with them; a new device gets sokol again and the pipelines. The frame target, the
// window's swap chain and textures are made again as the next frames ask for them (from the file levels kept for
// them), the interface canvases from the commands each keeps. No new device ends the game.
void RecreateDevice() {
    FpuGuard fpu;
    ++g_losses;
    std::size_t kept = 0, keptBytes = 0;
    DropCursorPresents();
    DropWindow();
    DestroyTarget();
    DestroyTerrain();
    DestroyMeshes();
    DropBlits();
    DropCanvases();
    DestroyCompose();
    DestroySmooth();
    DestroyShadows();
    perf::Release();
    DropTextures(&kept, &keptBytes);
    sg_shutdown();
    g_context->ClearState();
    g_context->Flush();
    g_context->Release();
    ULONG left = g_device->Release();
    g_context = nullptr;
    g_device = nullptr;
#if defined(EI_RENDER_VALIDATION)
    ReleaseRemovable();
#endif
    ei::Log("render: the lost Direct3D 11 device released (%lu references left); making a new one (%zu textures to make "
            "again from %zu KB of their files)",
            static_cast<unsigned long>(left), kept, keptBytes / 1024);
    // A device right after a driver reset may not be available at once.
    bool made = false;
    for (int attempt = 0; attempt < 5 && !made; ++attempt) {
        if (attempt) Sleep(200);
        made = CreateDevice();
    }
    if (!made) Fatal("The Direct3D 11 device was lost and no new one could be made: %s.", g_deviceError);
    MakePipelines("The Direct3D 11 device was lost and made again");
    RemakeComposed();
    ++g_recoveries;
    g_deviceLost = false;
}

// The end of a presented frame (nothing of it holds sokol resources any more): a loss reported
// during the frame, asked for, or found by GetDeviceRemovedReason (a loss nothing reported, e.g.
// while minimised) makes everything again.
void CheckDevice() {
    if (TakeSimulatedLoss()) ReportDeviceLost(DXGI_ERROR_DEVICE_REMOVED, S_OK);
#if defined(EI_RENDER_VALIDATION)
    RemoveIfAsked();   // without a swap chain of the game window's: found by GetDeviceRemovedReason below
#endif
    if (!g_deviceLost && g_device) {
        HRESULT reason = g_device->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            ei::Log("render: the Direct3D 11 device was lost (GetDeviceRemovedReason %08lX)", static_cast<unsigned long>(reason));
            ReportDeviceLost(reason, reason);
        }
    }
    if (g_deviceLost) RecreateDevice();
}

#if defined(EI_RENDER_VALIDATION)
// The timer of the device mesh draw running (at most one), made in place so no static destructor comes with it.
alignas(detail::StatsTimer) unsigned char g_deviceMeshTimer[sizeof(detail::StatsTimer)];
bool g_deviceMeshTiming = false;
#endif

#if defined(EI_RENDER_VALIDATION)
struct GameTimer {
    alignas(detail::StatsTimer) unsigned char storage[sizeof(detail::StatsTimer)];
    bool running = false;
    void Begin(double FrameStats::* field) {
        if (running) return;
        new (storage) detail::StatsTimer(field);
        running = true;
    }
    bool End() {
        if (!running) return false;
        std::launder(reinterpret_cast<detail::StatsTimer*>(storage))->~StatsTimer();
        running = false;
        return true;
    }
};
GameTimer g_decalTimer, g_flatDrawTimer, g_shadowBuildTimer;
#endif

FrameStats g_statsSum;      // the presented frames since the last TakeFrameStats
std::uint32_t g_lastPipeline = 0, g_lastTexture = 0;   // ids applied last in this frame
#if defined(EI_RENDER_VALIDATION)
StatsTimer* g_innerTimer = nullptr;
// CountReuse: per key the hashes of the vertices' geometry (positions, texture coordinates) and
// colours, this frame's and the last; this frame's mesh geometry hashes.
struct VertexHashes {
    std::uint64_t geometry, colours;
};
std::unordered_map<std::uint64_t, VertexHashes> g_hashes, g_lastHashes;
std::unordered_set<std::uint64_t> g_frameGeometry;

std::uint64_t Fnv(std::uint64_t h, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i + 4 <= bytes; i += 4) {
        std::uint32_t word;
        std::memcpy(&word, p + i, 4);
        h = (h ^ word) * 0x100000001B3ull;
    }
    return h;
}
double g_msPerTick = 0.0;

long long Ticks() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}
#endif

void AddFrameStats() {
    g_stats.matrices = g_frame.matrices.size();
    if (g_width > 0) {
        g_statsSum.width = g_width;
        g_statsSum.height = g_height;
    }
    FrameStats& s = g_statsSum;
    const FrameStats& f = g_stats;
    s.frames += 1;
    for (auto field : {&FrameStats::terrainPages, &FrameStats::terrainBatches, &FrameStats::meshes, &FrameStats::meshPasses,
                       &FrameStats::transformedDraws, &FrameStats::blits, &FrameStats::depthClears, &FrameStats::stateReads,
                       &FrameStats::flatMeshes, &FrameStats::flatVertexBytes, &FrameStats::matrices, &FrameStats::terrainVertexBytes, &FrameStats::terrainIndexBytes,
                       &FrameStats::meshVertexBytes, &FrameStats::meshIndexBytes, &FrameStats::uniformBytes, &FrameStats::passes,
                       &FrameStats::draws, &FrameStats::pipelineApplies, &FrameStats::pipelineChanges, &FrameStats::bindings,
                       &FrameStats::textureChanges, &FrameStats::morphMeshes,
                       &FrameStats::morphCorners, &FrameStats::decalShadows, &FrameStats::decalPrints,
                       &FrameStats::decalVertices, &FrameStats::flatDraws,
                       &FrameStats::flatDrawVertices, &FrameStats::shadowBuilds})
        s.*field += f.*field;
    for (int k = 0; k < FrameStats::UploadKinds; ++k) {
        s.uploads[k] += f.uploads[k];
        s.uploadBytes[k] += f.uploadBytes[k];
    }
    for (int k = 0; k < FrameStats::ReuseKinds; ++k) {
        s.terrainReuse[k] += f.terrainReuse[k];
        s.meshReuse[k] += f.meshReuse[k];
    }
    s.sharedMeshes += f.sharedMeshes;
    s.handOverMs += f.handOverMs;
    s.terrainMs += f.terrainMs;
    s.textureMs += f.textureMs;
    s.endFrameMs += f.endFrameMs;
    s.statsMs += f.statsMs;
    s.deviceMeshMs += f.deviceMeshMs;
    for (auto field : {&FrameStats::decalShadowMs, &FrameStats::decalPrintMs, &FrameStats::flatMs,
                       &FrameStats::shadowBuildMs})
        s.*field += f.*field;
    s.shadowDraws += f.shadowDraws;
    s.shadowMs += f.shadowMs;
    s.gpuFrames += f.gpuFrames;
    s.gpuShadowMs += f.gpuShadowMs;
    s.gpuFrameMs += f.gpuFrameMs;
    g_stats = FrameStats{};
    g_lastPipeline = g_lastTexture = 0;
#if defined(EI_RENDER_VALIDATION)
    g_lastHashes.swap(g_hashes);
    g_hashes.clear();
    g_frameGeometry.clear();
#endif
}

std::size_t g_drawnCommands = 0;   // commands of this frame already drawn into the target
int g_passes = 0;                  // passes drawn this frame
int g_passSamples = 1;             // the sample count of the pass being drawn (PassSampleCount)

// Which target holds the frame's latest pixels: none yet (the frame's first pass clears), the frame
// target, or the multisampled one (its colour also resolved into the frame target).
enum class Holder : std::uint8_t { None, Frame, Multisampled };
Holder g_holder = Holder::None;

SmoothInfo g_counts, g_lastCounts;

#if defined(EI_RENDER_VALIDATION)
SmoothOrder g_smoothOrder = SmoothOrder::Game;
#endif

bool InWorld(std::size_t k) {
    for (const Frame::Range& r : g_frame.worldRanges)
        if (k >= r.first && k < r.end) return true;
    return false;
}

// A pass on the frame target. After a multisampled run its colour is the resolve's; its depth is
// set from the multisampled one.
void BeginFramePass() {
    bool first = g_holder == Holder::None;
    bool afterWorld = g_holder == Holder::Multisampled;
    sg_pass pass{};
    pass.action.colors[0].load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = FromD3DColour(GameClearColour());
    pass.action.depth.load_action = first ? SG_LOADACTION_CLEAR : afterWorld ? SG_LOADACTION_DONTCARE : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = g_colourAttachment;
    pass.attachments.depth_stencil = g_depthAttachment;
    pass.label = "ei-frame";
    sg_begin_pass(&pass);
    ++g_stats.passes;
    g_passSamples = 1;
    if (afterWorld) {
        CopyDepthFromMultisampled(g_multiDepthTexture);
        ++g_counts.depthCopiesBack;
    }
    g_holder = Holder::Frame;
}

// A pass on the multisampled target, resolved into the frame target at its end. After draws into the
// frame target their colour and depth are copied into it first (a pass of its own: the frame target
// is the resolve's destination and cannot be read in that pass).
void BeginMultisampledPass() {
    bool first = g_holder == Holder::None;
    if (g_holder == Holder::Frame) {
        sg_pass copy{};
        copy.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        copy.action.colors[0].store_action = SG_STOREACTION_STORE;
        copy.action.depth.load_action = SG_LOADACTION_DONTCARE;
        copy.action.depth.store_action = SG_STOREACTION_STORE;
        copy.attachments.colors[0] = g_multiColourAttachment;
        copy.attachments.depth_stencil = g_multiDepthAttachment;
        copy.label = "ei-frame-into-multisampled";
        sg_begin_pass(&copy);
        ++g_stats.passes;
        g_passSamples = g_targetSamples;
        CopyIntoMultisampled(g_colourTexture, g_depthTexture);
        sg_end_pass();
        ++g_counts.copiesInto;
    }
    sg_pass pass{};
    pass.action.colors[0].load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = FromD3DColour(GameClearColour());
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.depth.load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 1.0f;
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.attachments.colors[0] = g_multiColourAttachment;
    pass.attachments.resolves[0] = g_resolveAttachment;
    pass.attachments.depth_stencil = g_multiDepthAttachment;
    pass.label = "ei-frame-multisampled";
    sg_begin_pass(&pass);
    ++g_stats.passes;
    g_passSamples = g_targetSamples;
    g_holder = Holder::Multisampled;
    ++g_counts.multisampledPasses;
}

#if defined(EI_RENDER_VALIDATION)
// An empty pass on the frame target (render::SetSmoothOrder's orders): the frame's first clears it;
// after a world run it takes the depth back from sample 0.
void EmptyFramePass() {
    BeginFramePass();
    sg_end_pass();
    ++g_passes;
}
#endif

void DrawCommands(std::size_t from, std::size_t to, int width, int height) {
    for (std::size_t k = from; k < to; ++k) {
        const Command& c = g_frame.commands[k];
        auto i = static_cast<std::size_t>(c.index);
        if (perf::On()) perf::WorldCommand(InWorld(k));
        switch (c.kind) {
        case Command::Terrain: DrawTerrainBatch(g_frame, g_frame.terrainBatches[i]); break;
        case Command::Mesh: {
            const MeshBatch& batch = g_frame.meshBatches[i];
            // The smooth option: the world's models sampled anisotropically; the interface's sprites and
            // previews and the pre-transformed draws as the stage says.
            bool anisotropic = g_smooth && batch.model && InWorld(k);
            if (batch.model) ++(anisotropic ? g_counts.anisotropicModels : g_counts.plainModels);
            DrawMeshBatch(g_frame, batch, anisotropic);
            break;
        }
        case Command::Blit: DrawBlitBatch(g_frame, g_frame.blitBatches[i], width, height); break;
        case Command::ClearDepth: DrawClearDepth(g_frame.clears[i], width, height); break;
        case Command::Fill: DrawFill(g_frame.fills[i], width, height); break;
        case Command::Canvas: DrawCanvasBatch(g_frame.canvasBatches[i], width, height); break;
        }
    }
}

// Draws the frame's commands not drawn yet into the off-screen target: one pass, the frame's first clearing
// it. A frame is drawn in more than one pass when a copy of it is taken in its middle (CopyFrameTo), and with
// the smooth option one pass per run of world and other commands. The interface canvases' queued drawing goes first.
bool DrawPending() {
    perf::BeginGpuFrame();
    RunCanvasCommands();
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height) || !EnsureTarget(width, height, g_passes == 0)) return false;
    if (g_passes > 0 && g_drawnCommands == g_frame.commands.size()) return true;
    UploadTerrain(g_frame);
    UploadMeshes(g_frame);
    // The sun's shadow map, once a frame before its first pass (the classic option: none).
    if (!g_frame.shadow.prepared) {
        perf::CpuTimer shadowTime(perf::Cpu::Shadow);
        perf::Stamp(perf::Mark::ShadowBegin);
        PrepareShadows(g_frame);
        DrawShadowMap(g_frame);
        perf::Stamp(perf::Mark::ShadowEnd);
    }
    // Direct3D 7 puts pixel centres at integer screen coordinates, Direct3D 11 half a pixel further: the 3D
    // draws move their positions by half a pixel (Frame::pixelShift); sokol takes whole pixels only.
    g_frame.pixelShift[0] = 1.0f / static_cast<float>(width);
    g_frame.pixelShift[1] = -1.0f / static_cast<float>(height);
    std::size_t k = g_drawnCommands, end = g_frame.commands.size();
    bool multisampling = g_targetSamples > 1;
    std::size_t split = SIZE_MAX;   // the first world run ends here (SmoothOrder::Split)
#if defined(EI_RENDER_VALIDATION)
    if (multisampling && g_smoothOrder == SmoothOrder::Before && g_holder == Holder::None && k < end && InWorld(k))
        EmptyFramePass();
    if (multisampling && g_smoothOrder == SmoothOrder::Split && !g_frame.worldRanges.empty()) {
        const Frame::Range& r = g_frame.worldRanges.front();
        std::size_t last = r.end < end ? r.end : end;
        if (last > r.first + 1) split = r.first + (last - r.first) / 2;
    }
#endif
    do {
        bool world = multisampling && k < end && InWorld(k);
        std::size_t stop = k;
        while (stop < end && (multisampling && InWorld(stop)) == world && !(world && stop == split && stop > k)) ++stop;
        if (world) BeginMultisampledPass();
        else BeginFramePass();
        DrawCommands(k, stop, width, height);
        sg_end_pass();
        g_passSamples = 1;
        ++g_passes;
#if defined(EI_RENDER_VALIDATION)
        if (world && stop == split) EmptyFramePass();
#endif
        k = stop;
    } while (k < end);
    g_drawnCommands = end;
    CanvasBlitsDrawn();
    return true;
}

// DrawPending for a draw in the middle of the frame (CopyFrameTo). The cursor thread draws the frame target
// itself, so it takes none of it while a pass is in progress: the present's lock, no cursor presents until
// the frame's own present.
bool DrawPendingAlone() {
    BeginWindowPresent();
    bool drawn = DrawPending();
    EndWindowPresent(false);
    return drawn;
}

// Nothing of this frame drawn or queued yet (a copy between frames: the quick save's key): the target still
// holds the last frame, as the original's render target did. A pass now would clear it first.
bool BetweenFrames() { return g_haveFrame && g_passes == 0 && g_frame.commands.empty(); }

} // namespace

namespace detail {

void ReportDeviceLost(long result, long reason) {
    if (!g_deviceLost) {
        g_lastLossResult = result;
        g_lastLossReason = reason;
        g_lastLossSimulated = g_simulationTaken;
    }
    g_simulationTaken = false;
    g_deviceLost = true;
}
bool DeviceIsLost() { return g_deviceLost; }

bool TakeSimulatedLoss() {
    if (!g_simulateLoss) return false;
    g_simulateLoss = false;
    g_simulationTaken = true;
    ei::Log("render: the device's loss asked for (render::LoseDevice): taken as DXGI_ERROR_DEVICE_REMOVED");
    return true;
}

void DeviceLossCounts(WindowPresentInfo* info) {
    info->deviceLosses = g_losses;
    info->deviceRecoveries = g_recoveries;
    info->lastLossResult = g_lastLossResult;
    info->lastLossReason = g_lastLossReason;
    info->lastLossSimulated = g_lastLossSimulated;
#if defined(EI_RENDER_VALIDATION)
    info->removable = g_d3d12 != nullptr;
    info->removals = g_removals;
#endif
}

} // namespace detail

void LoseDevice() {
    if (!g_started) return;
    g_simulateLoss = true;
}

#if defined(EI_RENDER_VALIDATION)
bool RemoveDevice() {
    if (!g_started || !g_d3d12) return false;
    ID3D12Device5* device5 = nullptr;
    if (FAILED(g_d3d12->QueryInterface(IID_PPV_ARGS(&device5)))) {
        ei::Log("render: RemoveDevice: no ID3D12Device5 (Windows 10 1809 or later)");
        return false;
    }
    device5->Release();
    g_removeAsked = true;
    return true;
}

namespace detail {

void RemoveIfAsked() {
    if (!g_removeAsked || !g_d3d12) return;
    g_removeAsked = false;
    ID3D12Device5* device5 = nullptr;
    if (FAILED(g_d3d12->QueryInterface(IID_PPV_ARGS(&device5)))) return;
    FpuGuard fpu;
    // The GPU finishes what it was given first. Removed with work in flight, the old device's teardown
    // (RecreateDevice) crashed in a thread of D3D11On12 / DXGI / the driver, also in a small program that
    // presented on its own window; with the GPU idle it never did (renderer.md).
    int waited = 0;
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
    ID3D11Query* idle = nullptr;
    if (SUCCEEDED(g_device->CreateQuery(&desc, &idle))) {
        g_context->End(idle);
        g_context->Flush();
        BOOL done = FALSE;
        while (g_context->GetData(idle, &done, sizeof(done), 0) == S_FALSE && waited < 2000) {
            Sleep(1);
            ++waited;
        }
        idle->Release();
    }
    device5->RemoveDevice();
    device5->Release();
    ++g_removals;
    ei::Log("render: the removable device removed (ID3D12Device5::RemoveDevice, the GPU idle after %d ms); its "
            "GetDeviceRemovedReason now %08lX",
            waited, static_cast<unsigned long>(g_device->GetDeviceRemovedReason()));
}

} // namespace detail
#endif

namespace detail {

void Fatal(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("render: fatal: %s", text);
    app::ShowMessageBox(text, MB_ICONHAND);
    std::exit(-1);
}

// The game's main-thread x87 state is part of its behaviour (docs/notes/rebirth.md, "FPU precision");
// drivers are not expected to change it, but the control word is put back after every call into them anyway
// (loaded only when it changed).
FpuGuard::FpuGuard() : saved_(x87::ControlWord()) {}
FpuGuard::~FpuGuard() {
    if (x87::ControlWord() != saved_) x87::SetControlWord(saved_);
}

Frame g_frame;
std::uint32_t FrameNumber() { return g_frameNumber; }
std::uint32_t CommitNumber() { return g_commits; }

ID3D11Device* D3DDevice() { return g_device; }
ID3D11DeviceContext* D3DContext() { return g_context; }
sg_image FrameImage() { return g_colour; }
sg_view FrameTexture() { return g_colourTexture; }
bool FrameSize(int* width, int* height) {
    *width = g_width;
    *height = g_height;
    return g_colour.id != 0;
}

CTextureDevice* DeviceObject() { return g_sceneRenderer->device_; }

// The size of the game's screen (the device object's).
bool GameTargetSize(int* width, int* height) {
    const CTextureDevice* device = DeviceObject();
    if (!device) return false;
    *width = device->screen_.right;
    *height = device->screen_.bottom;
    return *width > 0 && *height > 0 && *width <= 16384 && *height <= 16384;
}

int AddMatrix(const Matrix4& m) {
    if (!g_frame.matrices.empty() && std::memcmp(&g_frame.matrices.back(), &m, sizeof(Matrix4)) == 0)
        return static_cast<int>(g_frame.matrices.size()) - 1;
    g_frame.matrices.push_back(m);
    return static_cast<int>(g_frame.matrices.size()) - 1;
}

// The game's fog switch and colour, once per frame.
void ReadFog(Frame& f) {
    f.haveFog = true;
    f.fogEnabled = g_renderState.fog;
    f.fogColour = g_renderState.fogColour;
}

} // namespace detail

#if defined(EI_RENDER_VALIDATION)
namespace detail {
bool ReadBackDue(long count) { return g_checkAll || count < kReadBackAlways || count % 16 == 0; }
} // namespace detail
#endif

void Configure(const char* spec) {
    std::string combined = spec ? spec : "";
    std::string environment = EnvironmentSpec();
    if (!environment.empty()) combined += (combined.empty() ? "" : ",") + environment;
    ParseSpec(combined.c_str());
}

void PrepareDevice() {
    EnsureConfigured();
    if (g_started || g_prepared.thread) return;
#if defined(EI_RENDER_VALIDATION)
    if (g_removable) return;   // made at the first use (CreateRemovableDevice)
#endif
    g_prepared.thread = CreateThread(nullptr, 0, &PrepareDeviceThread, nullptr, 0, nullptr);
}

void BeginDeviceMeshTime() {
#if defined(EI_RENDER_VALIDATION)
    if (g_deviceMeshTiming) return;
    new (g_deviceMeshTimer) detail::StatsTimer(&FrameStats::deviceMeshMs);
    g_deviceMeshTiming = true;
#endif
}
void EndDeviceMeshTime() {
#if defined(EI_RENDER_VALIDATION)
    if (!g_deviceMeshTiming) return;
    std::launder(reinterpret_cast<detail::StatsTimer*>(g_deviceMeshTimer))->~StatsTimer();
    g_deviceMeshTiming = false;
#endif
}
void BeginDecalTime(bool shadow) {
#if defined(EI_RENDER_VALIDATION)
    if (g_decalTimer.running) return;
    ++(shadow ? detail::g_stats.decalShadows : detail::g_stats.decalPrints);
    g_decalTimer.Begin(shadow ? &FrameStats::decalShadowMs : &FrameStats::decalPrintMs);
#else
    (void)shadow;
#endif
}
void EndDecalTime(int vertices) {
#if defined(EI_RENDER_VALIDATION)
    if (g_decalTimer.End()) detail::g_stats.decalVertices += static_cast<std::uint64_t>(vertices);
#else
    (void)vertices;
#endif
}
void BeginFlatDrawTime(int vertices) {
#if defined(EI_RENDER_VALIDATION)
    if (g_flatDrawTimer.running) return;
    ++detail::g_stats.flatDraws;
    detail::g_stats.flatDrawVertices += static_cast<std::uint64_t>(vertices);
    g_flatDrawTimer.Begin(&FrameStats::flatMs);
#else
    (void)vertices;
#endif
}
void EndFlatDrawTime() {
#if defined(EI_RENDER_VALIDATION)
    g_flatDrawTimer.End();
#endif
}
void BeginShadowBuildTime() {
#if defined(EI_RENDER_VALIDATION)
    if (g_shadowBuildTimer.running) return;
    ++detail::g_stats.shadowBuilds;
    g_shadowBuildTimer.Begin(&FrameStats::shadowBuildMs);
#endif
}
void EndShadowBuildTime() {
#if defined(EI_RENDER_VALIDATION)
    g_shadowBuildTimer.End();
#endif
}

void StartPresenting() { Start(); }

void SetSmooth(bool on) {
    EnsureConfigured();
    if (on != g_smooth) ei::Log("render: the smooth option turned %s", on ? "on" : "off");
    g_smooth = on;
}

bool Smooth() {
    EnsureConfigured();
    return g_smooth;
}

void SetSmoothCamera(bool on) {
    EnsureConfigured();
    if (on != g_smoothCamera) ei::Log("render: the smooth camera turned %s", on ? "on" : "off");
    g_smoothCamera = on;
}

bool SmoothCamera() {
    EnsureConfigured();
    return g_smoothCamera;
}

void BeginWorld() {
    std::vector<Frame::Range>& ranges = g_frame.worldRanges;
    if (!ranges.empty() && ranges.back().end == SIZE_MAX) return;
    ranges.push_back(Frame::Range{g_frame.commands.size(), SIZE_MAX});
}

void EndWorld() {
    std::vector<Frame::Range>& ranges = g_frame.worldRanges;
    if (!ranges.empty() && ranges.back().end == SIZE_MAX) ranges.back().end = g_frame.commands.size();
}

void ClearDepth(float z, const Rect* rects, int count) {
    g_stats.depthClears += rects ? static_cast<std::uint64_t>(count > 0 ? count : 0) : 1;
    if (!rects) {
        g_frame.clears.push_back(ClearBatch{z, Rect{0, 0, 0, 0}});
        g_frame.commands.push_back(Command{Command::ClearDepth, static_cast<int>(g_frame.clears.size()) - 1});
        return;
    }
    for (int i = 0; i < count; ++i) {
        g_frame.clears.push_back(ClearBatch{z, rects[i]});
        g_frame.commands.push_back(Command{Command::ClearDepth, static_cast<int>(g_frame.clears.size()) - 1});
    }
}

// --- Frame statistics ----------------------------------------------------------------------------

namespace detail {

FrameStats g_stats;   // the frame being built

StatsTimer::StatsTimer(double FrameStats::* field) : field_(field), start_(0), outer_(nullptr) {
#if defined(EI_RENDER_VALIDATION)
    start_ = Ticks();
    outer_ = g_innerTimer;
    g_innerTimer = this;
#endif
}

StatsTimer::~StatsTimer() {
#if defined(EI_RENDER_VALIDATION)
    if (g_msPerTick == 0.0) {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        g_msPerTick = frequency.QuadPart > 0 ? 1000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
    }
    double ms = static_cast<double>(Ticks() - start_) * g_msPerTick;
    g_stats.*field_ += ms;
    if (outer_) g_stats.*(outer_->field_) -= ms;   // the outer timer counts its own work only
    g_innerTimer = outer_;
#endif
}

void CountPipeline(sg_pipeline pipeline) {
    ++g_stats.pipelineApplies;
    if (pipeline.id != g_lastPipeline) ++g_stats.pipelineChanges;
    g_lastPipeline = pipeline.id;
}

void CountBindings(const sg_bindings& b) {
    ++g_stats.bindings;
    if (b.views[0].id != g_lastTexture) ++g_stats.textureChanges;
    g_lastTexture = b.views[0].id;
}

void CountDraw(std::size_t uniformBytes) {
    ++g_stats.draws;
    g_stats.uniformBytes += uniformBytes;
}

void CountReuse([[maybe_unused]] bool terrain, [[maybe_unused]] const void* key, [[maybe_unused]] int page,
                [[maybe_unused]] const Vertex* vertices, [[maybe_unused]] std::size_t count) {
#if defined(EI_RENDER_VALIDATION)
    StatsTimer timer(&FrameStats::statsMs);
    VertexHashes h{0xCBF29CE484222325ull, 0xCBF29CE484222325ull};
    for (std::size_t i = 0; i < count; ++i) {
        const Vertex& v = vertices[i];
        h.geometry = Fnv(h.geometry, &v.position, sizeof(v.position));
        h.geometry = Fnv(h.geometry, &v.u, 2 * sizeof(float));
        h.colours = Fnv(h.colours, &v.colour, 2 * sizeof(std::uint32_t));
    }
    std::uint64_t id = (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key)) << 8) ^
                       static_cast<std::uint64_t>(page) ^ (terrain ? 1ull << 63 : 0);
    auto last = g_lastHashes.find(id);
    FrameStats::Reuse r = last == g_lastHashes.end()            ? FrameStats::New
                          : last->second.geometry != h.geometry ? FrameStats::Moved
                          : last->second.colours != h.colours   ? FrameStats::Relit
                                                                : FrameStats::Same;
    ++(terrain ? g_stats.terrainReuse : g_stats.meshReuse)[r];
    g_hashes[id] = h;
    if (!terrain && !g_frameGeometry.insert(h.geometry ^ count).second) ++g_stats.sharedMeshes;
#endif
}

void CountUpload(FrameStats::Upload kind, std::size_t bytes) {
    ++g_stats.uploads[kind];
    g_stats.uploadBytes[kind] += bytes;
}

} // namespace detail

FrameStats TakeFrameStats() {
    FrameStats s = g_statsSum;
    g_statsSum = FrameStats{};
    return s;
}

namespace detail {

void SplitFrame() {
    if (!g_started || g_frameDrawn || BetweenFrames()) return;
    FpuGuard fpu;
    DrawPendingAlone();
}

void RunCanvasesNow() {
    Start();
    FpuGuard fpu;
    BeginWindowPresent();
    RunCanvasCommands();
    EndWindowPresent(false);
}

GpuWorkAlone::GpuWorkAlone() { BeginWindowPresent(); }

GpuWorkAlone::~GpuWorkAlone() { EndWindowPresent(false); }

} // namespace detail

void EndFrame() {
    if (g_frameDrawn) return;
    Start();
    g_frameDrawn = true;
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::endFrameMs);
    perf::CpuTimer submitTime(perf::Cpu::Submit);
    // Onto the game window through the renderer's swap chain, in turn with the cursor thread's presents, which
    // draw the frame as it is (the present draws the cursor over the frame).
    BeginWindowPresent();
    if (!DrawPending()) {
        EndWindowPresent(false);
        return;
    }
    perf::Stamp(perf::Mark::FrameEnd);
    bool toWindow = DrawToWindow(g_frame.haveCursor ? &g_frame.cursor : nullptr);
    perf::Stamp(perf::Mark::PresentEnd);
    g_frame.haveCursor = false;
    ShadowFrameEnd();
    perf::EndGpuFrame();
    sg_commit();
    submitTime.Stop();   // the present's wait for the display is not the renderer's work
    ++g_commits;
    if (toWindow) PresentWindow();
    EndWindowPresent(toWindow && !g_deviceLost);
    g_haveFrame = true;
}

void Present() {
    EndFrame();
    perf::FrameDone(g_stats.draws, g_stats.shadowDraws);
    AddFrameStats();
    g_drawnCommands = 0;
    g_passes = 0;
    g_frame.matrices.clear();
    g_frame.terrainPieces.clear();
    g_frame.terrainBatches.clear();
    TerrainFrameDone();
    MeshCacheFrameDone();
    g_frame.meshVertices.clear();
    g_frame.meshIndices.clear();
    g_frame.meshBatches.clear();
    g_frame.meshLights.clear();
    g_frame.blitBatches.clear();
    g_frame.clears.clear();
    g_frame.fills.clear();
    g_frame.canvasBatches.clear();
    g_frame.commands.clear();
    g_frame.worldRanges.clear();
    g_frame.shadow.haveSun = false;
    g_frame.shadow.casters.clear();
    g_frame.shadow.prepared = false;
    g_frame.shadow.on = false;
    g_frame.shadow.lights.clear();
    ShadowFrameDone();
    g_frame.haveCursor = false;
    g_frame.haveFog = false;
    g_frameDrawn = false;
    g_holder = Holder::None;
    g_lastCounts = g_counts;
    g_counts = SmoothInfo{};
    if (g_started) {
        FpuGuard fpu;
        RetireTextures();
        RetireBuffers();
        RetireFrameImages();
        CheckDevice();
    }
    ++g_frameNumber;
}

namespace detail {
int PassSampleCount() { return g_passSamples; }
} // namespace detail

#if defined(EI_RENDER_VALIDATION)
void SetSmoothOrder(SmoothOrder order) {
    static const char* const kNames[] = {"as the game draws", "an empty pass on the frame target before the world",
                                         "the world split in two at its middle command"};
    if (order != g_smoothOrder) ei::Log("render: the smooth option's passes: %s", kNames[static_cast<int>(order)]);
    g_smoothOrder = order;
}
#endif

SmoothInfo SmoothState() {
    SmoothInfo info = g_lastCounts;
    info.on = Smooth();
    info.deviceSamples = g_deviceSamples;
    info.targetSamples = g_targetSamples;
    return info;
}

bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr) {
    if (!g_started || !g_haveFrame || !g_colour.id) return false;
    FpuGuard fpu;
    StagingCopy copy;
    bool ok = copy.Map(g_context, D3DTexture(g_colour));
    if (ok) {
        const unsigned w = copy.Width(), h = copy.Height();
        *width = static_cast<int>(w);
        *height = static_cast<int>(h);
        bgr->resize(static_cast<std::size_t>(w) * h * 3);
        for (unsigned y = 0; y < h; ++y) {
            const std::uint8_t* row = copy.Row(y);
            std::uint8_t* out = bgr->data() + static_cast<std::size_t>(y) * w * 3;
            for (unsigned x = 0; x < w; ++x) {
                out[x * 3 + 0] = row[x * 4 + 2];
                out[x * 3 + 1] = row[x * 4 + 1];
                out[x * 3 + 2] = row[x * 4 + 0];
            }
        }
    }
    copy.Release();
    sg_reset_state_cache();
    return ok;
}

} // namespace render
