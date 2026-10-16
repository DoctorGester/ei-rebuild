// The new renderer's frame: the switch, the Direct3D 11 device behind sokol_gfx, the off-screen
// target the frame is drawn into (the game's resolution) and reading it back for captures. See render.h and docs/notes/renderer.md.
#include "render.h"
#include "render_game.h"
#include "render_internal.h"

#include "../hook/log.h"
#include "ei/hook.h"   // ei::Standalone

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <float.h>

#include "../game/device_switchover.h"   // device::WithoutDirectDraw, SwitchOverRenderState
#include "../game/texture.h"   // CTextureDevice, CDXSurface

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace render {

namespace {

using namespace detail;

// --- The switch ------------------------------------------------------------------------------------

bool g_configured = false;
bool g_active = false;   // "new": the new renderer draws and presents every frame
bool g_failed = false;   // device or pipelines could not be created: the sidecar stays off
bool g_presentsItself = false;   // StartPresenting succeeded: the game has no DirectDraw device

void ParseSpec(const char* spec) {
    g_active = false;
    std::string s = spec ? spec : "";
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t comma = s.find(',', start);
        std::string word = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!word.empty() && word.front() == ' ') word.erase(word.begin());
        while (!word.empty() && word.back() == ' ') word.pop_back();
        if (word == "new") g_active = true;
        else if (word == "present" || word == "cpuwindows") {}   // what "new" means (run_all, the scenarios name them)
        else if (!word.empty()) ei::Log("render: unknown option '%s' in EI_RENDER", word.c_str());
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (g_presentsItself && !g_active) {
        // The game was built without DirectDraw (StartPresenting): only the new renderer can draw.
        ei::Log("render: '%s' asks for the old path, but the game has no DirectDraw device: 'new' stays on", spec ? spec : "");
        g_active = true;
    }
    g_configured = true;
    if (g_active) ei::Log("render: new renderer on (presenting, windows in game memory)");
}

// EI_RENDER; without it "" (off), in the standalone image "new,present": it has no dgVoodoo to count on
// and the new renderer presents by itself.
std::string EnvironmentSpec() {
    char spec[256];
    DWORD n = GetEnvironmentVariableA("EI_RENDER", spec, sizeof(spec));
    if (n > 0 && n < sizeof(spec)) return std::string(spec);
    return ei::Standalone() ? std::string("new,present") : std::string();
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
    // sokol cannot go on after a panic: the new renderer switches itself off (the old path draws).
    if (level == 0) g_failed = true;
}

bool CreateDevice() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   static_cast<UINT>(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &g_device, &got,
                                   &g_context);
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
        return false;
    }
    ei::Log("render: Direct3D 11 device ready (feature level %X)", static_cast<unsigned>(got));
    ProtectDevice(g_device, g_context);
    return true;
}

// --- The off-screen target ---------------------------------------------------------------------------

int g_width = 0, g_height = 0;
sg_image g_colour{}, g_depth{};
sg_view g_colourAttachment{}, g_colourTexture{}, g_depthAttachment{};
bool g_haveFrame = false;

void DestroyTarget() {
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

bool EnsureTarget(int width, int height) {
    if (width == g_width && height == g_height && g_colour.id) return true;
    DestroyTarget();
    sg_image_desc colour{};
    colour.usage.color_attachment = true;
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
    if (!g_active || g_failed) return false;
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
int g_losses = 0, g_recoveries = 0;
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
    DropTextures(&kept, &keptBytes);
    sg_shutdown();
    g_context->ClearState();
    g_context->Flush();
    g_context->Release();
    ULONG left = g_device->Release();
    g_context = nullptr;
    g_device = nullptr;
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

void ReportDeviceLost() { g_deviceLost = true; }
bool DeviceIsLost() { return g_deviceLost; }

bool TakeSimulatedLoss() {
    if (!g_simulateLoss) return false;
    g_simulateLoss = false;
    ei::Log("render: the device's loss asked for (render::LoseDevice): taken as DXGI_ERROR_DEVICE_REMOVED");
    return true;
}

void DeviceLossCounts(int* losses, int* recoveries) {
    *losses = g_losses;
    *recoveries = g_recoveries;
}

} // namespace detail

void LoseDevice() {
    if (!Active() || !g_started) return;
    g_simulateLoss = true;
}

namespace {

// The end of a presented frame (nothing of it holds sokol resources any more): a loss reported
// during the frame, asked for, or found by GetDeviceRemovedReason (a loss nothing reported: no swap
// chain of ours, "new" without "present") makes everything again.
void CheckDevice() {
    if (TakeSimulatedLoss()) ReportDeviceLost();
    if (!g_deviceLost && g_device) {
        HRESULT reason = g_device->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            ei::Log("render: the Direct3D 11 device was lost (GetDeviceRemovedReason %08lX)", static_cast<unsigned long>(reason));
            ReportDeviceLost();
        }
    }
    if (g_deviceLost) RecreateDevice();
}

} // namespace

namespace detail {

// The game's main-thread x87 state is part of its behaviour (docs/notes/rebirth.md, "FPU
// precision"); drivers are not expected to change it, but the control word is put back after every
// call into them anyway.
FpuGuard::FpuGuard() : saved_(0) { __control87_2(0, 0, &saved_, nullptr); }
FpuGuard::~FpuGuard() {
    unsigned int now = 0;
    __control87_2(saved_, _MCW_EM | _MCW_IC | _MCW_RC | _MCW_PC, &now, nullptr);
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
Addresses& Where() {
    static Addresses addresses;
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

bool Active() {
    EnsureConfigured();
    return g_active && !g_failed;
}

bool CpuWindows() {
    EnsureConfigured();
    return g_active;
}

bool Presenting() { return Active(); }

bool StartPresenting() {
    if (g_presentsItself) return true;
    if (!Presenting() || !EnsureStarted()) return false;
    g_presentsItself = true;
    ei::Log("render: the switch-over without DirectDraw: the new renderer presents the frames itself");
    return true;
}

bool PresentsItself() { return g_presentsItself; }

void TexturePixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                   std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask) {
    if (!holder || !pixels || !Active() || !EnsureStarted()) return;
    FpuGuard fpu;
    TextureFromPixels(holder, pixels, width, height, pitch, bitsPerPixel, redMask, greenMask, blueMask, alphaMask);
}

void ClearDepth(float z, const Rect* rects, int count) {
    if (!Active()) return;
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

namespace {

std::size_t g_drawnCommands = 0;   // commands of this frame already drawn into the target
int g_passes = 0;                  // passes drawn this frame

// Draws the frame's commands not drawn yet into the off-screen target: one pass, the frame's first
// clearing it. A frame is drawn in more than one pass when a copy of it is taken in its middle
// (CopyFrameTo).
bool DrawPending() {
    int width = 0, height = 0;
    if (!GameTargetSize(&width, &height) || !EnsureTarget(width, height)) return false;
    if (g_passes > 0 && g_drawnCommands == g_frame.commands.size()) return true;
    UploadTerrain(g_frame);
    UploadMeshes(g_frame);
    // Direct3D 7 puts pixel centres at integer screen coordinates, Direct3D 11 half a pixel further:
    // the 3D draws move their positions by half a pixel (Frame::pixelShift). (A viewport moved by
    // half a pixel would do the same, but sokol takes whole pixels.)
    g_frame.pixelShift[0] = 1.0f / static_cast<float>(width);
    g_frame.pixelShift[1] = -1.0f / static_cast<float>(height);
    sg_pass pass{};
    bool first = g_passes == 0;
    pass.action.colors[0].load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = FromD3DColour(GameClearColour());
    pass.action.depth.load_action = first ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = g_colourAttachment;
    pass.attachments.depth_stencil = g_depthAttachment;
    pass.label = "ei-frame";
    sg_begin_pass(&pass);
    for (std::size_t k = g_drawnCommands; k < g_frame.commands.size(); ++k) {
        const Command& c = g_frame.commands[k];
        auto i = static_cast<std::size_t>(c.index);
        switch (c.kind) {
        case Command::Terrain: DrawTerrainBatch(g_frame, g_frame.terrainBatches[i]); break;
        case Command::Mesh: DrawMeshBatch(g_frame, g_frame.meshBatches[i]); break;
        case Command::Blit: DrawBlitBatch(g_frame, g_frame.blitBatches[i], width, height); break;
        case Command::ClearDepth: DrawClearDepth(g_frame.clears[i], width, height); break;
        }
    }
    sg_end_pass();
    g_drawnCommands = g_frame.commands.size();
    ++g_passes;
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
    if (!g_frameDrawn && !DrawPending()) return false;
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
    return true;
}

void EndFrame() {
    if (g_frameDrawn || !EnsureStarted()) return;
    g_frameDrawn = true;
    FpuGuard fpu;
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
    sg_commit();
    ++g_commits;
    if (toWindow) PresentWindow();
    if (g_presentsItself) EndWindowPresent(toWindow && !g_deviceLost);
    g_haveFrame = true;
}

void Present() {
    if (!Active()) return;
    EndFrame();
    g_drawnCommands = 0;
    g_passes = 0;
    g_frame.matrices.clear();
    g_frame.terrainVertices.clear();
    g_frame.terrainIndices.clear();
    g_frame.terrainBatches.clear();
    g_frame.meshVertices.clear();
    g_frame.meshIndices.clear();
    g_frame.meshBatches.clear();
    g_frame.blitBatches.clear();
    g_frame.clears.clear();
    g_frame.commands.clear();
    g_frame.haveCursor = false;
    g_frame.haveFog = false;
    g_frameDrawn = false;
    if (g_started) {
        FpuGuard fpu;
        RetireTextures();
        RetireBuffers();
        RetireFrameImages();
        CheckDevice();
    }
    ++g_frameNumber;
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
