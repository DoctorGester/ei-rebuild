// The software cursor between frames in the switch-over without DirectDraw (R9c2; docs/notes/renderer.md,
// section 9). The game's cursor thread (005E5690, every 41 ms) drew the cursor straight onto the
// primary surface when it moved, so it kept moving while the main thread drew no frame (a long frame,
// a blocking load). Without DirectDraw the cursor thread hands the move here instead
// (render::CursorBetweenFrames): the last frame presented on the game window, as it was before its
// cursor, is presented again with the cursor at its new place.
//
// The main thread keeps using sokol alone. The cursor thread makes no sokol call: it records its
// draws on a Direct3D 11 deferred context with the raw objects of sokol's pipelines (the window
// blit that draws the cursor in the frame, the present that draws the frame through the gamma ramp)
// and runs them as one command list on the immediate context (ExecuteCommandList, which puts the
// context's state back, so sokol's state cache stays right), with the device's multithread
// protection on. The swap chain is shared: the main thread's present on the game window and the
// cursor thread's take turns under one lock, and the main thread takes the references the cursor
// thread draws with at the end of each of its presents.
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace render {

namespace {

using namespace detail;

// The main thread's frames show the cursor themselves: the cursor thread presents only when none was
// presented for this long (its own period is 41 ms).
constexpr ULONGLONG kBetweenFrames = 40;

class Lock {
public:
    Lock() { InitializeCriticalSection(&section_); }
    ~Lock() { DeleteCriticalSection(&section_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    void Enter() { EnterCriticalSection(&section_); }
    void Leave() { LeaveCriticalSection(&section_); }

private:
    CRITICAL_SECTION section_;
};

class Hold {
public:
    explicit Hold(Lock& lock) : lock_(lock) { lock_.Enter(); }
    ~Hold() { lock_.Leave(); }
    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;

private:
    Lock& lock_;
};

template <typename T>
void Release(T*& object) {
    if (object) object->Release();
    object = nullptr;
}

template <typename T>
T* Referenced(const void* object) {
    auto* typed = static_cast<T*>(const_cast<void*>(object));
    if (typed) typed->AddRef();
    return typed;
}

// --- The cursor's picture (both threads) ------------------------------------------------------------

Lock g_sheetLock;
struct Sheet {
    const void* holder = nullptr;
    const void* mask = nullptr;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;   // alpha 255: the image's texel, 0: the background shows
} g_sheet;

// The part `from` of the sheet; false when it does not lie inside.
bool CutSheet(const Rect& from, std::vector<std::uint8_t>* rgba) {
    if (from.left < 0 || from.top < 0 || from.right > g_sheet.width || from.bottom > g_sheet.height || from.right <= from.left ||
        from.bottom <= from.top)
        return false;
    int w = from.right - from.left, h = from.bottom - from.top;
    rgba->resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(&(*rgba)[static_cast<std::size_t>(y) * w * 4],
                    &g_sheet.rgba[(static_cast<std::size_t>(from.top + y) * g_sheet.width + from.left) * 4], static_cast<std::size_t>(w) * 4);
    return true;
}

// --- The swap chain's turns and what the cursor thread draws with ---------------------------------

Lock g_presentLock;
bool g_protected = false;          // the device's multithread protection is on
ULONGLONG g_lastPresent = 0;       // real time of the main thread's last present on the game window

// The frame without its cursor (the main thread copies it each frame), the cursor thread's work
// target of the same size (the frame with the cursor moved), the deferred context it records on.
ID3D11Texture2D* g_clean = nullptr;
ID3D11Texture2D* g_work = nullptr;
ID3D11RenderTargetView* g_workTarget = nullptr;
ID3D11ShaderResourceView* g_workView = nullptr;
ID3D11DeviceContext* g_deferred = nullptr;
int g_frameWidth = 0, g_frameHeight = 0;

// The raw objects of a sokol shader and pipeline.
struct RawPipeline {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11RasterizerState* rasterizer = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;

    bool Take(sg_shader shader, sg_pipeline pipeline) {
        sg_d3d11_shader_info s = sg_d3d11_query_shader_info(shader);
        sg_d3d11_pipeline_info p = sg_d3d11_query_pipeline_info(pipeline);
        vs = Referenced<ID3D11VertexShader>(s.vs);
        ps = Referenced<ID3D11PixelShader>(s.fs);
        layout = Referenced<ID3D11InputLayout>(p.il);
        rasterizer = Referenced<ID3D11RasterizerState>(p.rs);
        blend = Referenced<ID3D11BlendState>(p.bs);
        depth = Referenced<ID3D11DepthStencilState>(p.dss);
        return vs && ps && rasterizer && blend && depth;
    }
    void Drop() {
        Release(vs);
        Release(ps);
        Release(layout);
        Release(rasterizer);
        Release(blend);
        Release(depth);
    }
    void Bind(ID3D11DeviceContext* context) const {
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs, nullptr, 0);
        context->PSSetShader(ps, nullptr, 0);
        context->RSSetState(rasterizer);
        context->OMSetBlendState(blend, nullptr, 0xFFFFFFFFu);
        context->OMSetDepthStencilState(depth, 0);
    }
};

// What the cursor thread draws with, taken at the end of the main thread's last present on the
// game window (references held); `valid` false while there is none.
struct Kit {
    bool valid = false;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain1* chain = nullptr;
    ID3D11RenderTargetView* back = nullptr;
    ID3D11ShaderResourceView* ramp = nullptr;
    ID3D11SamplerState* linear = nullptr;
    ID3D11SamplerState* nearest = nullptr;
    RawPipeline blit, present;
    int clientWidth = 0, clientHeight = 0;
    Rect placed{0, 0, 0, 0};
    bool vsync = true;
} g_kit;

void DropKit() {
    g_kit.valid = false;
    Release(g_kit.device);
    Release(g_kit.context);
    Release(g_kit.chain);
    Release(g_kit.back);
    Release(g_kit.ramp);
    Release(g_kit.linear);
    Release(g_kit.nearest);
    g_kit.blit.Drop();
    g_kit.present.Drop();
}

void DropTargets() {
    Release(g_workView);
    Release(g_workTarget);
    Release(g_work);
    Release(g_clean);
    g_frameWidth = g_frameHeight = 0;
}

// The two frame-sized textures for a frame of `desc` (the off-screen target's).
bool MakeTargets(const D3D11_TEXTURE2D_DESC& frame) {
    DropTargets();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = frame.Width;
    desc.Height = frame.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = frame.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Device* device = D3DDevice();
    bool ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &g_clean));
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ok = ok && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &g_work)) &&
         SUCCEEDED(device->CreateRenderTargetView(g_work, nullptr, &g_workTarget)) &&
         SUCCEEDED(device->CreateShaderResourceView(g_work, nullptr, &g_workView));
    if (!ok) {
        DropTargets();
        return false;
    }
    g_frameWidth = static_cast<int>(frame.Width);
    g_frameHeight = static_cast<int>(frame.Height);
    return true;
}

ID3D11Buffer* ConstantBuffer(ID3D11Device* device, const void* data, UINT bytes) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{data, 0, 0};
    ID3D11Buffer* buffer = nullptr;
    if (FAILED(device->CreateBuffer(&desc, &init, &buffer))) return nullptr;
    return buffer;
}

CursorPresentInfo g_info;

// Why the cursor thread did not present (counted; the first few of each logged).
enum class Refusal { Picture, NoFrame, Recent, Outside };
bool Refused(Refusal why) {
    static const char* const kWhy[] = {"the cursor's picture was not drawn by a frame yet", "no frame presented on the game window",
                                       "a frame was presented less than 40 ms ago", "the rectangle is not inside the frame"};
    int i = static_cast<int>(why);
    ++g_info.refused[i];
    if (why != Refusal::Recent && g_info.refused[i] <= 3) ei::Log("render: cursor between frames: not presented: %s", kWhy[i]);
    return false;
}

#if defined(EI_RENDER_VALIDATION)
// Verify build: the frame the cursor thread composed, read back and checked against the frame
// without its cursor and the cursor's texels; kept for the harness (ReadCursorPresent).
std::vector<std::uint8_t> g_lastBgr;
int g_lastWidth = 0, g_lastHeight = 0;
ID3D11Texture2D* g_readback[2] = {nullptr, nullptr};   // the work target and the clean frame

void CheckComposed(const Kit& kit, const Rect& to, const std::vector<std::uint8_t>& cursor) {
    D3D11_TEXTURE2D_DESC desc{};
    g_work->GetDesc(&desc);
    for (ID3D11Texture2D*& r : g_readback) {
        if (r) {
            D3D11_TEXTURE2D_DESC have{};
            r->GetDesc(&have);
            if (have.Width != desc.Width || have.Height != desc.Height) Release(r);
        }
        if (!r) {
            D3D11_TEXTURE2D_DESC staging = desc;
            staging.Usage = D3D11_USAGE_STAGING;
            staging.BindFlags = 0;
            staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(kit.device->CreateTexture2D(&staging, nullptr, &r))) return;
        }
    }
    kit.context->CopyResource(g_readback[0], g_work);
    kit.context->CopyResource(g_readback[1], g_clean);
    D3D11_MAPPED_SUBRESOURCE work{}, clean{};
    if (FAILED(kit.context->Map(g_readback[0], 0, D3D11_MAP_READ, 0, &work))) return;
    if (FAILED(kit.context->Map(g_readback[1], 0, D3D11_MAP_READ, 0, &clean))) {
        kit.context->Unmap(g_readback[0], 0);
        return;
    }
    int width = static_cast<int>(desc.Width), height = static_cast<int>(desc.Height);
    int cw = to.right - to.left;
    int differing = 0;
    g_lastBgr.resize(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        const auto* w = static_cast<const std::uint8_t*>(work.pData) + static_cast<std::size_t>(y) * work.RowPitch;
        const auto* c = static_cast<const std::uint8_t*>(clean.pData) + static_cast<std::size_t>(y) * clean.RowPitch;
        std::uint8_t* out = &g_lastBgr[static_cast<std::size_t>(y) * width * 3];
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* want = c + x * 4;
            if (x >= to.left && x < to.right && y >= to.top && y < to.bottom) {
                const std::uint8_t* t = &cursor[(static_cast<std::size_t>(y - to.top) * cw + (x - to.left)) * 4];
                if (t[3] >= 128) want = t;
            }
            if (w[x * 4] != want[0] || w[x * 4 + 1] != want[1] || w[x * 4 + 2] != want[2]) ++differing;
            out[x * 3 + 0] = w[x * 4 + 2];
            out[x * 3 + 1] = w[x * 4 + 1];
            out[x * 3 + 2] = w[x * 4 + 0];
        }
    }
    kit.context->Unmap(g_readback[0], 0);
    kit.context->Unmap(g_readback[1], 0);
    g_lastWidth = width;
    g_lastHeight = height;
    ++g_info.checks;
    if (differing) {
        ++g_info.mismatches;
        if (g_info.mismatches <= 10)
            ei::Log("render: MISMATCH cursor between frames: %d pixels of the composed frame differ (cursor at %d,%d-%d,%d)",
                    differing, to.left, to.top, to.right, to.bottom);
    } else if (g_info.checks == 1 || g_info.checks % 50 == 0) {
        ei::Log("render: cursor between frames: %d composed frames checked, %d differed", g_info.checks, g_info.mismatches);
    }
}
#endif

// Records and runs the cursor thread's present: the frame without its cursor, the cursor drawn over
// it at `to` (the window blit, one to one), that frame through the gamma ramp into the swap chain's
// back buffer as the main thread's present draws it, Present.
bool PresentWithCursor(const Kit& kit, const Rect& to, const std::vector<std::uint8_t>& cursor) {
    int w = to.right - to.left, h = to.bottom - to.top;
    ID3D11Device* device = kit.device;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{cursor.data(), static_cast<UINT>(w * 4), 0};
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    // The window blit's cbuffers (render_blit.cpp kBlitSource): the rectangle and the frame's size;
    // the destination's corner, one texel a pixel, the source corner and the texture's size. The
    // present's (render_window.cpp): the whole frame.
    float blitVs[8] = {static_cast<float>(to.left), static_cast<float>(to.top), static_cast<float>(to.right), static_cast<float>(to.bottom),
                       static_cast<float>(g_frameWidth), static_cast<float>(g_frameHeight), 0.0f, 0.0f};
    float blitPs[8] = {static_cast<float>(to.left), static_cast<float>(to.top), 1.0f, 1.0f, 0.0f, 0.0f, static_cast<float>(w),
                       static_cast<float>(h)};
    float presentVs[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    ID3D11Buffer* buffers[3] = {ConstantBuffer(device, blitVs, sizeof(blitVs)), ConstantBuffer(device, blitPs, sizeof(blitPs)),
                                ConstantBuffer(device, presentVs, sizeof(presentVs))};
    bool ok = buffers[0] && buffers[1] && buffers[2] && SUCCEEDED(device->CreateTexture2D(&td, &init, &texture)) &&
              SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view));
    ID3D11CommandList* list = nullptr;
    if (ok) {
        ID3D11DeviceContext* dc = g_deferred;
        dc->CopyResource(g_work, g_clean);
        // The cursor into the frame (the window blit: texels with alpha below one half left out).
        D3D11_VIEWPORT frame{0.0f, 0.0f, static_cast<float>(g_frameWidth), static_cast<float>(g_frameHeight), 0.0f, 1.0f};
        D3D11_RECT frameRect{0, 0, g_frameWidth, g_frameHeight};
        dc->OMSetRenderTargets(1, &g_workTarget, nullptr);
        dc->RSSetViewports(1, &frame);
        dc->RSSetScissorRects(1, &frameRect);
        kit.blit.Bind(dc);
        dc->VSSetConstantBuffers(0, 1, &buffers[0]);
        dc->PSSetConstantBuffers(0, 1, &buffers[1]);
        dc->PSSetShaderResources(0, 1, &view);
        dc->PSSetSamplers(0, 1, &kit.nearest);
        dc->Draw(6, 0);
        // The frame through the ramp into the back buffer (black bars around it).
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        dc->OMSetRenderTargets(1, &kit.back, nullptr);
        dc->ClearRenderTargetView(kit.back, black);
        D3D11_VIEWPORT placed{static_cast<float>(kit.placed.left), static_cast<float>(kit.placed.top),
                              static_cast<float>(kit.placed.right - kit.placed.left),
                              static_cast<float>(kit.placed.bottom - kit.placed.top), 0.0f, 1.0f};
        D3D11_RECT client{0, 0, kit.clientWidth, kit.clientHeight};
        dc->RSSetViewports(1, &placed);
        dc->RSSetScissorRects(1, &client);
        kit.present.Bind(dc);
        dc->VSSetConstantBuffers(0, 1, &buffers[2]);
        ID3D11ShaderResourceView* views[2] = {g_workView, kit.ramp};
        dc->PSSetShaderResources(0, 2, views);
        ID3D11SamplerState* samplers[1] = {kit.linear};
        dc->PSSetSamplers(0, 1, samplers);
        dc->Draw(3, 0);
        ok = SUCCEEDED(dc->FinishCommandList(FALSE, &list));
    }
    if (ok) kit.context->ExecuteCommandList(list, TRUE);
    Release(list);
    Release(view);
    Release(texture);
    for (ID3D11Buffer*& b : buffers) Release(b);
    if (!ok) return false;
#if defined(EI_RENDER_VALIDATION)
    CheckComposed(kit, to, cursor);
#endif
    HRESULT hr = kit.chain->Present(kit.vsync ? 1 : 0, 0);
    if (FAILED(hr)) {
        // A lost device is found by the main thread at its next present (GetDeviceRemovedReason).
        static int failures = 0;
        if (failures++ < 10) ei::Log("render: the cursor thread's Present failed (%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

} // namespace

namespace detail {

bool CursorPicturePart(const void* holder, const void* mask, int maskBytes, const Picture& picture, const Rect& from,
                       std::vector<std::uint8_t>* rgba) {
    Hold hold(g_sheetLock);
    if (g_sheet.holder != holder || g_sheet.mask != mask || g_sheet.width != picture.width || g_sheet.height != picture.height) {
        g_sheet.holder = holder;
        g_sheet.mask = mask;
        g_sheet.width = picture.width;
        g_sheet.height = picture.height;
        g_sheet.rgba = picture.rgba;
        g_sheet.rgba.resize(static_cast<std::size_t>(picture.width) * picture.height * 4);
        const auto* m = static_cast<const std::uint8_t*>(mask);
        for (std::size_t i = 0; i < static_cast<std::size_t>(picture.width) * picture.height; ++i) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, m + i * maskBytes, static_cast<std::size_t>(maskBytes));
            g_sheet.rgba[i * 4 + 3] = bits == 0 ? 255 : 0;   // the mask is all ones where the image is black
        }
    }
    return CutSheet(from, rgba);
}

void ProtectDevice(ID3D11Device* device, ID3D11DeviceContext* context) {
    Hold hold(g_presentLock);
    g_protected = false;
    ID3D11Multithread* multithread = nullptr;
    if (context && SUCCEEDED(context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&multithread)))) {
        multithread->SetMultithreadProtected(TRUE);
        multithread->Release();
        g_protected = SUCCEEDED(device->CreateDeferredContext(0, &g_deferred));
    }
    if (!g_protected) ei::Log("render: no multithread protection or deferred context; the cursor does not move between frames");
}

void BeginWindowPresent() {
    g_presentLock.Enter();
    // The back buffer's view is not held while the swap chain may be resized.
    DropKit();
}

void KeepFrameForCursor() {
    if (!g_protected) return;
    sg_d3d11_image_info info = sg_d3d11_query_image_info(FrameImage());
    auto* frame = static_cast<ID3D11Texture2D*>(const_cast<void*>(info.tex2d));
    if (!frame) return;
    D3D11_TEXTURE2D_DESC desc{};
    frame->GetDesc(&desc);
    if ((!g_clean || g_frameWidth != static_cast<int>(desc.Width) || g_frameHeight != static_cast<int>(desc.Height)) &&
        !MakeTargets(desc))
        return;
    D3DContext()->CopyResource(g_clean, frame);
}

void EndWindowPresent(bool presented) {
    if (presented) g_lastPresent = GetTickCount64();
    WindowParts window{};
    if (presented && g_protected && g_clean && CurrentWindowParts(&window)) {
        sg_shader blitShader{};
        sg_pipeline blitPipeline{};
        sg_sampler nearest{};
        BlitParts(&blitShader, &blitPipeline, &nearest);
        g_kit.device = Referenced<ID3D11Device>(D3DDevice());
        g_kit.context = Referenced<ID3D11DeviceContext>(D3DContext());
        g_kit.chain = Referenced<IDXGISwapChain1>(window.chain);
        g_kit.back = Referenced<ID3D11RenderTargetView>(window.view);
        g_kit.ramp = Referenced<ID3D11ShaderResourceView>(sg_d3d11_query_view_info(window.ramp).srv);
        g_kit.linear = Referenced<ID3D11SamplerState>(sg_d3d11_query_sampler_info(LinearSampler()).smp);
        g_kit.nearest = Referenced<ID3D11SamplerState>(sg_d3d11_query_sampler_info(nearest).smp);
        bool pipelines = g_kit.blit.Take(blitShader, blitPipeline) && g_kit.present.Take(window.shader, window.pipeline);
        g_kit.clientWidth = window.clientWidth;
        g_kit.clientHeight = window.clientHeight;
        g_kit.placed = window.placed;
        g_kit.vsync = window.vsync;
        g_kit.valid = pipelines && g_kit.device && g_kit.context && g_kit.ramp && g_kit.linear && g_kit.nearest;
        if (!g_kit.valid) DropKit();
    }
    g_presentLock.Leave();
}

void DropCursorPresents() {
    Hold hold(g_presentLock);
    DropKit();
    DropTargets();
    Release(g_deferred);
#if defined(EI_RENDER_VALIDATION)
    for (ID3D11Texture2D*& r : g_readback) Release(r);
#endif
    g_protected = false;
}

} // namespace detail

bool CursorBetweenFrames(const void* imageHolder, const void* mask, const Rect& from, const Rect& to) {
    if (!imageHolder || !mask) return false;
    std::vector<std::uint8_t> rgba;
    bool cut = false;
    {
        Hold hold(g_sheetLock);
        cut = g_sheet.holder == imageHolder && g_sheet.mask == mask && CutSheet(from, &rgba);
    }
    Hold hold(g_presentLock);
    ++g_info.calls;
#if defined(EI_RENDER_VALIDATION)
    if (g_info.calls <= 200)
        ei::Log("render: cursor move %u at %d,%d (main thread's present %llu ms ago)", g_info.calls, to.left, to.top,
                GetTickCount64() - g_lastPresent);
#endif
    if (!cut) return Refused(Refusal::Picture);
    if (!g_kit.valid || !g_clean) return Refused(Refusal::NoFrame);
    if (GetTickCount64() - g_lastPresent < kBetweenFrames) return Refused(Refusal::Recent);
    // The cursor's rectangle is clipped to its bounds by the game (005E52E0); kept inside the frame.
    if (to.left < 0 || to.top < 0 || to.right > g_frameWidth || to.bottom > g_frameHeight ||
        to.right - to.left != from.right - from.left || to.bottom - to.top != from.bottom - from.top)
        return Refused(Refusal::Outside);
    FpuGuard fpu;
    if (!PresentWithCursor(g_kit, to, rgba)) {
        DropKit();   // until the main thread's next present
        return false;
    }
    ++g_info.presents;
    g_info.lastTo = to;
    return true;
}

CursorPresentInfo CursorPresents() {
    Hold hold(g_presentLock);
    return g_info;
}

bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr) {
#if defined(EI_RENDER_VALIDATION)
    Hold hold(g_presentLock);
    if (g_lastBgr.empty()) return false;
    *width = g_lastWidth;
    *height = g_lastHeight;
    *bgr = g_lastBgr;
    return true;
#else
    (void)width;
    (void)height;
    (void)bgr;
    return false;
#endif
}

} // namespace render
