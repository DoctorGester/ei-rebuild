// Direct3D device, drawables, transfers and diagnostic queries behind the shared sokol renderer.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
// The sokol_gfx implementation, selecting the host's backend (third_party/sokol, unmodified).
#define SOKOL_IMPL
#if defined(EI_RENDER_VALIDATION) || defined(EI_NATIVE_VALIDATION)
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL   // a failed check skips the call and is logged; the game goes on
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "sokol_gfx.h"
#undef SOKOL_IMPL
#undef SOKOL_GFX_IMPL

#include "hook/log.h"
#include "platform/graphics.h"

#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <float.h>
#include <windows.h>
#include <xmmintrin.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace platform::graphics {
ID3D11Device* Device();
ID3D11DeviceContext* Context();
inline ID3D11Texture2D* ImageTexture(sg_image image) {
    return static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(image).tex2d));
}

void SetError(const char* where, HRESULT result);
void ClearError();
bool RecordResult(const char* where, HRESULT result);
bool ReadTexture(ID3D11Texture2D* texture, unsigned x, unsigned y, unsigned width, unsigned height,
                 std::vector<std::uint8_t>* bytes, std::size_t* pitch);
void DropTransfers();
bool TearingSupported(IDXGIFactory2* factory);
namespace {
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
char g_error[256]{};
bool g_removable = false;
long g_failure = 0;
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
        if (left)
            ei::Log("render: the Direct3D 12 device under the removable one keeps %lu references",
                    static_cast<unsigned long>(left));
    }
    g_d3d12 = nullptr;
}

HRESULT CreateRemovableDevice(const D3D_FEATURE_LEVEL* levels, UINT count, D3D_FEATURE_LEVEL* got) {
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
    // Direct3D 12 devices are one per adapter while one lives: a removed one still referenced
    // somewhere would come back here.
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
        hr = create11on12(g_d3d12, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, count, queues, 1, 0, &g_device, &g_context,
                          got);
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
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels,
                             kLevelCount, D3D11_SDK_VERSION, device, got, context);
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

}
ID3D11Device* Device() {
    return sg_isvalid() ? static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device())) : g_device;
}
ID3D11DeviceContext* Context() {
    return sg_isvalid() ? static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context())) : g_context;
}
const char* DeviceError() { return g_error; }
void ClearError() { g_error[0] = 0; }
void SetError(const char* where, HRESULT hr) {
    std::snprintf(g_error, sizeof(g_error), "%s failed (%08lX)", where, static_cast<unsigned long>(hr));
    ei::Log("render: %s", g_error);
}

bool RecordResult(const char* where, HRESULT hr) {
    if (SUCCEEDED(hr)) return true;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
        hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
        g_failure = hr;
    SetError(where, hr);
    return false;
}

bool CreateEnvironment(sg_environment* environment, bool removable, void (*)()) {
    g_removable = removable;
    ClearError();
    g_failure = 0;
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = S_OK;
#if defined(EI_RENDER_VALIDATION)
    if (g_removable) hr = CreateRemovableDevice(kLevels, kLevelCount, &got);
    else
#endif
        if (!TakePreparedDevice(&hr, &got))
        hr = CreatePlainDevice(&g_device, &g_context, &got);
    if (FAILED(hr)) {
        SetError("D3D11CreateDevice", hr);
        return false;
    }
    environment->d3d11.device = g_device;
    environment->d3d11.device_context = g_context;
    ei::Log("render: Direct3D 11 device ready (feature level %X)", static_cast<unsigned>(got));
    return true;
}

void PrepareDevice(bool removable) {
    if (g_prepared.thread || g_device || removable) return;
    g_prepared.thread = CreateThread(nullptr, 0, &PrepareDeviceThread, nullptr, 0, nullptr);
}

void DropDevice() {
    DropWindow();
    DropTransfers();
    if (g_context) {
        g_context->ClearState();
        g_context->Flush();
        g_context->Release();
    }
    if (g_device) g_device->Release();
    g_context = nullptr;
    g_device = nullptr;
#if defined(EI_RENDER_VALIDATION)
    ReleaseRemovable();
#endif
}

int SampleCount() {
    for (int count = 8; count > 1; count /= 2) {
        UINT colour = 0, depth = 0;
        if (SUCCEEDED(g_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, count, &colour)) &&
            SUCCEEDED(g_device->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, count, &depth)) && colour && depth)
            return count;
    }
    return 1;
}

bool PollDeviceLoss(long* result, long* reason) {
    RemoveIfAsked();
    if (!g_device) return false;
    const HRESULT hr = g_device->GetDeviceRemovedReason();
    if (SUCCEEDED(hr) && !g_failure) return false;
    *result = g_failure ? g_failure : hr;
    *reason = hr;
    return true;
}

bool Removable() {
#if defined(EI_RENDER_VALIDATION)
    return g_d3d12 != nullptr;
#else
    return false;
#endif
}

int Removals() {
#if defined(EI_RENDER_VALIDATION)
    return g_removals;
#else
    return 0;
#endif
}

bool RequestRemoval() {
#if defined(EI_RENDER_VALIDATION)
    if (!g_d3d12) return false;
    ID3D12Device5* device5 = nullptr;
    if (FAILED(g_d3d12->QueryInterface(IID_PPV_ARGS(&device5)))) return false;
    device5->Release();
    g_removeAsked = true;
    return true;
#else
    return false;
#endif
}
#if defined(EI_RENDER_VALIDATION)
void RemoveIfAsked() {
    if (!g_removeAsked || !g_d3d12) return;
    g_removeAsked = false;
    ID3D12Device5* device5 = nullptr;
    if (FAILED(g_d3d12->QueryInterface(IID_PPV_ARGS(&device5)))) return;
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

#else
void RemoveIfAsked() {}
#endif
namespace {
IDXGISwapChain1* g_chain = nullptr;
ID3D11RenderTargetView* g_view = nullptr;
int g_chainWidth = 0, g_chainHeight = 0;
UINT g_chainFlags = 0;
bool g_waitForVsync = true, g_allowTearing = false, g_recreated = false;
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
    if (FAILED(g_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) return false;
    HRESULT hr = Device()->CreateRenderTargetView(buffer, nullptr, &g_view);
    buffer->Release();
    return SUCCEEDED(hr);
}

bool CreateChain(HWND window, int width, int height) {
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = SUCCEEDED(Device()->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
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
        sd.Flags = ChainFlagsFor(g_allowTearing, TearingSupported(factory));
        HRESULT hr = factory->CreateSwapChainForHwnd(Device(), window, &sd, nullptr, nullptr, &g_chain);
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
            !g_waitForVsync
                ? ((g_chainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? "no vertical blank wait, tearing allowed"
                                                                       : "no vertical blank wait (no tearing support)")
                : "waiting for the vertical blank");
    return true;
}

// The device is gone (removed, reset, hung): every sokol resource with it. Nothing more is drawn
// into the swap chain; at the end of the frame the device, the swap chain and every image are made
// again (render.cpp, RecreateDevice).
void DeviceLost(const char* where, HRESULT hr) {
    HRESULT reason = Device()->GetDeviceRemovedReason();
    ei::Log("render: the Direct3D 11 device was lost (%s: %08lX, reason %08lX)", where, static_cast<unsigned long>(hr),
            static_cast<unsigned long>(reason));
    RecordResult(where, hr);
}

bool IsDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

// No swap chain could be made: on a lost device the new one gets it (RecreateDevice at the frame's end, false);
// on a working device the game cannot go on.
bool NoChain(const char* what) {
    HRESULT reason = Device()->GetDeviceRemovedReason();
    if (FAILED(reason)) {
        DeviceLost(what, reason);
        return false;
    }
    SetError(what, E_FAIL);
    return false;
}

// Back buffer of `width` x `height`: made, or resized (a failed resize, e.g. after a display mode
// change, makes the swap chain again once). False: the device was lost (made again at the frame's end).
bool EnsureChain(HWND window, int width, int height) {
    if (!g_chain) return CreateChain(window, width, height) || NoChain("no swap chain");
    if (width == g_chainWidth && height == g_chainHeight) return true;
    ReleaseView();
    sg_reset_state_cache();
    HRESULT hr = g_chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN,
                                        g_chainFlags);
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
    if (g_recreated) return NoChain("the swap chain cannot be resized");
    g_recreated = true;
    return CreateChain(window, width, height) || NoChain("the swap chain cannot be made again");
}

}

bool WindowSize(void* window, int* width, int* height) {
    RECT client{};
    if (!window || IsIconic(static_cast<HWND>(window)) || !GetClientRect(static_cast<HWND>(window), &client))
        return false;
    *width = client.right - client.left;
    *height = client.bottom - client.top;
    return *width > 0 && *height > 0;
}

bool AcquireDrawable(void* window, bool vsync, bool allowTearing, sg_swapchain* drawable) {
    ClearError();
    int width = 0, height = 0;
    if (!WindowSize(window, &width, &height)) return false;
    g_waitForVsync = vsync;
    g_allowTearing = allowTearing;
    if (!EnsureChain(static_cast<HWND>(window), width, height)) return false;
    *drawable = {};
    drawable->width = width;
    drawable->height = height;
    drawable->sample_count = 1;
    drawable->color_format = SG_PIXELFORMAT_BGRA8;
    drawable->depth_format = SG_PIXELFORMAT_NONE;
    drawable->d3d11.render_view = g_view;
    return true;
}

bool PresentDrawable(bool vsync) {
    if (!g_chain) return false;
    return RecordResult("Present", g_chain->Present(SyncIntervalFor(vsync), PresentFlagsFor(vsync, g_chainFlags)));
}

void DropWindow() {
    ReleaseChain();
    g_recreated = false;
}

bool CanPresentOnWorker() { return true; }
bool ReadDrawable(unsigned x, unsigned y, unsigned width, unsigned height, std::vector<std::uint8_t>* bytes,
                  std::size_t* pitch) {
    if (!g_chain) return false;
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(g_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) return false;
    const bool read = ReadTexture(texture, x, y, width, height, bytes, pitch);
    texture->Release();
    return read;
}
namespace {
ID3D11Texture2D* g_staging = nullptr;
unsigned g_width = 0, g_height = 0;
DXGI_FORMAT g_format = DXGI_FORMAT_UNKNOWN;
}

void DropTransfers() {
    if (g_staging) g_staging->Release();
    g_staging = nullptr;
    g_width = g_height = 0;
    g_format = DXGI_FORMAT_UNKNOWN;
}

bool ReadTexture(ID3D11Texture2D* texture, unsigned x, unsigned y, unsigned width, unsigned height,
                 std::vector<std::uint8_t>* bytes, std::size_t* pitch) {
    if (!texture || !width || !height) return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (x + width > desc.Width || y + height > desc.Height) return false;
    if (!g_staging || g_width < width || g_height < height || g_format != desc.Format) {
        DropTransfers();
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(Device()->CreateTexture2D(&desc, nullptr, &g_staging))) return false;
        g_width = width;
        g_height = height;
        g_format = desc.Format;
    }
    D3D11_BOX region{x, y, 0, x + width, y + height, 1};
    Context()->CopySubresourceRegion(g_staging, 0, 0, 0, 0, texture, 0, &region);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(Context()->Map(g_staging, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    *pitch = mapped.RowPitch;
    bytes->resize(*pitch * height);
    std::memcpy(bytes->data(), mapped.pData, bytes->size());
    Context()->Unmap(g_staging, 0);
    sg_reset_state_cache();
    return true;
}

bool ReadImage(sg_image image, unsigned x, unsigned y, unsigned width, unsigned height,
               std::vector<std::uint8_t>* bytes, std::size_t* pitch) {
    return ReadTexture(ImageTexture(image), x, y, width, height, bytes, pitch);
}

void CopyImage(sg_image from, sg_image to, Region region, int x, int y) {
    D3D11_BOX box{static_cast<UINT>(region.left),  static_cast<UINT>(region.top),    0,
                  static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1};
    Context()->CopySubresourceRegion(ImageTexture(to), 0, x, y, 0, ImageTexture(from), 0, &box);
    sg_reset_state_cache();
}

void WriteImage(sg_image image, Region region, const void* bytes, int pitch) {
    D3D11_BOX box{static_cast<UINT>(region.left),  static_cast<UINT>(region.top),    0,
                  static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1};
    Context()->UpdateSubresource(ImageTexture(image), 0, &box, bytes, pitch, 0);
    sg_reset_state_cache();
}

void GenerateMipmaps(sg_image image) {
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(Device()->CreateShaderResourceView(ImageTexture(image), nullptr, &view))) {
        Context()->GenerateMips(view);
        view->Release();
        sg_reset_state_cache();
    }
}

sg_image MakeMipmappedImage(const sg_image_desc& source) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = source.width;
    desc.Height = source.height;
    desc.MipLevels = source.num_mipmaps;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = source.num_mipmaps > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(Device()->CreateTexture2D(&desc, nullptr, &texture))) return {};
    sg_image_desc injected = source;
    injected.d3d11_texture = texture;
    const sg_image image = sg_make_image(injected);
    texture->Release();
    return image;
}
namespace {
constexpr int kStamps = 64;
struct TimestampQueries {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* stamps[kStamps]{};
    int count = 0;
    bool pending = false;
    ~TimestampQueries() {
        if (disjoint) disjoint->Release();
        for (auto* stamp : stamps)
            if (stamp) stamp->Release();
    }
};
}

bool Timestamps::Begin() {
    auto* set = static_cast<TimestampQueries*>(state);
    if (!set) {
        set = new TimestampQueries;
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        bool made = Device() && SUCCEEDED(Device()->CreateQuery(&desc, &set->disjoint));
        desc.Query = D3D11_QUERY_TIMESTAMP;
        for (int i = 0; made && i < kStamps; ++i) made = SUCCEEDED(Device()->CreateQuery(&desc, &set->stamps[i]));
        if (!made) {
            delete set;
            return false;
        }
        state = set;
    }
    if (set->pending) return false;
    set->count = 0;
    Context()->Begin(set->disjoint);
    return true;
}

void Timestamps::Stamp() {
    auto* set = static_cast<TimestampQueries*>(state);
    if (set && set->count < kStamps) Context()->End(set->stamps[set->count++]);
}

void Timestamps::End() {
    auto* set = static_cast<TimestampQueries*>(state);
    if (!set) return;
    Context()->End(set->disjoint);
    set->pending = true;
}

bool Timestamps::Read(std::vector<double>* milliseconds) {
    auto* set = static_cast<TimestampQueries*>(state);
    if (!set || !set->pending) return false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    if (Context()->GetData(set->disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        return false;
    UINT64 values[kStamps]{};
    for (int i = 0; i < set->count; ++i)
        if (Context()->GetData(set->stamps[i], &values[i], sizeof(values[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            return false;
    set->pending = false;
    milliseconds->clear();
    if (!disjoint.Disjoint && disjoint.Frequency && set->count) {
        for (int i = 0; i < set->count; ++i)
            milliseconds->push_back(static_cast<double>(values[i] - values[0]) * 1000.0 /
                                    static_cast<double>(disjoint.Frequency));
    }
    return true;
}

void Timestamps::Release() {
    delete static_cast<TimestampQueries*>(state);
    state = nullptr;
}

// The vertical blank switch (docs/notes/renderer.md section 15): on, sync interval 1 and no flags; off, interval
// 0 and, where the system supports it (IDXGIFactory5, DXGI_FEATURE_PRESENT_ALLOW_TEARING), a swap chain made with
// DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (`allowTearing`: SetVsync(false), not the harness's SetWaitForVsync) whose presents carry DXGI_PRESENT_ALLOW_TEARING (the only way a windowed flip-model
// chain runs unlocked). Present's tearing flag needs the chain's, so it follows the flags the chain was made with.
bool TearingSupported(IDXGIFactory2* factory) {
    IDXGIFactory5* factory5 = nullptr;
    BOOL allowed = FALSE;
    if (factory && SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
        if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed))))
            allowed = FALSE;
        factory5->Release();
    }
    return allowed != FALSE;
}
UINT ChainFlagsFor(bool allowTearing, bool tearingSupported) {
    return allowTearing && tearingSupported ? static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) : 0u;
}
UINT SyncIntervalFor(bool vsync) { return vsync ? 1u : 0u; }
UINT PresentFlagsFor(bool vsync, UINT chainFlags) {
    return !vsync && (chainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING)
                                                                       : 0u;
}

std::uint32_t SaveFloatingPointState() {
#if defined(_M_IX86)
    unsigned state = 0;
    __control87_2(0, 0, &state, nullptr);
    return state;
#else
    return _mm_getcsr();
#endif
}

void RestoreFloatingPointState(std::uint32_t state) {
#if defined(_M_IX86)
    unsigned now = 0;
    __control87_2(0, 0, &now, nullptr);
    if (now != state) __control87_2(state, _MCW_EM | _MCW_IC | _MCW_RC | _MCW_PC, &now, nullptr);
#else
    constexpr unsigned mask = 0xFFC0;
    const unsigned now = _mm_getcsr();
    if ((now ^ state) & mask) _mm_setcsr((now & ~mask) | (state & mask));
#endif
}

void ReleaseReadbacks() { DropTransfers(); }
}
