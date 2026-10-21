// The software cursor between frames in the switch-over without DirectDraw (R9c2; docs/notes/renderer.md,
// section 9). The game's cursor thread (005E5690, every 41 ms) drew the cursor straight onto the
// primary surface when it moved, so it kept moving while the main thread drew no frame (a long frame,
// a blocking load). Without DirectDraw the cursor thread hands the move here instead
// (render::CursorBetweenFrames): the last frame presented on the game window, which holds no cursor
// (the present draws it over the frame), is presented again with the cursor at its new place.
//
// The main thread keeps using sokol alone. The cursor thread makes no sokol call: it records its
// present on a Direct3D 11 deferred context with the raw objects of sokol's present pipeline (the frame
// target and the cursor through the gamma ramp into the back buffer: the draw the main thread's present
// makes) and runs it as one command list on the immediate context (ExecuteCommandList, which puts the
// context's state back, so sokol's state cache stays right), with the device's multithread protection
// on. The swap chain and the frame target are shared: the main thread's frame draw and present on the
// game window and the cursor thread's present take turns under one lock, and the main thread takes the
// references the cursor thread draws with at the end of each of its presents (and drops them when it
// begins the next frame's draws: the frame target is partly drawn until then).
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
ID3D11DeviceContext* g_deferred = nullptr;   // the cursor thread's deferred context

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
// game window (references held); `valid` false while there is none (or a frame is being drawn).
struct Kit {
    bool valid = false;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain1* chain = nullptr;
    ID3D11RenderTargetView* back = nullptr;
    ID3D11ShaderResourceView* ramp = nullptr;
    ID3D11SamplerState* linear = nullptr;
    ID3D11ShaderResourceView* frame = nullptr;
    RawPipeline present;
    int clientWidth = 0, clientHeight = 0;
    int frameWidth = 0, frameHeight = 0;
    const std::uint8_t* rampTable = nullptr;   // the ramp as uploaded (the main thread's, read under the lock)
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
    Release(g_kit.frame);
    g_kit.present.Drop();
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
// Verify build: the back buffer the cursor thread drew, read back and checked against the frame, the
// cursor's texels where their alpha is a half or more and the gamma ramp as uploaded (one to one
// placement only; a channel off by more than one counts as a difference); kept for the harness
// (ReadCursorPresent).
std::vector<std::uint8_t> g_lastBgr;
int g_lastWidth = 0, g_lastHeight = 0;
ID3D11Texture2D* g_readback[2] = {nullptr, nullptr};   // the back buffer and the frame

ID3D11Texture2D* TextureOf(ID3D11View* view) {
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource) {
        resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        resource->Release();
    }
    return texture;
}

void CheckPresented(const Kit& kit, const Rect& to, const std::vector<std::uint8_t>& cursor) {
    if (kit.placed.left != 0 || kit.placed.top != 0 || kit.placed.right != kit.frameWidth || kit.placed.bottom != kit.frameHeight ||
        kit.clientWidth != kit.frameWidth || kit.clientHeight != kit.frameHeight)
        return;
    ID3D11Texture2D* sources[2] = {TextureOf(kit.back), TextureOf(kit.frame)};
    bool ok = sources[0] && sources[1];
    for (int i = 0; ok && i < 2; ++i) {
        ID3D11Texture2D*& r = g_readback[i];
        D3D11_TEXTURE2D_DESC desc{};
        sources[i]->GetDesc(&desc);
        if (r) {
            D3D11_TEXTURE2D_DESC have{};
            r->GetDesc(&have);
            if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format) Release(r);
        }
        if (!r) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            ok = SUCCEEDED(kit.device->CreateTexture2D(&desc, nullptr, &r));
        }
        if (ok) kit.context->CopyResource(r, sources[i]);
    }
    D3D11_MAPPED_SUBRESOURCE back{}, frame{};
    bool mapped0 = ok && SUCCEEDED(kit.context->Map(g_readback[0], 0, D3D11_MAP_READ, 0, &back));
    bool mapped1 = mapped0 && SUCCEEDED(kit.context->Map(g_readback[1], 0, D3D11_MAP_READ, 0, &frame));
    if (mapped1) {
        int width = kit.frameWidth, height = kit.frameHeight;
        int cw = to.right - to.left;
        int differing = 0;
        g_lastBgr.resize(static_cast<std::size_t>(width) * height * 3);
        const std::uint8_t* table = kit.rampTable;
        for (int y = 0; y < height; ++y) {
            const auto* b = static_cast<const std::uint8_t*>(back.pData) + static_cast<std::size_t>(y) * back.RowPitch;   // B, G, R, A
            const auto* f = static_cast<const std::uint8_t*>(frame.pData) + static_cast<std::size_t>(y) * frame.RowPitch;   // R, G, B, A
            std::uint8_t* out = &g_lastBgr[static_cast<std::size_t>(y) * width * 3];
            for (int x = 0; x < width; ++x) {
                const std::uint8_t* want = f + x * 4;
                if (x >= to.left && x < to.right && y >= to.top && y < to.bottom) {
                    const std::uint8_t* t = &cursor[(static_cast<std::size_t>(y - to.top) * cw + (x - to.left)) * 4];
                    if (t[3] >= 128) want = t;
                }
                int wantB = table[want[2] * 4 + 2], wantG = table[want[1] * 4 + 1], wantR = table[want[0] * 4 + 0];
                const std::uint8_t* p = b + x * 4;
                if (p[0] - wantB > 1 || wantB - p[0] > 1 || p[1] - wantG > 1 || wantG - p[1] > 1 || p[2] - wantR > 1 || wantR - p[2] > 1)
                    ++differing;
                out[x * 3 + 0] = p[0];
                out[x * 3 + 1] = p[1];
                out[x * 3 + 2] = p[2];
            }
        }
        g_lastWidth = width;
        g_lastHeight = height;
        ++g_info.checks;
        if (differing) {
            ++g_info.mismatches;
            if (g_info.mismatches <= 10)
                ei::Log("render: MISMATCH cursor between frames: %d pixels of the presented frame differ (cursor at %d,%d-%d,%d)",
                        differing, to.left, to.top, to.right, to.bottom);
        } else if (g_info.checks == 1 || g_info.checks % 50 == 0) {
            ei::Log("render: cursor between frames: %d presented frames checked, %d differed", g_info.checks, g_info.mismatches);
        }
    }
    if (mapped1) kit.context->Unmap(g_readback[1], 0);
    if (mapped0) kit.context->Unmap(g_readback[0], 0);
    for (ID3D11Texture2D* t : sources)
        if (t) t->Release();
}
#endif

// Records and runs the cursor thread's present: the frame target (it holds no cursor) and the cursor at
// `to` through the gamma ramp into the swap chain's back buffer as the main thread's present draws them
// (shaders/present.hlsl), Present.
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
    // The present's cbuffers (render_window.cpp): the whole frame; the cursor's rectangle, the frame's
    // size and the cursor texels per frame pixel (one), the cursor texture's size.
    float presentVs[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    float presentPs[12] = {static_cast<float>(to.left), static_cast<float>(to.top), static_cast<float>(to.right),
                           static_cast<float>(to.bottom), static_cast<float>(kit.frameWidth), static_cast<float>(kit.frameHeight),
                           1.0f, 1.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 0.0f};
    ID3D11Buffer* buffers[2] = {ConstantBuffer(device, presentVs, sizeof(presentVs)), ConstantBuffer(device, presentPs, sizeof(presentPs))};
    bool ok = buffers[0] && buffers[1] && SUCCEEDED(device->CreateTexture2D(&td, &init, &texture)) &&
              SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view));
    ID3D11CommandList* list = nullptr;
    if (ok) {
        ID3D11DeviceContext* dc = g_deferred;
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
        dc->VSSetConstantBuffers(0, 1, &buffers[0]);
        dc->PSSetConstantBuffers(0, 1, &buffers[1]);
        ID3D11ShaderResourceView* views[3] = {kit.frame, kit.ramp, view};
        dc->PSSetShaderResources(0, 3, views);
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
    CheckPresented(kit, to, cursor);
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

void EndWindowPresent(bool presented) {
    if (presented) g_lastPresent = GetTickCount64();
    WindowParts window{};
    if (presented && g_protected && CurrentWindowParts(&window)) {
        g_kit.device = Referenced<ID3D11Device>(D3DDevice());
        g_kit.context = Referenced<ID3D11DeviceContext>(D3DContext());
        g_kit.chain = Referenced<IDXGISwapChain1>(window.chain);
        g_kit.back = Referenced<ID3D11RenderTargetView>(window.view);
        g_kit.frame = Referenced<ID3D11ShaderResourceView>(sg_d3d11_query_view_info(window.frame).srv);
        g_kit.ramp = Referenced<ID3D11ShaderResourceView>(sg_d3d11_query_view_info(window.ramp).srv);
        g_kit.linear = Referenced<ID3D11SamplerState>(sg_d3d11_query_sampler_info(LinearSampler()).smp);
        bool pipelines = g_kit.present.Take(window.shader, window.pipeline);
        g_kit.clientWidth = window.clientWidth;
        g_kit.clientHeight = window.clientHeight;
        g_kit.frameWidth = window.frameWidth;
        g_kit.frameHeight = window.frameHeight;
        g_kit.rampTable = window.rampTable;
        g_kit.placed = window.placed;
        g_kit.vsync = window.vsync;
        g_kit.valid = pipelines && g_kit.device && g_kit.context && g_kit.frame && g_kit.ramp && g_kit.linear;
        if (!g_kit.valid) DropKit();
    }
    g_presentLock.Leave();
}

void DropCursorPresents() {
    Hold hold(g_presentLock);
    DropKit();
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
    if (!cut) return Refused(Refusal::Picture);
    if (!g_kit.valid) return Refused(Refusal::NoFrame);
    if (GetTickCount64() - g_lastPresent < kBetweenFrames) return Refused(Refusal::Recent);
    // The cursor's rectangle is clipped to its bounds by the game (005E52E0); kept inside the frame.
    if (to.left < 0 || to.top < 0 || to.right > g_kit.frameWidth || to.bottom > g_kit.frameHeight ||
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
