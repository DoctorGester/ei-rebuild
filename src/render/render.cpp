// The new renderer's frame: the switch, the Direct3D 11 device behind sokol_gfx, the off-screen
// target the frame is drawn into (the game's resolution), the child window that can show it, and
// reading it back for captures. See render.h and docs/notes/renderer.md.
#include "render.h"
#include "render_game.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <ddraw.h>
#include <dxgi1_2.h>
#include <float.h>

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
bool g_active = false;
bool g_show = false;
bool g_split = false;
bool g_present = false;
bool g_cpuWindows = false;
bool g_oldTerrain = true;
bool g_oldObjects = true;
bool g_oldInterface = true;
bool g_inWorld = false;
bool g_failed = false;   // device or pipelines could not be created: the sidecar stays off
bool g_presentsItself = false;   // StartPresenting succeeded: the game has no DirectDraw device

void ParseSpec(const char* spec) {
    g_active = g_show = g_split = g_present = g_cpuWindows = false;
    g_oldTerrain = g_oldObjects = g_oldInterface = true;
    std::string s = spec ? spec : "";
    std::size_t start = 0;
    bool noCpuWindows = false;
    while (start <= s.size()) {
        std::size_t comma = s.find(',', start);
        std::string word = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!word.empty() && word.front() == ' ') word.erase(word.begin());
        while (!word.empty() && word.back() == ' ') word.pop_back();
        if (word == "new") g_active = true;
        else if (word == "show") g_show = true;
        else if (word == "split") g_show = g_split = true;
        else if (word == "noterrain") g_oldTerrain = false;
        else if (word == "noobjects") g_oldObjects = false;
        else if (word == "nointerface") g_oldInterface = false;
        else if (word == "present") g_present = true;
        else if (word == "cpuwindows") g_cpuWindows = true;
        else if (word == "nocpuwindows") noCpuWindows = true;
        else if (!word.empty()) ei::Log("render: unknown option '%s' in EI_RENDER", word.c_str());
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (g_present) {
        // No child window: a second swap chain over dgVoodoo's full-screen one deactivated the game
        // (WM_ACTIVATE inactive: the game stops drawing and minimises itself). The frame is copied
        // into the game's render target and the old present shows it.
        g_show = g_split = false;
        g_oldTerrain = g_oldObjects = g_oldInterface = false;
        // The windows' pixels in game-owned memory (render_present_cpuwindows passes on both
        // installs); "nocpuwindows" keeps DirectDraw's own surfaces.
        g_cpuWindows = true;
    }
    if (noCpuWindows) g_cpuWindows = false;
    if (!g_active) {
        g_show = g_split = g_present = g_cpuWindows = false;
        g_oldTerrain = g_oldObjects = g_oldInterface = true;
    }
    if (g_presentsItself && !(g_active && g_present && g_cpuWindows)) {
        // The game was built without DirectDraw (StartPresenting): only the new renderer can draw.
        ei::Log("render: '%s' asks for the old path, but the game has no DirectDraw device: 'new,present' stays on",
                spec ? spec : "");
        g_active = g_present = g_cpuWindows = true;
        g_show = g_split = false;
        g_oldTerrain = g_oldObjects = g_oldInterface = false;
    }
    g_configured = true;
    if (g_active)
        ei::Log("render: new renderer on (%s%s%s%s%s)", g_present ? "presenting" : g_show ? (g_split ? "split view" : "shown") : "off-screen",
                g_oldTerrain ? "" : ", old terrain off", g_oldObjects ? "" : ", old objects off",
                g_oldInterface ? "" : ", old interface off", g_cpuWindows ? ", windows in game memory" : "");
}

// EI_RENDER, or "".
std::string EnvironmentSpec() {
    char spec[256];
    DWORD n = GetEnvironmentVariableA("EI_RENDER", spec, sizeof(spec));
    return n > 0 && n < sizeof(spec) ? std::string(spec) : std::string();
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
        return false;
    }
    ei::Log("render: Direct3D 11 device ready (feature level %X)", static_cast<unsigned>(got));
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

// --- The child window that shows the frame ("show", "split") --------------------------------------------

HWND g_view = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_swapView = nullptr;
int g_viewWidth = 0, g_viewHeight = 0;
sg_shader g_blitShader{};
sg_pipeline g_blitPipeline{};
bool g_viewFailed = false;

LRESULT CALLBACK ViewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;   // the game gets the mouse
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcA(window, message, wParam, lParam);
}

const char* const kShowSource = R"(
cbuffer params : register(b0) { float4 source; };
struct vs_out { float2 uv : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    vs_out o;
    o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    o.uv = lerp(source.xy, source.zw, p);
    return o;
}
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
float4 ps_main(vs_out i) : SV_Target0 { return float4(tex.Sample(smp, i.uv).rgb, 1.0); }
)";

bool CreateShowPipeline() {
    sg_shader_desc s{};
    s.vertex_func.source = kShowSource;
    s.vertex_func.entry = "vs_main";
    s.fragment_func.source = kShowSource;
    s.fragment_func.entry = "ps_main";
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = 16;
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[0].texture.hlsl_register_t_n = 0;
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    s.label = "ei-show";
    g_blitShader = sg_make_shader(&s);
    sg_pipeline_desc p{};
    p.shader = g_blitShader;
    p.colors[0].pixel_format = SG_PIXELFORMAT_BGRA8;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    p.label = "ei-show";
    g_blitPipeline = sg_make_pipeline(&p);
    return sg_query_pipeline_state(g_blitPipeline) == SG_RESOURCESTATE_VALID;
}

void ReleaseSwapView() {
    if (g_swapView) g_swapView->Release();
    g_swapView = nullptr;
}

bool CreateSwapView() {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) return false;
    HRESULT hr = g_device->CreateRenderTargetView(buffer, nullptr, &g_swapView);
    buffer->Release();
    return SUCCEEDED(hr);
}

bool CreateView(int width, int height) {
    HINSTANCE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&ViewProc), &self);
    WNDCLASSA wc{};
    wc.lpfnWndProc = &ViewProc;
    wc.hInstance = self;
    wc.lpszClassName = "EiNewRendererView";
    RegisterClassA(&wc);
    g_view = CreateWindowExA(0, wc.lpszClassName, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, width, height,
                             GameWindow(), nullptr, self, nullptr);
    if (!g_view) return false;
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;
    bool ok = SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
              SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
              SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)));
    if (ok) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Width = static_cast<UINT>(width);
        sd.BufferDesc.Height = static_cast<UINT>(height);
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 1;
        sd.OutputWindow = g_view;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ok = SUCCEEDED(factory->CreateSwapChain(g_device, &sd, &g_swapChain));
        if (ok) factory->MakeWindowAssociation(g_view, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
    ok = ok && CreateSwapView() && (g_blitPipeline.id || CreateShowPipeline());
    if (!ok) return false;
    g_viewWidth = width;
    g_viewHeight = height;
    return true;
}

// Draws the frame's `source` part (texture coordinates: left, top, right, bottom) over the whole
// of `view` (`width` x `height`, BGRA8).
void DrawFrameInto(ID3D11RenderTargetView* view, int width, int height, const float source[4]) {
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    pass.swapchain.width = width;
    pass.swapchain.height = height;
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_BGRA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_NONE;
    pass.swapchain.d3d11.render_view = view;
    pass.label = "ei-show";
    sg_begin_pass(&pass);
    sg_apply_pipeline(g_blitPipeline);
    sg_bindings b{};
    b.views[0] = g_colourTexture;
    b.samplers[0] = LinearSampler();
    sg_apply_bindings(&b);
    float uniforms[4] = {source[0], source[1], source[2], source[3]};
    sg_range range{uniforms, sizeof(uniforms)};
    sg_apply_uniforms(0, &range);
    sg_draw(0, 3, 1);
    sg_end_pass();
}

// Shows the frame in the child window: the whole client area, or its right half for "split" (the
// same half of the frame, so the two halves line up with the old frame's).
void ShowFrame() {
    if (!g_show || g_viewFailed || !g_colour.id) return;
    RECT client{};
    if (!GetClientRect(GameWindow(), &client)) return;
    int clientWidth = client.right - client.left, clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0) return;
    int x = g_split ? clientWidth / 2 : 0;
    int width = clientWidth - x;
    if (!g_view) {
        if (!CreateView(width, clientHeight)) {
            ei::Log("render: cannot create the window that shows the new frame");
            g_viewFailed = true;
            return;
        }
    }
    if (width != g_viewWidth || clientHeight != g_viewHeight) {
        ReleaseSwapView();
        if (FAILED(g_swapChain->ResizeBuffers(1, static_cast<UINT>(width), static_cast<UINT>(clientHeight),
                                              DXGI_FORMAT_B8G8R8A8_UNORM, 0)) ||
            !CreateSwapView()) {
            g_viewFailed = true;
            return;
        }
        g_viewWidth = width;
        g_viewHeight = clientHeight;
    }
    SetWindowPos(g_view, HWND_TOP, x, 0, width, clientHeight, SWP_NOACTIVATE);
    const float source[4] = {g_split ? 0.5f : 0.0f, 0.0f, 1.0f, 1.0f};
    DrawFrameInto(g_swapView, width, clientHeight, source);
}

// --- The game window's swap chain (the switch-over without DirectDraw) ------------------------------------

// A flip-model swap chain on the game window (no DirectDraw or Direct3D 7 has touched it), its back
// buffer the window's client size; the frame is drawn into it stretched (one to one when the
// sizes agree) and presented with the vertical blank (no faster than the screen shows frames).
// The window keeps its size and style: the full-screen mode change, the gamma ramp and lost
// devices are ticket R9c's (docs/notes/renderer.md, section 9).
IDXGISwapChain1* g_windowChain = nullptr;
ID3D11RenderTargetView* g_windowView = nullptr;
int g_windowWidth = 0, g_windowHeight = 0;
bool g_windowFailed = false;
int g_windowPresentFailures = 0;

void ReleaseWindowView() {
    if (g_windowView) g_windowView->Release();
    g_windowView = nullptr;
}

bool CreateWindowView() {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(g_windowChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) return false;
    HRESULT hr = g_device->CreateRenderTargetView(buffer, nullptr, &g_windowView);
    buffer->Release();
    return SUCCEEDED(hr);
}

bool CreateWindowChain(HWND window, int width, int height) {
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
              SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
              SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)));
    if (ok) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = static_cast<UINT>(width);
        sd.Height = static_cast<UINT>(height);
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        HRESULT hr = factory->CreateSwapChainForHwnd(g_device, window, &sd, nullptr, nullptr, &g_windowChain);
        if (FAILED(hr)) {
            ei::Log("render: CreateSwapChainForHwnd failed (%08lX)", static_cast<unsigned long>(hr));
            g_windowChain = nullptr;
            ok = false;
        }
        // The game handles Alt+Enter and its window itself.
        if (ok) factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
    if (!ok) return false;
    if (!CreateWindowView()) return false;
    if (!g_blitPipeline.id && !CreateShowPipeline()) return false;
    g_windowWidth = width;
    g_windowHeight = height;
    ei::Log("render: presenting on the game window through a flip-model swap chain (%dx%d)", width, height);
    return true;
}

// The frame into the window's back buffer (made or resized to the client area first). False when
// there is nothing to present into (no window, minimised, the swap chain failed). The client area is
// in physical pixels: the hook makes the game system DPI aware at WinMain (src/hook/hook.cpp), so
// Windows does not stretch the window and the whole back buffer shows.
bool DrawToWindow() {
    if (g_windowFailed || !g_colour.id) return false;
    HWND window = GameWindow();
    RECT client{};
    if (!window || !GetClientRect(window, &client)) return false;
    int width = client.right - client.left, height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return false;
    if (!g_windowChain) {
        if (!CreateWindowChain(window, width, height)) {
            ei::Log("render: cannot present on the game window; nothing is shown (captures still work)");
            g_windowFailed = true;
            return false;
        }
    }
    if (width != g_windowWidth || height != g_windowHeight) {
        ReleaseWindowView();
        sg_reset_state_cache();
        if (FAILED(g_windowChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0)) ||
            !CreateWindowView()) {
            ei::Log("render: cannot resize the game window's swap chain to %dx%d", width, height);
            g_windowFailed = true;
            return false;
        }
        g_windowWidth = width;
        g_windowHeight = height;
    }
    const float whole[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    DrawFrameInto(g_windowView, width, height, whole);
    return true;
}

void PresentWindow() {
    HRESULT hr = g_windowChain->Present(1, 0);
    if (FAILED(hr) && g_windowPresentFailures++ < 10)
        ei::Log("render: the game window's Present failed (%08lX)", static_cast<unsigned long>(hr));
}

// --- The frame -----------------------------------------------------------------------------------------

Frame g_frame;
bool g_frameDrawn = false;
bool g_started = false;
std::uint32_t g_frameNumber = 0;
std::uint32_t g_commits = 0;
int g_presentFailures = 0;

bool EnsureStarted() {
    EnsureConfigured();
    if (!g_active || g_failed) return false;
    if (g_started) return true;
    FpuGuard fpu;
    if (!CreateDevice() || !CreateTerrainPipelines() || !CreateMeshShader() || !CreateBlitPipeline()) {
        g_failed = true;
        return false;
    }
    g_started = true;
    return true;
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

int AddMatrix(const float* m) {
    if (!g_frame.matrices.empty() && std::memcmp(g_frame.matrices.back().m, m, sizeof(Matrix4)) == 0)
        return static_cast<int>(g_frame.matrices.size()) - 1;
    Matrix4 copy;
    std::memcpy(copy.m, m, sizeof(copy.m));
    g_frame.matrices.push_back(copy);
    return static_cast<int>(g_frame.matrices.size()) - 1;
}

void LockGameDevice() { EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(game::Where().deviceLock)); }
void UnlockGameDevice() { LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(game::Where().deviceLock)); }

namespace {
// The game's IDirect3DDevice7 (in the switch-over the device object's stand-in, device.h).
void* OldDevice() {
    CTextureDevice* device = DeviceObject();
    return device ? device->d3dDevice_ : nullptr;
}
} // namespace

// IDirect3DDevice7::GetRenderState (slot 21).
void OldRenderStates(const std::uint32_t* states, std::uint32_t* values, int count) {
    void* d3d = OldDevice();
    for (int i = 0; i < count; ++i) values[i] = 0;
    if (!d3d) return;
    using GetState = long(__stdcall*)(void*, unsigned long, unsigned long*);
    auto get = reinterpret_cast<GetState>((*static_cast<void***>(d3d))[21]);
    LockGameDevice();
    for (int i = 0; i < count; ++i) {
        unsigned long value = 0;
        get(d3d, states[i], &value);
        values[i] = static_cast<std::uint32_t>(value);
    }
    UnlockGameDevice();
}

// IDirect3DDevice7::GetTextureStageState (slot 36).
void OldStageStates(int stage, const std::uint32_t* states, std::uint32_t* values, int count) {
    void* d3d = OldDevice();
    for (int i = 0; i < count; ++i) values[i] = 0;
    if (!d3d) return;
    using GetStage = long(__stdcall*)(void*, unsigned long, unsigned long, unsigned long*);
    auto get = reinterpret_cast<GetStage>((*static_cast<void***>(d3d))[36]);
    LockGameDevice();
    for (int i = 0; i < count; ++i) {
        unsigned long value = 0;
        get(d3d, static_cast<unsigned long>(stage), states[i], &value);
        values[i] = static_cast<std::uint32_t>(value);
    }
    UnlockGameDevice();
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

bool InWorld() { return g_inWorld; }

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
    return g_cpuWindows && g_active;
}

bool Presenting() {
    EnsureConfigured();
    return g_present && Active();
}

bool StartPresenting() {
    if (g_presentsItself) return true;
    if (!Presenting() || !EnsureStarted()) return false;
    g_presentsItself = true;
    // No DirectDraw surfaces for the windows either.
    if (!g_cpuWindows) ei::Log("render: 'nocpuwindows' needs DirectDraw: the windows' pixels stay in game memory");
    g_cpuWindows = true;
    ei::Log("render: the switch-over without DirectDraw: the new renderer presents the frames itself");
    return true;
}

bool PresentsItself() { return g_presentsItself; }

bool DrawOldTerrain() {
    EnsureConfigured();
    return g_oldTerrain || !Active();
}

void SetDrawOldTerrain(bool on) {
    EnsureConfigured();
    g_oldTerrain = on;
}

bool DrawOldMeshes() {
    EnsureConfigured();
    return (g_inWorld ? g_oldObjects : g_oldInterface) || !Active();
}

bool DrawOldInterface() {
    EnsureConfigured();
    return g_oldInterface || !Active();
}

void SetDrawOldObjects(bool on) {
    EnsureConfigured();
    g_oldObjects = on;
}

void SetDrawOldInterface(bool on) {
    EnsureConfigured();
    g_oldInterface = on;
}

void TexturePixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                   std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask) {
    if (!holder || !pixels || !Active() || !EnsureStarted()) return;
    FpuGuard fpu;
    TextureFromPixels(holder, pixels, width, height, pitch, bitsPerPixel, redMask, greenMask, blueMask, alphaMask);
}

void BeginWorld() { g_inWorld = true; }
void EndWorld() { g_inWorld = false; }

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

// A staging copy of the off-screen target's `box`, mapped for reading: `rows` and `pitch` until
// UnmapTarget. The staging texture is kept (the full frame is read every frame with "present").
ID3D11Texture2D* g_staging = nullptr;
UINT g_stagingWidth = 0, g_stagingHeight = 0;

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
    auto* dd = static_cast<IDirectDrawSurface7*>(static_cast<const CDXSurface*>(holder)->surface);
    // In the switch-over a surface with memory of the renderer's is written there directly (no
    // DirectDraw); a holder made in the switch-over has nothing else.
    const SurfaceMemory* memory = SurfaceMemoryOf(holder);
    bool direct = memory && (g_present || !dd);
    if (!dd && !direct) return false;
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
    // Into the surface, stretched to its size as DirectDraw's Blt does (nearest texel).
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    HRESULT hr = S_OK;
    if (direct) {
        GdiFlush();   // GDI drawing still queued for the memory must not land on the copy
        desc.dwWidth = static_cast<DWORD>(memory->width);
        desc.dwHeight = static_cast<DWORD>(memory->height);
        desc.lPitch = memory->pitch;
        desc.lpSurface = memory->bits;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB | (memory->alphaMask ? DDPF_ALPHAPIXELS : 0);
        desc.ddpfPixelFormat.dwRGBBitCount = static_cast<DWORD>(memory->bitsPerPixel);
        desc.ddpfPixelFormat.dwRBitMask = memory->redMask;
        desc.ddpfPixelFormat.dwGBitMask = memory->greenMask;
        desc.ddpfPixelFormat.dwBBitMask = memory->blueMask;
        desc.ddpfPixelFormat.dwRGBAlphaBitMask = memory->alphaMask;
    } else {
        LockGameDevice();
        hr = dd->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr);
    }
    bool ok = SUCCEEDED(hr) && desc.lpSurface && (desc.ddpfPixelFormat.dwFlags & DDPF_RGB) &&
              (desc.ddpfPixelFormat.dwRGBBitCount == 16 || desc.ddpfPixelFormat.dwRGBBitCount == 32);
    if (ok) {
        const DDPIXELFORMAT& pf = desc.ddpfPixelFormat;
        int dw = static_cast<int>(desc.dwWidth), dh = static_cast<int>(desc.dwHeight);
        std::uint32_t alpha = (pf.dwFlags & DDPF_ALPHAPIXELS) ? pf.dwRGBAlphaBitMask : 0;
        ChannelPack pr(pf.dwRBitMask), pg(pf.dwGBitMask), pb(pf.dwBBitMask);
        std::vector<int> columns(static_cast<std::size_t>(dw));
        for (int x = 0; x < dw; ++x) columns[static_cast<std::size_t>(x)] = static_cast<int>(static_cast<long long>(x) * sw / dw) * 4;
        bool bgrx = pf.dwRGBBitCount == 32 && pf.dwRBitMask == 0xFF0000 && pf.dwGBitMask == 0xFF00 && pf.dwBBitMask == 0xFF;
        for (int y = 0; y < dh; ++y) {
            const std::uint8_t* src = rgba + static_cast<std::size_t>(static_cast<long long>(y) * sh / dh) * pitch;
            auto* row = static_cast<std::uint8_t*>(desc.lpSurface) + static_cast<std::size_t>(y) * desc.lPitch;
            if (pf.dwRGBBitCount == 32) {
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
    }
    if (!direct) {
        if (SUCCEEDED(hr)) dd->Unlock(nullptr);
        UnlockGameDevice();
    }
    UnmapTarget();
    return ok;
}

void EndFrame() {
    if (g_frameDrawn || !EnsureStarted()) return;
    g_frameDrawn = true;
    FpuGuard fpu;
    if (!DrawPending()) return;
    ShowFrame();
    // "present" without DirectDraw: onto the game window through the new renderer's swap chain.
    bool toWindow = g_presentsItself && DrawToWindow();
    sg_commit();
    ++g_commits;
    if (g_swapChain && g_show && !g_viewFailed) g_swapChain->Present(0, 0);
    if (toWindow) PresentWindow();
    g_haveFrame = true;
    // "present" turned on after the DirectDraw device was made: into the game's render target,
    // which the old present then flips or copies to the screen.
    if (g_present && !g_presentsItself) {
        CTextureDevice* device = DeviceObject();
        const CDXSurface* target = device ? device->renderTarget_ : nullptr;
        if (target && !CopyFrameTo(target, Rect{0, 0, g_width, g_height}) && g_presentFailures++ < 10)
            ei::Log("render: cannot copy the frame into the game's render target");
    }
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
    g_frame.haveFog = false;
    g_frameDrawn = false;
    if (g_started) {
        FpuGuard fpu;
        RetireTextures();
        RetireBuffers();
        RetireFrameImages();
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
