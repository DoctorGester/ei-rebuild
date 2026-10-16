// The game window's swap chain in the switch-over without DirectDraw (render::StartPresenting): the
// frame is drawn into a flip-model swap chain on the game window, its aspect kept (centred, black
// bars when the window's client area has another shape), through the display's gamma ramp (the
// options' brightness / contrast / gamma, IDirectDrawGammaControl's ramp in the old path) as a
// shader step, and presented with the vertical blank. Also where the frame sits in the window, for
// the game's mouse coordinates (WindowToFrame / FrameToWindow). docs/notes/renderer.md, section 9.
#include "render.h"
#include "render_game.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <cstring>

namespace render {

namespace {

using namespace detail;

HWND GameWindow() { return *reinterpret_cast<HWND*>(game::Where().window); }

IDXGISwapChain1* g_chain = nullptr;
ID3D11RenderTargetView* g_view = nullptr;
int g_chainWidth = 0, g_chainHeight = 0;
bool g_failed = false;        // no swap chain could be made, or the device is gone: nothing is shown
int g_presentFailures = 0;
// Present waits for the vertical blank (sync interval 1) unless the harness's virtual clock runs
// the game, where a frame's length no longer depends on the display.
bool g_waitForVsync = true;
bool g_recreated = false;     // the swap chain was made again once after a failed resize

sg_shader g_shader{};
sg_pipeline g_pipeline{};

// The gamma ramp as a 256 x 1 lookup (RGBA8: the red, green and blue ramps' entries scaled to 255),
// identity until the game sets one.
sg_image g_rampImage{};
sg_view g_rampView{};
struct Ramp {
    std::uint8_t entries[256 * 4];
    Ramp() {
        for (int i = 0; i < 256; ++i) {
            entries[i * 4 + 0] = entries[i * 4 + 1] = entries[i * 4 + 2] = static_cast<std::uint8_t>(i);
            entries[i * 4 + 3] = 255;
        }
    }
};
Ramp g_rampTable;   // (kept across devices: the game sets it once, a new device gets it again)
std::uint8_t (&g_ramp)[256 * 4] = g_rampTable.entries;
bool g_rampDirty = true;     // to be uploaded before the next present
bool g_rampIdentity = true;
bool g_checkRamp = false;     // verify build: compare the next presented frame with the ramp applied
WindowPresentInfo g_info;

const char* const kSource = R"(
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
Texture2D<float4> ramp : register(t1);
SamplerState smp : register(s0);
float4 ps_main(vs_out i) : SV_Target0 {
    int3 k = (int3)(saturate(tex.Sample(smp, i.uv).rgb) * 255.0 + 0.5);
    return float4(ramp.Load(int3(k.r, 0, 0)).r, ramp.Load(int3(k.g, 0, 0)).g, ramp.Load(int3(k.b, 0, 0)).b, 1.0);
}
)";

bool CreatePipeline() {
    sg_shader_desc s{};
    s.vertex_func.source = kSource;
    s.vertex_func.entry = "vs_main";
    s.fragment_func.source = kSource;
    s.fragment_func.entry = "ps_main";
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = 16;
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[0].texture.image_type = SG_IMAGETYPE_2D;
    s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[0].texture.hlsl_register_t_n = 0;
    s.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[1].texture.image_type = SG_IMAGETYPE_2D;
    s.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s.views[1].texture.hlsl_register_t_n = 1;
    s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s.samplers[0].hlsl_register_s_n = 0;
    s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[0].view_slot = 0;
    s.texture_sampler_pairs[0].sampler_slot = 0;
    // The ramp is read with Load; sokol wants every texture in a pair all the same.
    s.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[1].view_slot = 1;
    s.texture_sampler_pairs[1].sampler_slot = 0;
    s.label = "ei-present";
    g_shader = sg_make_shader(&s);
    sg_pipeline_desc p{};
    p.shader = g_shader;
    p.colors[0].pixel_format = SG_PIXELFORMAT_BGRA8;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.sample_count = 1;
    p.label = "ei-present";
    g_pipeline = sg_make_pipeline(&p);
    if (sg_query_pipeline_state(g_pipeline) != SG_RESOURCESTATE_VALID) return false;

    sg_image_desc image{};
    image.width = 256;
    image.height = 1;
    image.pixel_format = SG_PIXELFORMAT_RGBA8;
    image.usage.dynamic_update = true;
    image.label = "ei-gamma-ramp";
    g_rampImage = sg_make_image(&image);
    sg_view_desc v{};
    v.texture.image = g_rampImage;
    g_rampView = sg_make_view(&v);
    g_rampDirty = true;
    return sg_query_image_state(g_rampImage) == SG_RESOURCESTATE_VALID;
}

void ReleaseView() {
    if (g_view) g_view->Release();
    g_view = nullptr;
}

void ReleaseChain() {
    ReleaseView();
    if (g_chain) g_chain->Release();
    g_chain = nullptr;
    g_chainWidth = g_chainHeight = 0;
}

bool CreateView() {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(g_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) return false;
    HRESULT hr = D3DDevice()->CreateRenderTargetView(buffer, nullptr, &g_view);
    buffer->Release();
    return SUCCEEDED(hr);
}

bool CreateChain(HWND window, int width, int height) {
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = SUCCEEDED(D3DDevice()->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
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
        HRESULT hr = factory->CreateSwapChainForHwnd(D3DDevice(), window, &sd, nullptr, nullptr, &g_chain);
        if (FAILED(hr)) {
            ei::Log("render: CreateSwapChainForHwnd failed (%08lX)", static_cast<unsigned long>(hr));
            g_chain = nullptr;
            ok = false;
        }
        // The game handles Alt+Enter and its window itself.
        if (ok) factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgiDevice) dxgiDevice->Release();
    if (!ok) return false;
    if (!CreateView()) {
        ReleaseChain();
        return false;
    }
    if (!g_pipeline.id && !CreatePipeline()) return false;
    g_chainWidth = width;
    g_chainHeight = height;
    ei::Log("render: presenting on the game window through a flip-model swap chain (%dx%d)", width, height);
    return true;
}

// The device is gone (removed, reset, hung): every sokol resource with it. Nothing more is drawn
// into the swap chain; at the end of the frame the device, the swap chain and every image are made
// again (render.cpp, RecreateDevice).
void DeviceLost(const char* where, HRESULT hr) {
    HRESULT reason = D3DDevice()->GetDeviceRemovedReason();
    ei::Log("render: the Direct3D 11 device was lost (%s: %08lX, reason %08lX)", where, static_cast<unsigned long>(hr),
            static_cast<unsigned long>(reason));
    ReportDeviceLost(hr, reason);
}

bool IsDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

// Back buffer of `width` x `height`: made, or resized (a failed resize, e.g. after a display mode
// change, makes the swap chain again once).
bool EnsureChain(HWND window, int width, int height) {
    if (!g_chain) {
        if (CreateChain(window, width, height)) return true;
        ei::Log("render: cannot present on the game window; nothing is shown (captures still work)");
        g_failed = true;
        return false;
    }
    if (width == g_chainWidth && height == g_chainHeight) return true;
    ReleaseView();
    sg_reset_state_cache();
    HRESULT hr = g_chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr) && CreateView()) {
        g_chainWidth = width;
        g_chainHeight = height;
        return true;
    }
    if (IsDeviceLost(hr)) {
        DeviceLost("ResizeBuffers", hr);
        return false;
    }
    ei::Log("render: cannot resize the game window's swap chain to %dx%d (%08lX)%s", width, height,
            static_cast<unsigned long>(hr), g_recreated ? "" : "; making it again");
    ReleaseChain();
    if (!g_recreated) {
        g_recreated = true;
        if (CreateChain(window, width, height)) return true;
    }
    g_failed = true;
    return false;
}

// The frame's place in a client area: its aspect kept, as large as fits, centred.
Rect Placed(int clientWidth, int clientHeight, int frameWidth, int frameHeight) {
    long long a = static_cast<long long>(clientWidth) * frameHeight;
    long long b = static_cast<long long>(clientHeight) * frameWidth;
    int w = clientWidth, h = clientHeight;
    if (a > b) w = static_cast<int>((static_cast<long long>(clientHeight) * frameWidth + frameHeight / 2) / frameHeight);
    else if (a < b) h = static_cast<int>((static_cast<long long>(clientWidth) * frameHeight + frameWidth / 2) / frameWidth);
    int x = (clientWidth - w) / 2, y = (clientHeight - h) / 2;
    return Rect{x, y, x + w, y + h};
}

// The client area, the frame's size and its place; false when there is no frame or window.
bool Placement(int* clientWidth, int* clientHeight, int* frameWidth, int* frameHeight, Rect* placed) {
    if (!PresentsItself()) return false;
    HWND window = GameWindow();
    RECT client{};
    if (!window || !GetClientRect(window, &client)) return false;
    *clientWidth = client.right - client.left;
    *clientHeight = client.bottom - client.top;
    if (*clientWidth <= 0 || *clientHeight <= 0 || !GameTargetSize(frameWidth, frameHeight)) return false;
    *placed = Placed(*clientWidth, *clientHeight, *frameWidth, *frameHeight);
    return true;
}

#if defined(EI_RENDER_VALIDATION)
// Verify build: the back buffer just drawn against the frame through the ramp, at a grid of points
// (one to one placement only). Logged once per ramp.
void CheckRamp(int width, int height) {
    g_checkRamp = false;
    int fw = 0, fh = 0;
    if (!FrameSize(&fw, &fh) || fw != width || fh != height) return;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return;
    sg_d3d11_image_info info = sg_d3d11_query_image_info(FrameImage());
    auto* frame = static_cast<ID3D11Texture2D*>(const_cast<void*>(info.tex2d));
    ID3D11Texture2D* copies[2] = {nullptr, nullptr};
    ID3D11Texture2D* sources[2] = {back, frame};
    bool ok = frame != nullptr;
    for (int i = 0; ok && i < 2; ++i) {
        D3D11_TEXTURE2D_DESC desc{};
        sources[i]->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ok = SUCCEEDED(D3DDevice()->CreateTexture2D(&desc, nullptr, &copies[i]));
        if (ok) D3DContext()->CopyResource(copies[i], sources[i]);
    }
    D3D11_MAPPED_SUBRESOURCE mb{}, mf{};
    if (ok) ok = SUCCEEDED(D3DContext()->Map(copies[0], 0, D3D11_MAP_READ, 0, &mb));
    if (ok && FAILED(D3DContext()->Map(copies[1], 0, D3D11_MAP_READ, 0, &mf))) {
        D3DContext()->Unmap(copies[0], 0);
        ok = false;
    }
    if (ok) {
        int points = 0, differing = 0;
        for (int y = height / 34; y < height; y += height / 17 + 1) {
            for (int x = width / 34; x < width; x += width / 17 + 1) {
                const auto* b = static_cast<const std::uint8_t*>(mb.pData) + static_cast<std::size_t>(y) * mb.RowPitch + x * 4;
                const auto* f = static_cast<const std::uint8_t*>(mf.pData) + static_cast<std::size_t>(y) * mf.RowPitch + x * 4;
                // back buffer B, G, R; frame R, G, B
                int want[3] = {g_ramp[f[2] * 4 + 2], g_ramp[f[1] * 4 + 1], g_ramp[f[0] * 4 + 0]};
                for (int c = 0; c < 3; ++c) {
                    int d = static_cast<int>(b[c]) - want[c];
                    if (d > 1 || d < -1) {
                        ++differing;
                        break;
                    }
                }
                ++points;
            }
        }
        g_info.rampChecks++;
        g_info.rampPoints = points;
        g_info.rampDiffering = differing;
        if (differing) ei::Log("render: MISMATCH gamma ramp: %d of %d points of the presented frame differ", differing, points);
        else ei::Log("render: gamma ramp check: %d points of the presented frame match the ramp", points);
        D3DContext()->Unmap(copies[0], 0);
        D3DContext()->Unmap(copies[1], 0);
    }
    for (ID3D11Texture2D* c : copies)
        if (c) c->Release();
    back->Release();
    sg_reset_state_cache();
}
#endif

} // namespace

namespace detail {

bool DrawToWindow() {
    if (g_failed || DeviceIsLost()) return false;
    int clientWidth = 0, clientHeight = 0, frameWidth = 0, frameHeight = 0;
    Rect placed{};
    if (!Placement(&clientWidth, &clientHeight, &frameWidth, &frameHeight, &placed)) return false;
    if (!FrameTexture().id) return false;
    if (!EnsureChain(GameWindow(), clientWidth, clientHeight)) return false;
    if (g_rampDirty) {
        sg_image_data data{};
        data.mip_levels[0] = sg_range{g_ramp, sizeof(g_ramp)};
        sg_update_image(g_rampImage, &data);
        g_rampDirty = false;
    }
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;   // the bars black
    pass.action.colors[0].clear_value = sg_color{0.0f, 0.0f, 0.0f, 1.0f};
    pass.swapchain.width = clientWidth;
    pass.swapchain.height = clientHeight;
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_BGRA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_NONE;
    pass.swapchain.d3d11.render_view = g_view;
    pass.label = "ei-present";
    sg_begin_pass(&pass);
    sg_apply_viewport(placed.left, placed.top, placed.right - placed.left, placed.bottom - placed.top, true);
    sg_apply_pipeline(g_pipeline);
    sg_bindings b{};
    b.views[0] = FrameTexture();
    b.views[1] = g_rampView;
    b.samplers[0] = LinearSampler();
    sg_apply_bindings(&b);
    float uniforms[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    sg_range range{uniforms, sizeof(uniforms)};
    sg_apply_uniforms(0, &range);
    sg_draw(0, 3, 1);
    sg_end_pass();
    return true;
}

void PresentWindow() {
#if defined(EI_RENDER_VALIDATION)
    if (g_checkRamp && g_chain) CheckRamp(g_chainWidth, g_chainHeight);
#endif
    // A loss asked for by the harness (render::LoseDevice) stands in for the Present that would
    // have reported it.
    HRESULT hr = DXGI_ERROR_DEVICE_REMOVED;
    if (!TakeSimulatedLoss()) {
#if defined(EI_RENDER_VALIDATION)
        // A removal asked for (render::RemoveDevice) happens here, after the frame was drawn on a
        // working device: this Present reports it.
        RemoveIfAsked();
#endif
        hr = g_chain->Present(g_waitForVsync ? 1 : 0, 0);
    }
    if (IsDeviceLost(hr)) {
        DeviceLost("Present", hr);
        return;
    }
    // DXGI_STATUS_OCCLUDED (covered or minimised) is a success: nothing to do.
    if (SUCCEEDED(hr)) ++g_info.presents;
    if (FAILED(hr) && g_presentFailures++ < 10)
        ei::Log("render: the game window's Present failed (%08lX)", static_cast<unsigned long>(hr));
}

bool CurrentWindowParts(WindowParts* parts) {
    if (!g_chain || !g_view || !g_pipeline.id || !g_rampView.id || g_failed) return false;
    int clientWidth = 0, clientHeight = 0, frameWidth = 0, frameHeight = 0;
    Rect placed{};
    if (!Placement(&clientWidth, &clientHeight, &frameWidth, &frameHeight, &placed)) return false;
    *parts = WindowParts{g_chain, g_view, g_shader, g_pipeline, g_rampView, g_chainWidth, g_chainHeight, placed, g_waitForVsync};
    return true;
}

void DropWindow() {
    ReleaseChain();
    if (g_rampView.id) sg_destroy_view(g_rampView);
    if (g_rampImage.id) sg_destroy_image(g_rampImage);
    if (g_pipeline.id) sg_destroy_pipeline(g_pipeline);
    if (g_shader.id) sg_destroy_shader(g_shader);
    g_rampView = sg_view{};
    g_rampImage = sg_image{};
    g_pipeline = sg_pipeline{};
    g_shader = sg_shader{};
    g_rampDirty = true;   // the ramp the game set goes to the new device's lookup texture
    g_failed = false;
    g_recreated = false;
    g_info.presentsAtRecovery = g_info.presents;
}

} // namespace detail

void SetWaitForVsync(bool on) { g_waitForVsync = on; }

void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue) {
    // The ramp's full scale is the game's 65280 (255 * 256: 00504A60 multiplies by it); entries are
    // rounded to the 8 bits a channel of the frame has.
    const std::uint16_t* channels[3] = {red, green, blue};
    bool identity = true;
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            unsigned v = channels[c] ? (static_cast<unsigned>(channels[c][i]) * 255u + 32640u) / 65280u : static_cast<unsigned>(i);
            if (v > 255) v = 255;
            g_ramp[i * 4 + c] = static_cast<std::uint8_t>(v);
            identity = identity && v == static_cast<unsigned>(i);
        }
        g_ramp[i * 4 + 3] = 255;
    }
    if (identity != g_rampIdentity || !identity)
        ei::Log("render: gamma ramp %s (entry 64: %u %u %u, 128: %u %u %u)", identity ? "identity" : "set", g_ramp[256],
                g_ramp[257], g_ramp[258], g_ramp[512], g_ramp[513], g_ramp[514]);
    g_rampIdentity = identity;
    g_rampDirty = true;
    g_checkRamp = true;
}

WindowPresentInfo WindowPresents() {
    WindowPresentInfo info = g_info;
    info.rampCheckPending = g_checkRamp;
    info.rampIdentity = g_rampIdentity;
    info.failed = g_failed;
    DeviceLossCounts(&info);
    return info;
}

void GammaRamp(std::uint8_t* rgba256) { std::memcpy(rgba256, g_ramp, sizeof(g_ramp)); }

bool FrameInWindow(int* frameWidth, int* frameHeight, Rect* placed) {
    int cw = 0, ch = 0;
    return Placement(&cw, &ch, frameWidth, frameHeight, placed);
}

void WindowToFrame(int* x, int* y) {
    int fw = 0, fh = 0;
    Rect r{};
    if (!FrameInWindow(&fw, &fh, &r)) return;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w == fw && h == fh && r.left == 0 && r.top == 0) return;
    // Floor division, so that points left of / above the frame stay negative.
    long long px = static_cast<long long>(*x - r.left) * fw, py = static_cast<long long>(*y - r.top) * fh;
    *x = static_cast<int>(px >= 0 ? px / w : -((-px + w - 1) / w));
    *y = static_cast<int>(py >= 0 ? py / h : -((-py + h - 1) / h));
}

void FrameToWindow(int* x, int* y) {
    int fw = 0, fh = 0;
    Rect r{};
    if (!FrameInWindow(&fw, &fh, &r)) return;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w == fw && h == fh && r.left == 0 && r.top == 0) return;
    // The centre of the window pixels that map to the frame pixel.
    *x = r.left + static_cast<int>((static_cast<long long>(*x) * w * 2 + w) / (2LL * fw));
    *y = r.top + static_cast<int>((static_cast<long long>(*y) * h * 2 + h) / (2LL * fh));
}

} // namespace render
