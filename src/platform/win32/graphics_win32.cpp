// The Direct3D 11 backend of the renderer's device and window (graphics.h, graphics_win32.h; docs/notes/platform.md,
// "Graphics"), and sokol_gfx's implementation on it.

#include "platform/win32/graphics_win32.h"

#include "hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#if defined(EI_RENDER_VALIDATION)
#include <d3d11on12.h>
#include <d3d12.h>
#endif

#include <cstdio>

// sokol_gfx (third_party/sokol, unmodified; SOKOL_D3D11 from CMakeLists.txt): its implementation, after its
// declarations came through graphics.h. Its own warnings are not ours to fix.
#define SOKOL_IMPL
#if defined(EI_RENDER_VALIDATION)
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL   // a failed check skips the call and is logged; the game goes on
#endif
#pragma warning(push, 0)
#include "sokol_gfx.h"
#pragma warning(pop)

namespace platform::graphics {

namespace {

// ---- The device ---------------------------------------------------------------------------------------------------

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
char g_error[160] = "";
HRESULT g_failure = S_OK;   // a lost device reported by Present or ResizeBuffers (Lost)

bool IsLoss(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

// The call `where` found the device lost: nothing more is drawn into the swap chain, and the renderer makes everything
// again at the end of the frame.
void LostBy(const char* where, HRESULT hr) {
    ei::Log("render: the Direct3D 11 device was lost (%s: %08lX, reason %08lX)", where, static_cast<unsigned long>(hr),
            static_cast<unsigned long>(g_device->GetDeviceRemovedReason()));
    g_failure = hr;
}

// The shaders are shader model 5 (sokol-shdc's hlsl5): feature level 11_0 or no device.
const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_0};
constexpr UINT kLevelCount = static_cast<UINT>(sizeof(kLevels) / sizeof(kLevels[0]));

HRESULT CreatePlainDevice(ID3D11Device** device, ID3D11DeviceContext** context, D3D_FEATURE_LEVEL* got) {
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels,
                             kLevelCount, D3D11_SDK_VERSION, device, got, context);
}

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

#if defined(EI_RENDER_VALIDATION)
// ---- The harness build's removable device -------------------------------------------------------------------------

bool g_makeRemovable = false;
ID3D12Device* g_d3d12 = nullptr;
ID3D12CommandQueue* g_queue = nullptr;
bool g_removeAsked = false;   // RequestRemoval: removed right before the next present (RemoveIfAsked)
int g_removals = 0;

void ReleaseRemovable() {
    if (g_queue) g_queue->Release();
    g_queue = nullptr;
    if (g_d3d12) {
        ULONG left = g_d3d12->Release();
        if (left)
            ei::Log("render: the Direct3D 12 device under the removable one keeps %lu references",
                    static_cast<unsigned long>(left));
    }
    g_d3d12 = nullptr;
}

HRESULT CreateRemovableDevice(D3D_FEATURE_LEVEL* got) {
    HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    auto create12 =
        d3d12 ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
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
    // Direct3D 12 devices are one per adapter while one lives: a removed one still referenced somewhere would come
    // back here.
    HRESULT removed = g_d3d12->GetDeviceRemovedReason();
    if (FAILED(removed)) {
        ei::Log("render: removable device: D3D12CreateDevice gave a removed device (%08lX)",
                static_cast<unsigned long>(removed));
        ReleaseRemovable();
        return removed;
    }
    D3D12_COMMAND_QUEUE_DESC queue{};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = g_d3d12->CreateCommandQueue(&queue, IID_PPV_ARGS(&g_queue));
    if (SUCCEEDED(hr)) {
        IUnknown* queues[] = {g_queue};
        hr = create11on12(g_d3d12, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels, kLevelCount, queues, 1, 0, &g_device,
                          &g_context, got);
    }
    if (FAILED(hr)) {
        ei::Log("render: removable device: D3D11On12CreateDevice failed (%08lX)", static_cast<unsigned long>(hr));
        g_device = nullptr;
        g_context = nullptr;
        ReleaseRemovable();
    }
    return hr;
}

void RemoveIfAsked() {
    if (!g_removeAsked || !g_d3d12) return;
    g_removeAsked = false;
    ID3D12Device5* device5 = nullptr;
    if (FAILED(g_d3d12->QueryInterface(IID_PPV_ARGS(&device5)))) return;
    // The GPU finishes what it was given first. Removed with work in flight, the old device's teardown crashed in a
    // thread of D3D11On12 / DXGI / the driver, also in a small program that presented on its own window; with the GPU
    // idle it never did (docs/notes/renderer.md, "a real removal of the device").
    int waited = 0;
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
    ID3D11Query* idle = nullptr;
    if (SUCCEEDED(g_device->CreateQuery(&desc, &idle))) {
        g_context->End(idle);
        g_context->Flush();
        BOOL done = FALSE;
        while (g_context->GetData(idle, &done, sizeof(done), 0) == S_FALSE && waited < 2000) {
            ::Sleep(1);
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
#endif

// ---- The swap chain -----------------------------------------------------------------------------------------------

IDXGISwapChain1* g_chain = nullptr;
ID3D11RenderTargetView* g_view = nullptr;
int g_chainWidth = 0, g_chainHeight = 0;
UINT g_chainFlags = 0;     // the flags it was made with (ResizeBuffers takes the same, Present's follow)
bool g_recreated = false;  // made again once after a failed resize
PresentMode g_mode;
int g_presentFailures = 0;

void ReleaseView() {
    if (g_view) g_view->Release();
    g_view = nullptr;
}

void ReleaseChain() {
    ReleaseView();
    if (g_chain) g_chain->Release();
    g_chain = nullptr;
    g_chainWidth = g_chainHeight = 0;
    g_chainFlags = 0;
}

bool CreateView() {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(g_chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
    HRESULT hr = g_device->CreateRenderTargetView(buffer, nullptr, &g_view);
    buffer->Release();
    return SUCCEEDED(hr);
}

bool CreateChain(HWND window, int width, int height) {
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
              SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory)));
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
        sd.Flags = ChainFlagsFor(g_mode.allowTearing, TearingSupported(factory));
        HRESULT hr = factory->CreateSwapChainForHwnd(g_device, window, &sd, nullptr, nullptr, &g_chain);
        if (FAILED(hr)) {
            ei::Log("render: CreateSwapChainForHwnd failed (%08lX)", static_cast<unsigned long>(hr));
            g_chain = nullptr;
            ok = false;
        } else {
            g_chainFlags = sd.Flags;
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
    g_chainWidth = width;
    g_chainHeight = height;
    ei::Log("render: presenting on the game window through a flip-model swap chain (%dx%d), %s", width, height,
            g_mode.vsync ? "waiting for the vertical blank"
            : (g_chainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? "no vertical blank wait, tearing allowed"
                                                                 : "no vertical blank wait (no tearing support)");
    return true;
}

bool NoChain(const char* what) {
    std::snprintf(g_error, sizeof(g_error), "%s", what);
    return false;
}

// Back buffer of `width` x `height`: made, or resized (a failed resize, e.g. after a display mode change, makes the
// swap chain again once).
bool EnsureChain(HWND window, int width, int height) {
    if (!g_chain) return CreateChain(window, width, height) || NoChain("no swap chain");
    if (width == g_chainWidth && height == g_chainHeight) return true;
    ReleaseView();
    HRESULT hr = g_chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN,
                                        g_chainFlags);
    if (SUCCEEDED(hr) && CreateView()) {
        g_chainWidth = width;
        g_chainHeight = height;
        return true;
    }
    if (IsLoss(hr)) {
        LostBy("ResizeBuffers", hr);
        return false;
    }
    ei::Log("render: cannot resize the game window's swap chain to %dx%d (%08lX)%s", width, height,
            static_cast<unsigned long>(hr), g_recreated ? "" : "; making it again");
    ReleaseChain();
    if (g_recreated) return NoChain("the swap chain cannot be resized");
    g_recreated = true;
    return CreateChain(window, width, height) || NoChain("the swap chain cannot be made again");
}

} // namespace

// ---- graphics.h ---------------------------------------------------------------------------------------------------

void PrepareInBackground() {
#if defined(EI_RENDER_VALIDATION)
    if (g_makeRemovable) return;   // made at Start (CreateRemovableDevice)
#endif
    if (g_prepared.thread || g_device) return;
    g_prepared.thread = CreateThread(nullptr, 0, &PrepareDeviceThread, nullptr, 0, nullptr);
}

bool Start(sg_environment* environment) {
    g_failure = S_OK;
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = S_OK;
#if defined(EI_RENDER_VALIDATION)
    if (g_makeRemovable) hr = CreateRemovableDevice(&got);
    else
#endif
        if (!TakePreparedDevice(&hr, &got)) hr = CreatePlainDevice(&g_device, &g_context, &got);
    if (FAILED(hr)) {
        std::snprintf(g_error, sizeof(g_error), "no Direct3D 11 device (D3D11CreateDevice failed, %08lX)",
                      static_cast<unsigned long>(hr));
        ei::Log("render: %s", g_error);
        return false;
    }
    environment->d3d11.device = g_device;
    environment->d3d11.device_context = g_context;
#if defined(EI_RENDER_VALIDATION)
    if (g_d3d12)
        ei::Log("render: Direct3D 11 device ready (feature level %X), through D3D11On12 on a Direct3D 12 device: "
                "removable",
                static_cast<unsigned>(got));
    else
#endif
        ei::Log("render: Direct3D 11 device ready (feature level %X)", static_cast<unsigned>(got));
    return true;
}

void Stop() {
    ReleaseChain();
    g_recreated = false;
    if (g_context) {
        g_context->ClearState();
        g_context->Flush();
        g_context->Release();
    }
    if (g_device)
        ei::Log("render: the Direct3D 11 device released (%lu references left)",
                static_cast<unsigned long>(g_device->Release()));
    g_context = nullptr;
    g_device = nullptr;
    g_failure = S_OK;
#if defined(EI_RENDER_VALIDATION)
    ReleaseRemovable();
#endif
}

const char* Error() { return g_error; }

int MaxSamples(int most) {
    for (int samples = most; samples > 1; samples /= 2) {
        UINT colour = 0, depth = 0;
        // RGBA8 and sokol's DEPTH (a D32_FLOAT view of R32_TYPELESS).
        const auto count = static_cast<UINT>(samples);
        if (SUCCEEDED(g_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, count, &colour)) &&
            SUCCEEDED(g_device->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, count, &depth)) && colour > 0 &&
            depth > 0)
            return samples;
    }
    return 1;
}

std::optional<std::uint32_t> Lost() {
    if (!g_device) return std::nullopt;
    const HRESULT reason = g_device->GetDeviceRemovedReason();
    if (FAILED(reason)) return static_cast<std::uint32_t>(reason);
    if (FAILED(g_failure)) return static_cast<std::uint32_t>(g_failure);
    return std::nullopt;
}

bool BeginDrawable(NativeWindow window, int width, int height, const PresentMode& mode, sg_swapchain* drawable) {
    g_mode = mode;
    if (!EnsureChain(static_cast<HWND>(window), width, height)) return false;
    *drawable = sg_swapchain{};
    drawable->width = width;
    drawable->height = height;
    drawable->sample_count = 1;
    drawable->color_format = SG_PIXELFORMAT_BGRA8;
    drawable->depth_format = SG_PIXELFORMAT_NONE;
    drawable->d3d11.render_view = g_view;
    return true;
}

bool EndDrawable() {
    if (!g_chain) return false;
#if defined(EI_RENDER_VALIDATION)
    // A removal asked for happens here, after the frame was drawn on a working device: this Present reports it.
    RemoveIfAsked();
#endif
    const HRESULT hr = g_chain->Present(SyncIntervalFor(g_mode.vsync), PresentFlagsFor(g_mode.vsync, g_chainFlags));
    if (IsLoss(hr)) {
        LostBy("Present", hr);
        return false;
    }
    // DXGI_STATUS_OCCLUDED (covered or minimised) is a success.
    if (FAILED(hr) && g_presentFailures++ < 10)
        ei::Log("render: the game window's Present failed (%08lX)", static_cast<unsigned long>(hr));
    return SUCCEEDED(hr);
}

Readback::Readback() = default;
Readback::~Readback() = default;

bool Readback::Read(sg_image image, const Rect& region) {
    if (!staging) staging = std::make_unique<Staging>();
    const bool read = staging->Map(Context(), Texture(image), region);
    sg_reset_state_cache();
    return read;
}

bool Readback::ReadDrawable(const Rect& region) {
    ID3D11Texture2D* back = nullptr;
    if (!g_chain || FAILED(g_chain->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    if (!staging) staging = std::make_unique<Staging>();
    const bool read = staging->Map(Context(), back, region);
    back->Release();
    sg_reset_state_cache();
    return read;
}

const std::uint8_t* Readback::Row(int y) const { return staging->Row(y); }

void Readback::Unmap() {
    if (staging) staging->Unmap();
}

void Readback::Release() {
    if (staging) staging->Release();
    staging.reset();
}

struct GpuTimer::Queries {
    bool made = false;   // false: the device has no timestamp queries
    ID3D11Query* clock = nullptr;   // the disjoint query: the stamps' frequency, and whether it held
    ID3D11Query* stamps[kMostStamps] = {};
    int count = 0;
    bool pending = false;   // ended, not read yet

    void Release() {
        if (clock) clock->Release();
        for (ID3D11Query* stamp : stamps)
            if (stamp) stamp->Release();
    }
};

GpuTimer::GpuTimer() = default;
GpuTimer::~GpuTimer() = default;

bool GpuTimer::Begin() {
    if (!queries) {
        queries = std::make_unique<Queries>();
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        bool made = SUCCEEDED(Device()->CreateQuery(&desc, &queries->clock));
        desc.Query = D3D11_QUERY_TIMESTAMP;
        for (ID3D11Query*& stamp : queries->stamps) made = made && SUCCEEDED(Device()->CreateQuery(&desc, &stamp));
        queries->made = made;
        if (!made) ei::Log("render: no GPU timestamps (CreateQuery failed)");
    }
    if (!queries->made || queries->pending) return false;
    queries->count = 0;
    Context()->Begin(queries->clock);
    return true;
}

void GpuTimer::Stamp() {
    if (queries && queries->made && queries->count < kMostStamps) Context()->End(queries->stamps[queries->count++]);
}

void GpuTimer::End() {
    if (!queries || !queries->made) return;
    Context()->End(queries->clock);
    queries->pending = true;
}

bool GpuTimer::Read(std::vector<double>* milliseconds) {
    if (!queries || !queries->pending) return false;
    ID3D11DeviceContext* context = Context();
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
    if (context->GetData(queries->clock, &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return false;
    UINT64 ticks[kMostStamps] = {};
    for (int k = 0; k < queries->count; ++k)
        if (context->GetData(queries->stamps[k], &ticks[k], sizeof(ticks[k]), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            return false;
    queries->pending = false;
    milliseconds->clear();
    if (clock.Disjoint || clock.Frequency == 0) return true;
    const double ms = 1000.0 / static_cast<double>(clock.Frequency);
    for (int k = 0; k < queries->count; ++k)
        milliseconds->push_back(static_cast<double>(static_cast<std::int64_t>(ticks[k] - ticks[0])) * ms);
    return true;
}

void GpuTimer::Release() {
    if (queries) queries->Release();
    queries.reset();
}

// ---- graphics_win32.h ---------------------------------------------------------------------------------------------

bool TearingSupported(IDXGIFactory2* factory) {
    IDXGIFactory5* factory5 = nullptr;
    BOOL allowed = FALSE;
    if (factory && SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
        if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed))))
            allowed = FALSE;
        factory5->Release();
    }
    return allowed != FALSE;
}

std::uint32_t ChainFlagsFor(bool allowTearing, bool tearingSupported) {
    return allowTearing && tearingSupported ? static_cast<std::uint32_t>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) : 0u;
}

std::uint32_t SyncIntervalFor(bool vsync) { return vsync ? 1u : 0u; }

std::uint32_t PresentFlagsFor(bool vsync, std::uint32_t chainFlags) {
    const bool tearing = !vsync && (chainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    return tearing ? static_cast<std::uint32_t>(DXGI_PRESENT_ALLOW_TEARING) : 0u;
}

#if defined(EI_RENDER_VALIDATION)
void MakeRemovable() { g_makeRemovable = true; }

bool Removable() { return g_d3d12 != nullptr; }

bool RequestRemoval() {
    if (!g_d3d12) return false;
    ID3D12Device5* device5 = nullptr;
    if (FAILED(g_d3d12->QueryInterface(IID_PPV_ARGS(&device5)))) {
        ei::Log("render: RemoveDevice: no ID3D12Device5 (Windows 10 1809 or later)");
        return false;
    }
    device5->Release();
    g_removeAsked = true;
    return true;
}

int Removals() { return g_removals; }
#endif

// sokol's (the offline checks set sokol up on a device of their own).
ID3D11Device* Device() { return static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device())); }

ID3D11DeviceContext* Context() {
    return static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
}

ID3D11Texture2D* Texture(sg_image image) {
    return static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(image).tex2d));
}

bool CurrentChain(Chain* chain) {
    if (!g_chain || !g_view) return false;
    *chain = Chain{g_chain, g_view, SyncIntervalFor(g_mode.vsync), PresentFlagsFor(g_mode.vsync, g_chainFlags)};
    return true;
}

bool Staging::Map(ID3D11DeviceContext* context, ID3D11Texture2D* source, const Rect& region) {
    Unmap();
    if (!context || !source || region.right <= region.left || region.bottom <= region.top) return false;
    const auto copyWidth = static_cast<unsigned>(region.right - region.left);
    const auto copyHeight = static_cast<unsigned>(region.bottom - region.top);
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (!texture || width < copyWidth || height < copyHeight || format != static_cast<unsigned>(desc.Format)) {
        Release();
        ID3D11Device* device = nullptr;
        source->GetDevice(&device);
        if (!device) return false;
        desc.Width = copyWidth;
        desc.Height = copyHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
        device->Release();
        if (FAILED(hr)) {
            texture = nullptr;
            return false;
        }
        width = copyWidth;
        height = copyHeight;
        format = static_cast<unsigned>(desc.Format);
    }
    D3D11_BOX box{static_cast<UINT>(region.left),  static_cast<UINT>(region.top),    0,
                  static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1};
    context->CopySubresourceRegion(texture, 0, 0, 0, 0, source, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(texture, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    mappedOn = context;
    data = static_cast<const std::uint8_t*>(mapped.pData);
    pitch = mapped.RowPitch;
    return true;
}

void Staging::Unmap() {
    if (mappedOn) mappedOn->Unmap(texture, 0);
    mappedOn = nullptr;
    data = nullptr;
    pitch = 0;
}

void Staging::Release() {
    Unmap();
    if (texture) texture->Release();
    texture = nullptr;
    width = height = format = 0;
}

} // namespace platform::graphics
