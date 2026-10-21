// The new renderer's frame: the switch, the Direct3D 11 device behind sokol_gfx, the off-screen
// target the frame is drawn into (the game's resolution) and reading it back for captures. See render.h and docs/notes/renderer.md.
#include "render.h"
#include "render_game.h"
#include "render_internal.h"

#include "../hook/log.h"
#include "ei/hook.h"   // ei::Standalone
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

#include "../game/device_switchover.h"   // device::SwitchOverRenderState
#include "../game/texture.h"   // CTextureDevice, CDXSurface

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
bool g_failed = false;   // device or pipelines could not be created, or sokol panicked: nothing more is drawn
bool g_presentsItself = false;   // StartPresenting succeeded: the game has no DirectDraw device
// The smooth option (R12a, off by default): EI_RENDER's "smooth" (ParseSpec), render::SetSmooth.
bool g_smooth = false;
// The classic shadows (R16j, off by default): EI_RENDER's "classicshadows", render::SetClassicShadows.
bool g_classicShadows = false;
#if defined(EI_RENDER_VALIDATION)
bool g_removable = false;   // "removable" (harness build only): the device is made through D3D11On12 (R9c5)
#endif

// The words of EI_RENDER. "new", "present" and "cpuwindows" (the old switch, still named by run_all
// and the scenarios) change nothing: the image always draws with the new renderer. "smooth" turns
// the smooth option on (render::SetSmooth), "classicshadows" the game's shadow decals instead of the
// shadow map (render::SetClassicShadows, R16j).
void ParseSpec(const char* spec) {
    g_smooth = false;
    bool classicShadows = false;
#if defined(EI_RENDER_VALIDATION)
    g_removable = false;
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
        else if (word == "classicshadows") classicShadows = true;
#if defined(EI_RENDER_VALIDATION)
        else if (word == "removable") g_removable = true;
#endif
        else if (!word.empty()) ei::Log("render: unknown option '%s' in EI_RENDER", word.c_str());
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (classicShadows != g_classicShadows) ei::Log("render: shadows: %s", classicShadows ? "classic" : "the sun's shadow map");
    g_classicShadows = classicShadows;
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

HWND GameWindow() { return *reinterpret_cast<HWND*>(game::Where().window); }

// The renderer's clear colour (007AF060 +0x14, D3DCOLOR), as 00501A40 clears the frame with.
std::uint32_t GameClearColour() { return *reinterpret_cast<std::uint32_t*>(game::Where().renderer + 0x14); }

sg_color FromD3DColour(std::uint32_t c) {
    return sg_color{static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                    static_cast<float>(c & 0xFF) / 255.0f, 1.0f};
}

// --- Device ----------------------------------------------------------------------------------------

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
    // sokol cannot go on after a panic: the new renderer switches itself off (nothing more is drawn).
    if (level == 0) g_failed = true;
}

#if defined(EI_RENDER_VALIDATION)
// --- A device the harness can remove (R9c5) --------------------------------------------------------------
// With "removable" in EI_RENDER (harness build only) the Direct3D 11 device is made through
// D3D11On12CreateDevice on a Direct3D 12 device of the default adapter, so that
// ID3D12Device5::RemoveDevice (render::RemoveDevice) can remove it for real: the swap chain's
// Present and GetDeviceRemovedReason report DXGI_ERROR_DEVICE_REMOVED as after a driver reset, in
// this process only (no driver reset; other processes keep their devices).

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
    HRESULT hr = create12(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&g_d3d12));
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
    hr = g_d3d12->CreateCommandQueue(&queue, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&g_queue));
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

// The prepared device, waited for; false when none was being made.
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

bool CreateDevice() {
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = S_OK;
#if defined(EI_RENDER_VALIDATION)
    if (g_removable) hr = CreateRemovableDevice(kLevels, kLevelCount, &got);
    else
#endif
        if (!TakePreparedDevice(&hr, &got)) hr = CreatePlainDevice(&g_device, &g_context, &got);
    if (FAILED(hr)) {
        ei::Log("render: D3D11CreateDevice failed (%08lX); the new renderer stays off", static_cast<unsigned long>(hr));
        return false;
    }
    sg_desc desc{};
    desc.environment.defaults.color_format = kColourFormat;
    desc.environment.defaults.depth_format = kDepthFormat;
    desc.environment.defaults.sample_count = 1;
    desc.environment.d3d11.device = g_device;
    desc.environment.d3d11.device_context = g_context;
    desc.logger.func = &SokolLog;
    // The game's textures (terrain pages, every model's, the interface's, one shadow texture per
    // object), window surfaces and the images replaced within a frame: far more than sokol's
    // default pools (128 images, 256 views); a full pool made sg_make_image fail.
    desc.image_pool_size = 16384;
    desc.view_pool_size = 16384;
    desc.buffer_pool_size = 512;
    desc.pipeline_pool_size = 1024;
    desc.sampler_pool_size = 256;
    sg_setup(&desc);
    if (!sg_isvalid()) {
        ei::Log("render: sg_setup failed; the new renderer stays off");
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

// The multisampled colour and depth of `samples` (the frame target made): false when the device
// refused them.
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

Frame g_frame;
bool g_frameDrawn = false;
bool g_started = false;
std::uint32_t g_frameNumber = 0;
std::uint32_t g_commits = 0;

bool EnsureStarted() {
    EnsureConfigured();
    if (!Active()) return false;
    if (g_started) return true;
    FpuGuard fpu;
    if (!CreateDevice() || !CreateTerrainShader() || !CreateMeshShader() || !CreateBlitPipeline()) {
        g_failed = true;
        return false;
    }
    g_started = true;
    return true;
}

// --- A lost device -------------------------------------------------------------------------------------

bool g_deviceLost = false;     // seen (a DXGI error, GetDeviceRemovedReason): made again at the present
bool g_simulateLoss = false;   // render::LoseDevice
bool g_simulationTaken = false;   // the loss being reported is LoseDevice's
int g_losses = 0, g_recoveries = 0;
long g_lastLossResult = 0, g_lastLossReason = 0;   // what reported the last loss, GetDeviceRemovedReason then
bool g_lastLossSimulated = false;
ID3D11Texture2D* g_staging = nullptr;   // the frame's readback copy (MapTarget, below)
UINT g_stagingWidth = 0, g_stagingHeight = 0;

// Everything made on the lost device is released (sokol's resources through each module, then
// sg_shutdown), the device and context with them; a new device gets sokol again and the pipelines.
// The frame target, the window's swap chain, window surfaces and textures are made again as the
// next frames ask for them: from the game's memory, or (the switch-over's .mmp textures) from the
// file levels kept for them. False when no new device could be made: the new renderer is off.
bool RecreateDevice() {
    FpuGuard fpu;
    ++g_losses;
    std::size_t kept = 0, keptBytes = 0;
    DropCursorPresents();
    DropWindow();
    DestroyTarget();
    if (g_staging) g_staging->Release();
    g_staging = nullptr;
    g_stagingWidth = g_stagingHeight = 0;
    DestroyTerrain();
    DestroyMeshes();
    DropBlits();
    DestroySmooth();
    DestroyShadows();
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
    if (!made || !CreateTerrainShader() || !CreateMeshShader() || !CreateBlitPipeline()) {
        ei::Log("render: no new Direct3D 11 device; the new renderer is off");
        g_failed = true;
        return false;
    }
    ++g_recoveries;
    g_deviceLost = false;
    return true;
}

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
    if (!Active() || !g_started) return;
    g_simulateLoss = true;
}

#if defined(EI_RENDER_VALIDATION)
bool RemoveDevice() {
    if (!Active() || !g_started || !g_d3d12) return false;
    ID3D12Device5* device5 = nullptr;
    if (FAILED(g_d3d12->QueryInterface(__uuidof(ID3D12Device5), reinterpret_cast<void**>(&device5)))) {
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
    if (FAILED(g_d3d12->QueryInterface(__uuidof(ID3D12Device5), reinterpret_cast<void**>(&device5)))) return;
    FpuGuard fpu;
    // The GPU finishes what it was given first. Removed with work in flight, the old device's
    // teardown (RecreateDevice) crashed in a thread of D3D11On12 / DXGI / the driver (a null fence in
    // d3d11on12.dll, an access violation in the driver's user-mode part), also in a small program that
    // presented on its own window; with the GPU idle it never did (renderer.md, R9c5).
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

namespace {

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

} // namespace

namespace detail {

// The game's main-thread x87 state is part of its behaviour (docs/notes/rebirth.md, "FPU
// precision"); drivers are not expected to change it, but the control word is put back after every
// call into them anyway. The FPU's word as it holds it (every bit __control87_2 kept), loaded only when
// it changed (SV10i).
FpuGuard::FpuGuard() : saved_(x87::ControlWord()) {}
FpuGuard::~FpuGuard() {
    if (x87::ControlWord() != saved_) x87::SetControlWord(saved_);
}

Frame& CurrentFrame() { return g_frame; }
std::uint32_t FrameNumber() { return g_frameNumber; }
std::uint32_t CommitNumber() { return g_commits; }
bool Started() { return EnsureStarted(); }

// For the game window's swap chain (render_window.cpp).
ID3D11Device* D3DDevice() { return g_device; }
ID3D11DeviceContext* D3DContext() { return g_context; }
sg_image FrameImage() { return g_colour; }
sg_view FrameTexture() { return g_colourTexture; }
bool FrameSize(int* width, int* height) {
    *width = g_width;
    *height = g_height;
    return g_colour.id != 0;
}

CTextureDevice* DeviceObject() { return *reinterpret_cast<CTextureDevice**>(game::Where().deviceObject); }

// The size of the game's render target (in the switch-over a holder without a surface that still
// has the screen's size).
bool GameTargetSize(int* width, int* height) {
    CTextureDevice* device = DeviceObject();
    const CDXSurface* target = device ? device->renderTarget_ : nullptr;
    if (!target) return false;
    *width = target->width;
    *height = target->height;
    return *width > 0 && *height > 0 && *width <= 16384 && *height <= 16384;
}

int AddMatrix(const Matrix4& m) {
    if (!g_frame.matrices.empty() && std::memcmp(&g_frame.matrices.back(), &m, sizeof(Matrix4)) == 0)
        return static_cast<int>(g_frame.matrices.size()) - 1;
    g_frame.matrices.push_back(m);
    return static_cast<int>(g_frame.matrices.size()) - 1;
}

// The stand-in device's states (device_switchover.h): what the game last set, Direct3D 7's defaults
// before that.
void OldRenderStates(const std::uint32_t* states, std::uint32_t* values, int count) {
    for (int i = 0; i < count; ++i) values[i] = device::SwitchOverRenderState(states[i]);
}

void OldStageStates(int stage, const std::uint32_t* states, std::uint32_t* values, int count) {
    for (int i = 0; i < count; ++i) values[i] = device::SwitchOverStageState(static_cast<std::uint32_t>(stage), states[i]);
}

// The fog the old path draws with: FOGENABLE (28) and FOGCOLOR (34), once per frame.
void ReadFog(Frame& f) {
    const std::uint32_t states[2] = {28, 34};
    std::uint32_t values[2];
    RenderValues(states, values, 2);
    f.haveFog = true;
    f.fogEnabled = values[0] != 0;
    f.fogColour = values[1];
}

} // namespace detail

namespace game {
static Addresses Resolved() {
    Addresses a;
    a.window = ei::GameAddress(a.window);
    a.renderer = ei::GameAddress(a.renderer);
    a.deviceObject = ei::GameAddress(a.deviceObject);
    a.deviceLock = ei::GameAddress(a.deviceLock);
    return a;
}

Addresses& Where() {
    static Addresses addresses = Resolved();
    return addresses;
}
} // namespace game

// The step's words, then EI_RENDER's.
void Configure(const char* spec) {
    std::string combined = spec ? spec : "";
    std::string environment = EnvironmentSpec();
    if (!environment.empty()) combined += (combined.empty() ? "" : ",") + environment;
    ParseSpec(combined.c_str());
}

void PrepareDevice() {
    EnsureConfigured();
    if (!Active() || g_started || g_prepared.thread) return;
#if defined(EI_RENDER_VALIDATION)
    if (g_removable) return;   // made at the first use (CreateRemovableDevice)
#endif
    g_prepared.thread = CreateThread(nullptr, 0, &PrepareDeviceThread, nullptr, 0, nullptr);
}

// game.exe (the harness and the vector recorders in the original's process) never runs the new
// renderer: its frames are the original's.
bool Active() { return ei::Standalone() && !g_failed; }

namespace {
bool g_meshCornersFull = false;   // the harness's "meshcorners full" (R16k)
#if defined(EI_RENDER_VALIDATION)
// The timer of the device mesh draw running (at most one: they do not nest), made in place so that
// no static destructor or heap block comes with it.
alignas(detail::StatsTimer) unsigned char g_deviceMeshTimer[sizeof(detail::StatsTimer)];
bool g_deviceMeshTiming = false;
#endif
} // namespace
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
bool MeshCornersSkipped() { return Active() && !g_meshCornersFull; }
void SetMeshCornersFull(bool full) {
#if defined(EI_RENDER_VALIDATION)
    if (full != g_meshCornersFull)
        ei::Log("render: the device's mesh draws do %s per-corner work", full ? "all the old" : "only the read");
    g_meshCornersFull = full;
#else
    (void)full;
#endif
}

bool StartPresenting() {
    if (g_presentsItself) return true;
    if (!Active() || !EnsureStarted()) return false;
    g_presentsItself = true;
    ei::Log("render: the switch-over without DirectDraw: the new renderer presents the frames itself");
    return true;
}

bool PresentsItself() { return g_presentsItself; }

void TexturePixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                   std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask) {
    if (!holder || !pixels || !Active() || !EnsureStarted()) return;
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::textureMs);
    TextureFromPixels(holder, pixels, width, height, pitch, bitsPerPixel, redMask, greenMask, blueMask, alphaMask);
}

void SetSmooth(bool on) {
    EnsureConfigured();
    if (on != g_smooth) ei::Log("render: the smooth option turned %s", on ? "on" : "off");
    g_smooth = on;
}

bool Smooth() {
    EnsureConfigured();
    return g_smooth;
}

void SetClassicShadows(bool on) {
    EnsureConfigured();
    if (on != g_classicShadows) ei::Log("render: shadows: %s", on ? "classic (the game's decals)" : "the sun's shadow map");
    g_classicShadows = on;
}

bool ClassicShadows() {
    EnsureConfigured();
    return g_classicShadows;
}

void BeginWorld() {
    std::vector<Frame::Range>& ranges = g_frame.worldRanges;
    if (!Active() || (!ranges.empty() && ranges.back().end == SIZE_MAX)) return;
    ranges.push_back(Frame::Range{g_frame.commands.size(), SIZE_MAX});
}

void EndWorld() {
    std::vector<Frame::Range>& ranges = g_frame.worldRanges;
    if (!ranges.empty() && ranges.back().end == SIZE_MAX) ranges.back().end = g_frame.commands.size();
}

void ClearDepth(float z, const Rect* rects, int count) {
    if (!Active()) return;
    Stats().depthClears += rects ? static_cast<std::uint64_t>(count > 0 ? count : 0) : 1;
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

// --- Frame statistics (R16) ----------------------------------------------------------------------------

namespace {
FrameStats g_stats;         // the frame being built
FrameStats g_statsSum;      // the presented frames since the last TakeFrameStats
std::uint32_t g_lastPipeline = 0, g_lastTexture = 0;   // ids applied last in this frame
#if defined(EI_RENDER_VALIDATION)
// The times and the reuse counts are the harness build's (the product counts only).
StatsTimer* g_innerTimer = nullptr;
// CountReuse: per key the hashes of the vertices' geometry (positions, texture coordinates) and
// colours, this frame's and the last; this frame's mesh geometry hashes.
struct VertexHashes {
    std::uint64_t geometry, colours;
};
std::unordered_map<std::uint64_t, VertexHashes> g_hashes, g_lastHashes;
std::unordered_set<std::uint64_t> g_frameGeometry;

// FNV-1a over 32-bit words (every field hashed is one or more of them).
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

// The frame's counts into the sums; a new frame's counts begin.
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
                       &FrameStats::textureChanges, &FrameStats::comparedBytes, &FrameStats::morphMeshes,
                       &FrameStats::morphCorners})
        s.*field += f.*field;
    for (int k = 0; k < FrameStats::UploadKinds; ++k) {
        s.uploads[k] += f.uploads[k];
        s.uploadBytes[k] += f.uploadBytes[k];
        if (f.largestUpload[k] > s.largestUpload[k]) s.largestUpload[k] = f.largestUpload[k];
    }
    for (int k = 0; k < FrameStats::ReuseKinds; ++k) {
        s.terrainReuse[k] += f.terrainReuse[k];
        s.meshReuse[k] += f.meshReuse[k];
    }
    s.sharedMeshes += f.sharedMeshes;
    s.handOverMs += f.handOverMs;
    s.terrainMs += f.terrainMs;
    s.textureMs += f.textureMs;
    s.windowMs += f.windowMs;
    s.endFrameMs += f.endFrameMs;
    s.statsMs += f.statsMs;
    s.deviceMeshMs += f.deviceMeshMs;
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
} // namespace

namespace detail {

FrameStats& Stats() { return g_stats; }

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
    if (bytes > g_stats.largestUpload[kind]) g_stats.largestUpload[kind] = bytes;
}

} // namespace detail

FrameStats TakeFrameStats() {
    FrameStats s = g_statsSum;
    g_statsSum = FrameStats{};
    return s;
}

namespace {

std::size_t g_drawnCommands = 0;   // commands of this frame already drawn into the target
int g_passes = 0;                  // passes drawn this frame
int g_passSamples = 1;             // the sample count of the pass being drawn (PassSampleCount)

// Which target holds the frame's latest pixels: none yet (the frame's first pass clears), the frame
// target, or the multisampled one (its colour also resolved into the frame target).
enum class Holder : std::uint8_t { None, Frame, Multisampled };
Holder g_holder = Holder::None;

// The smooth option's counts for this frame and the last presented one (render::SmoothState).
SmoothInfo g_counts, g_lastCounts;

// Command `k` belongs to the world view (render::BeginWorld .. EndWorld).
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
    ++Stats().passes;
    g_passSamples = 1;
    if (afterWorld) CopyDepthFromMultisampled(g_multiDepthTexture);
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
        ++Stats().passes;
        g_passSamples = g_targetSamples;
        CopyIntoMultisampled(g_colourTexture, g_depthTexture);
        sg_end_pass();
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
    ++Stats().passes;
    g_passSamples = g_targetSamples;
    g_holder = Holder::Multisampled;
    ++g_counts.multisampledPasses;
}

// Commands [from, to) into the pass begun.
void DrawCommands(std::size_t from, std::size_t to, int width, int height) {
    for (std::size_t k = from; k < to; ++k) {
        const Command& c = g_frame.commands[k];
        auto i = static_cast<std::size_t>(c.index);
        switch (c.kind) {
        case Command::Terrain: DrawTerrainBatch(g_frame, g_frame.terrainBatches[i]); break;
        case Command::Mesh: {
            const MeshBatch& batch = g_frame.meshBatches[i];
            // The smooth option: the world's models (figures, map objects, the sky, shadows) sampled
            // anisotropically; the interface's sprites and previews and the pre-transformed draws as
            // the stage says.
            bool anisotropic = g_smooth && batch.model && InWorld(k);
            if (batch.model) ++(anisotropic ? g_counts.anisotropicModels : g_counts.plainModels);
            DrawMeshBatch(g_frame, batch, anisotropic);
            break;
        }
        case Command::Blit: DrawBlitBatch(g_frame, g_frame.blitBatches[i], width, height); break;
        case Command::ClearDepth: DrawClearDepth(g_frame.clears[i], width, height); break;
        }
    }
}

// Draws the frame's commands not drawn yet into the off-screen target: one pass, the frame's first
// clearing it. A frame is drawn in more than one pass when a copy of it is taken in its middle
// (CopyFrameTo), and with the smooth option one pass per run of world and other commands (the
// world's into the multisampled target).
bool DrawPending() {
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height) || !EnsureTarget(width, height, g_passes == 0)) return false;
    if (g_passes > 0 && g_drawnCommands == g_frame.commands.size()) return true;
    UploadTerrain(g_frame);
    UploadMeshes(g_frame);
    // The sun's shadow map, once a frame before its first pass (R16j; the classic option: none).
    if (!g_frame.shadow.prepared) {
        PrepareShadows(g_frame);
        DrawShadowMap(g_frame);
    }
    // Direct3D 7 puts pixel centres at integer screen coordinates, Direct3D 11 half a pixel further:
    // the 3D draws move their positions by half a pixel (Frame::pixelShift). (A viewport moved by
    // half a pixel would do the same, but sokol takes whole pixels.)
    g_frame.pixelShift[0] = 1.0f / static_cast<float>(width);
    g_frame.pixelShift[1] = -1.0f / static_cast<float>(height);
    std::size_t k = g_drawnCommands, end = g_frame.commands.size();
    bool multisampling = g_targetSamples > 1;
    do {
        bool world = multisampling && k < end && InWorld(k);
        std::size_t stop = k;
        while (stop < end && (multisampling && InWorld(stop)) == world) ++stop;
        if (world) BeginMultisampledPass();
        else BeginFramePass();
        DrawCommands(k, stop, width, height);
        sg_end_pass();
        g_passSamples = 1;
        ++g_passes;
        k = stop;
    } while (k < end);
    g_drawnCommands = end;
    return true;
}

// The software cursor kept out of the frame's commands (render::Cursor when the new renderer
// presents itself) drawn over it: the frame's last draw.
void DrawCursor() {
    if (!g_frame.haveCursor) return;
    g_frame.haveCursor = false;
    g_frame.blitBatches.push_back(g_frame.cursor);
    g_frame.commands.push_back(Command{Command::Blit, static_cast<int>(g_frame.blitBatches.size()) - 1});
    DrawPending();
}

// A staging copy of the off-screen target's `box`, mapped for reading: `rows` and `pitch` until
// UnmapTarget. The staging texture (g_staging) is kept (the full frame is read every frame with
// "present").

bool MapTarget(const D3D11_BOX& box, const std::uint8_t** rows, UINT* pitch) {
    sg_d3d11_image_info info = sg_d3d11_query_image_info(g_colour);
    auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(info.tex2d));
    if (!texture) return false;
    UINT width = box.right - box.left, height = box.bottom - box.top;
    if (!g_staging || g_stagingWidth < width || g_stagingHeight < height) {
        if (g_staging) g_staging->Release();
        g_staging = nullptr;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Width = width > desc.Width ? width : desc.Width;
        desc.Height = height > desc.Height ? height : desc.Height;
        desc.MipLevels = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &g_staging))) {
            g_staging = nullptr;
            return false;
        }
        g_stagingWidth = desc.Width;
        g_stagingHeight = desc.Height;
    }
    g_context->CopySubresourceRegion(g_staging, 0, 0, 0, 0, texture, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(g_staging, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    *rows = static_cast<const std::uint8_t*>(mapped.pData);
    *pitch = mapped.RowPitch;
    return true;
}

void UnmapTarget() {
    g_context->Unmap(g_staging, 0);
    sg_reset_state_cache();
}

// One 8-bit channel into a mask's bits: shift down to its width, then up to its place.
struct ChannelPack {
    int down = 8, up = 0;
    std::uint32_t mask = 0;
    explicit ChannelPack(std::uint32_t m) : mask(m) {
        if (!m) return;
        int shift = 0, bits = 0;
        while (!((m >> shift) & 1)) ++shift;
        while (shift + bits < 32 && ((m >> (shift + bits)) & 1)) ++bits;
        if (bits >= 8) {
            down = 0;
            up = shift + bits - 8;
        } else {
            down = 8 - bits;
            up = shift;
        }
    }
    std::uint32_t operator()(std::uint8_t v) const { return ((static_cast<std::uint32_t>(v) >> down) << up) & mask; }
};

} // namespace

bool CopyFrameTo(const void* holder, const Rect& source) {
    if (!holder || !Active() || !EnsureStarted()) return false;
    // The holders the screen copies write (005FD3D0) have only memory of the renderer's.
    const SurfaceMemory* memory = SurfaceMemoryOf(holder);
    if (!memory || (memory->bitsPerPixel != 16 && memory->bitsPerPixel != 32)) return false;
    FpuGuard fpu;
    // Nothing of this frame drawn or queued yet (a copy between frames: the quick save's key, 006570E0
    // with DL 0, R13): the target still holds the last frame, as the original's render target did. A
    // pass now would clear it to the clear colour first.
    bool betweenFrames = g_haveFrame && g_passes == 0 && g_frame.commands.empty();
    if (!g_frameDrawn && !betweenFrames && !DrawPending()) return false;
    if (!g_colour.id) return false;
    Rect r = source;
    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > g_width) r.right = g_width;
    if (r.bottom > g_height) r.bottom = g_height;
    if (r.right <= r.left || r.bottom <= r.top) return false;
    D3D11_BOX box{static_cast<UINT>(r.left), static_cast<UINT>(r.top), 0, static_cast<UINT>(r.right),
                  static_cast<UINT>(r.bottom), 1};
    const std::uint8_t* rgba = nullptr;
    UINT pitch = 0;
    if (!MapTarget(box, &rgba, &pitch)) return false;
    int sw = r.right - r.left, sh = r.bottom - r.top;
    // Into the memory, stretched to its size as DirectDraw's Blt did (nearest texel).
    GdiFlush();   // GDI drawing still queued for the memory must not land on the copy
    int dw = memory->width, dh = memory->height;
    std::uint32_t alpha = memory->alphaMask;
    ChannelPack pr(memory->redMask), pg(memory->greenMask), pb(memory->blueMask);
    std::vector<int> columns(static_cast<std::size_t>(dw));
    for (int x = 0; x < dw; ++x) columns[static_cast<std::size_t>(x)] = static_cast<int>(static_cast<long long>(x) * sw / dw) * 4;
    bool bgrx = memory->bitsPerPixel == 32 && memory->redMask == 0xFF0000 && memory->greenMask == 0xFF00 &&
                memory->blueMask == 0xFF;
    for (int y = 0; y < dh; ++y) {
        const std::uint8_t* src = rgba + static_cast<std::size_t>(static_cast<long long>(y) * sh / dh) * pitch;
        auto* row = static_cast<std::uint8_t*>(memory->bits) + static_cast<std::size_t>(y) * memory->pitch;
        if (memory->bitsPerPixel == 32) {
            auto* out = reinterpret_cast<std::uint32_t*>(row);
            for (int x = 0; x < dw; ++x) {
                const std::uint8_t* p = src + columns[static_cast<std::size_t>(x)];
                out[x] = bgrx ? (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2] | alpha
                              : pr(p[0]) | pg(p[1]) | pb(p[2]) | alpha;
            }
        } else {
            auto* out = reinterpret_cast<std::uint16_t*>(row);
            for (int x = 0; x < dw; ++x) {
                const std::uint8_t* p = src + columns[static_cast<std::size_t>(x)];
                out[x] = static_cast<std::uint16_t>(pr(p[0]) | pg(p[1]) | pb(p[2]) | alpha);
            }
        }
    }
    UnmapTarget();
    SurfaceWritten(holder, nullptr);
    return true;
}

void EndFrame() {
    if (g_frameDrawn || !EnsureStarted()) return;
    g_frameDrawn = true;
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::endFrameMs);
    if (!DrawPending()) return;
    // "present" without DirectDraw: onto the game window through the new renderer's swap chain, in
    // turn with the cursor thread's presents (render_cursor.cpp), which take the frame as it was
    // before its cursor.
    bool toWindow = false;
    if (g_presentsItself) {
        BeginWindowPresent();
        KeepFrameForCursor();
        DrawCursor();
        toWindow = DrawToWindow();
    }
    ShadowFrameEnd();
    sg_commit();
    ++g_commits;
    if (toWindow) PresentWindow();
    if (g_presentsItself) EndWindowPresent(toWindow && !g_deviceLost);
    g_haveFrame = true;
}

void Present() {
    if (!Active()) return;
    EndFrame();
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
    g_frame.blitBatches.clear();
    g_frame.clears.clear();
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

SmoothInfo SmoothState() {
    SmoothInfo info = g_lastCounts;
    info.on = Smooth();
    info.deviceSamples = g_deviceSamples;
    info.targetSamples = g_targetSamples;
    return info;
}

bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr) {
    if (!Active() || !g_started || !g_haveFrame || !g_colour.id) return false;
    FpuGuard fpu;
    sg_d3d11_image_info info = sg_d3d11_query_image_info(g_colour);
    auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(info.tex2d));
    if (!texture) return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    g_context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    bool ok = SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    if (ok) {
        *width = static_cast<int>(desc.Width);
        *height = static_cast<int>(desc.Height);
        bgr->resize(static_cast<std::size_t>(desc.Width) * desc.Height * 3);
        for (UINT y = 0; y < desc.Height; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            std::uint8_t* out = bgr->data() + static_cast<std::size_t>(y) * desc.Width * 3;
            for (UINT x = 0; x < desc.Width; ++x) {   // RGBA8 -> B, G, R
                out[x * 3 + 0] = row[x * 4 + 2];
                out[x * 3 + 1] = row[x * 4 + 1];
                out[x * 3 + 2] = row[x * 4 + 0];
            }
        }
        g_context->Unmap(staging, 0);
    }
    staging->Release();
    sg_reset_state_cache();
    return ok;
}

} // namespace render
