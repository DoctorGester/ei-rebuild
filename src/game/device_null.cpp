// The switch-over device (device.h, docs/notes/renderer.md section 9): the device object's IDirectDraw7 and
// IDirect3DDevice7 pointers hold two tokens, and the calls of device.h answer from this file's state (the
// capability and video memory queries with dgVoodoo's values where the game's logic depends on them).
#include "device.h"

#include "../hook/log.h"

#include <intrin.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

#pragma intrinsic(_ReturnAddress)

namespace ei {
void DescribeCode(std::uintptr_t address, char* out, std::size_t size);
void ReportImageFailure(const char* what);
} // namespace ei

namespace {

// ---- Fixed values --------------------------------------------------------------------------------

constexpr DWORD kVideoMemory = 256u << 20;   // dgVoodoo's emulated card (VRAM = 256 in its conf)
constexpr DWORD kRefreshRate = 60;

DDPIXELFORMAT RgbFormat(DWORD flags, DWORD bits, DWORD red, DWORD green, DWORD blue, DWORD alpha) {
    DDPIXELFORMAT f{};
    f.dwSize = sizeof(f);
    f.dwFlags = flags;
    f.dwRGBBitCount = bits;
    f.dwRBitMask = red;
    f.dwGBitMask = green;
    f.dwBBitMask = blue;
    f.dwRGBAlphaBitMask = alpha;
    return f;
}

// The capabilities the game reads: the texture stages (CRenderHelper's presets, the renderer's
// filters), the operations (the presets' check), the raster capabilities (the camp's antialiasing),
// the largest texture (the texture manager's 512 limit), the render bit depths.
D3DDEVICEDESC7 DeviceCaps() {
    D3DDEVICEDESC7 d{};
    d.dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX | D3DDEVCAPS_EXECUTEVIDEOMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
                  D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP |
                  D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWRASTERIZATION;
    D3DPRIMCAPS caps{};
    caps.dwSize = sizeof(caps);
    caps.dwMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
    caps.dwRasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE |
                        D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZBIAS | D3DPRASTERCAPS_FOGRANGE |
                        D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_ZFOG;
    caps.dwZCmpCaps = 0xFF;          // every D3DPCMPCAPS_
    caps.dwSrcBlendCaps = 0x1FFF;    // every D3DPBLENDCAPS_
    caps.dwDestBlendCaps = 0x1FFF;
    caps.dwAlphaCmpCaps = 0xFF;
    caps.dwShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB |
                       D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
    caps.dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_TRANSPARENCY |
                         D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE;
    caps.dwTextureFilterCaps = D3DPTFILTERCAPS_NEAREST | D3DPTFILTERCAPS_LINEAR | D3DPTFILTERCAPS_MIPNEAREST |
                               D3DPTFILTERCAPS_MIPLINEAR | D3DPTFILTERCAPS_LINEARMIPNEAREST | D3DPTFILTERCAPS_LINEARMIPLINEAR |
                               D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
                               D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                               D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    caps.dwTextureBlendCaps = D3DPTBLENDCAPS_DECAL | D3DPTBLENDCAPS_MODULATE | D3DPTBLENDCAPS_DECALALPHA |
                              D3DPTBLENDCAPS_MODULATEALPHA | D3DPTBLENDCAPS_ADD;
    caps.dwTextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
                                D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_INDEPENDENTUV;
    d.dpcLineCaps = caps;
    d.dpcTriCaps = caps;
    d.dwDeviceRenderBitDepth = DDBD_16 | DDBD_32;
    d.dwDeviceZBufferBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
    d.dwMinTextureWidth = 1;
    d.dwMinTextureHeight = 1;
    d.dwMaxTextureWidth = 4096;
    d.dwMaxTextureHeight = 4096;
    d.dwMaxTextureRepeat = 8192;
    d.dwMaxTextureAspectRatio = 4096;
    d.dwMaxAnisotropy = 16;
    d.dvGuardBandLeft = -16384.0f;
    d.dvGuardBandTop = -16384.0f;
    d.dvGuardBandRight = 16384.0f;
    d.dvGuardBandBottom = 16384.0f;
    d.dwStencilCaps = 0xFF;            // every D3DSTENCILCAPS_
    d.dwFVFCaps = 8;                   // texture coordinate sets
    d.dwTextureOpCaps = 0x00FFFFFF;    // every D3DTEXOPCAPS_
    d.wMaxTextureBlendStages = 8;
    d.wMaxSimultaneousTextures = 8;
    d.deviceGUID = device::kIidHalDevice;
    return d;
}

DDCAPS DirectDrawCaps() {
    DDCAPS c{};
    c.dwSize = sizeof(c);
    c.dwCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_BLTSTRETCH | DDCAPS_COLORKEY | DDCAPS_CANBLTSYSMEM;
    c.dwCaps2 = DDCAPS2_NONLOCALVIDMEM | DDCAPS2_WIDESURFACES;   // no DDCAPS2_PRIMARYGAMMA: no gamma ramp
    c.dwVidMemTotal = kVideoMemory;
    c.dwVidMemFree = kVideoMemory;
    c.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY |
                       DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    return c;
}

// ---- The switch-over device's state -------------------------------------------------------------
// What a Direct3D 7 device and its DirectDraw object would keep, without either (the render and texture stage
// states are the renderer's: render/render_state.h).

constexpr int kStages = 8;

class SwitchOverDevice {
public:
    // A new device: no textures, no scene.
    void Reset() {
        for (IDirectDrawSurface7*& texture : textures_) {
            if (texture) texture->Release();
            texture = nullptr;
        }
        inScene_ = false;
        viewport_ = D3DVIEWPORT7{};
    }

    HRESULT BeginScene() {
        if (inScene_) return D3DERR_SCENE_IN_SCENE;
        inScene_ = true;
        return D3D_OK;
    }
    HRESULT EndScene() {
        if (!inScene_) return D3DERR_SCENE_NOT_IN_SCENE;
        inScene_ = false;
        return D3D_OK;
    }
    void SetViewport(const D3DVIEWPORT7& viewport) { viewport_ = viewport; }

    HRESULT SetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 texture) {
        if (stage >= kStages) return DDERR_INVALIDPARAMS;
        if (texture) texture->AddRef();
        if (textures_[stage]) textures_[stage]->Release();
        textures_[stage] = texture;
        return D3D_OK;
    }

    void SetMode(int width, int height, int bitsPerPixel) {
        width_ = width;
        height_ = height;
        bits_ = bitsPerPixel;
    }
    DDSURFACEDESC2 Mode() const {
        DDSURFACEDESC2 mode{};
        mode.dwSize = sizeof(mode);
        mode.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
        mode.dwWidth = static_cast<DWORD>(width_);
        mode.dwHeight = static_cast<DWORD>(height_);
        mode.lPitch = width_ * (bits_ == 16 ? 2 : 4);
        mode.dwRefreshRate = kRefreshRate;
        mode.ddpfPixelFormat = bits_ == 16 ? RgbFormat(DDPF_RGB, 16, 0xF800, 0x07E0, 0x001F, 0)
                                            : RgbFormat(DDPF_RGB, 32, 0xFF0000, 0xFF00, 0xFF, 0);
        return mode;
    }

private:
    IDirectDrawSurface7* textures_[kStages] = {};
    bool inScene_ = false;
    D3DVIEWPORT7 viewport_{};
    int width_ = 0, height_ = 0, bits_ = 32;
};

SwitchOverDevice g_switchOver;

HRESULT EnumMode(LPVOID context, LPDDENUMMODESCALLBACK2 callback) {
    DDSURFACEDESC2 mode = g_switchOver.Mode();
    if (callback) callback(&mode, context);
    return DD_OK;
}

HRESULT FillDeviceCaps(LPD3DDEVICEDESC7 desc) {
    if (!desc) return DDERR_INVALIDPARAMS;
    *desc = DeviceCaps();
    return D3D_OK;
}

HRESULT FillDirectDrawCaps(LPDDCAPS driver, LPDDCAPS emulated) {
    if (driver) *driver = DirectDrawCaps();
    if (emulated) {
        *emulated = DDCAPS{};
        emulated->dwSize = sizeof(DDCAPS);
    }
    return DD_OK;
}

HRESULT VideoMemory(LPDWORD total, LPDWORD free) {
    if (total) *total = kVideoMemory;
    if (free) *free = kVideoMemory;
    return DD_OK;
}

// ---- The device object's interface pointers -------------------------------------------------------
// Tokens, not interfaces: each slot of a token's method table ends the run naming its caller, so code that calls
// the old device directly fails at once.

constexpr int kTrapSlots = 64;   // more than either interface has (IDirect3DDevice7 49, IDirectDraw7 30)
const char* const kInterfaceNames[] = {"IDirect3DDevice7", "IDirectDraw7"};

[[noreturn]] void OldInterfaceCalled(int interfaceId, int slot, const void* caller) {
    char from[160], what[320];
    ei::DescribeCode(reinterpret_cast<std::uintptr_t>(caller), from, sizeof(from));
    std::snprintf(what, sizeof(what),
                  "old device: %s method %d called from %s: the device object holds no interface (device.h's calls "
                  "answer for it)",
                  kInterfaceNames[interfaceId], slot, from);
    ei::ReportImageFailure(what);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) Sleep(INFINITE);
}

template <int InterfaceId, int Slot>
[[noreturn]] void Trap() {
    OldInterfaceCalled(InterfaceId, Slot, _ReturnAddress());
}

using TrapFn = void(*)();
template <int InterfaceId, int... Slots>
constexpr std::array<TrapFn, sizeof...(Slots)> TrapTable(std::integer_sequence<int, Slots...>) {
    return {&Trap<InterfaceId, Slots>...};
}

// What d3dDevice_ / ddraw_ point at: an object whose first word is its method table, as a COM object's.
struct InterfaceToken {
    const TrapFn* methods;
};
constexpr auto kDeviceTraps = TrapTable<0>(std::make_integer_sequence<int, kTrapSlots>{});
constexpr auto kDirectDrawTraps = TrapTable<1>(std::make_integer_sequence<int, kTrapSlots>{});
InterfaceToken g_device{kDeviceTraps.data()};
InterfaceToken g_directDraw{kDirectDrawTraps.data()};

} // namespace

namespace device {

IDirectDraw7* StartWithoutDirectDraw(int width, int height, int bitsPerPixel) {
    g_switchOver.SetMode(width, height, bitsPerPixel);
    ei::Log("device: built without DirectDraw or Direct3D (%dx%d, %d bits); the new renderer presents", width, height,
            bitsPerPixel);
    return reinterpret_cast<IDirectDraw7*>(&g_directDraw);
}

IDirect3DDevice7* Direct3DDeviceToken() {
    g_switchOver.Reset();
    return reinterpret_cast<IDirect3DDevice7*>(&g_device);
}

// ---- The device calls of the rewritten code ----------------------------------------------

void AddRef(IDirect3DDevice7*) {}
void Release(IDirect3DDevice7*) {}
void Release(IDirectDraw7*) {}

HRESULT GetCaps(IDirect3DDevice7*, D3DDEVICEDESC7* desc) { return FillDeviceCaps(desc); }
HRESULT BeginScene(IDirect3DDevice7*) { return g_switchOver.BeginScene(); }
HRESULT EndScene(IDirect3DDevice7*) { return g_switchOver.EndScene(); }
HRESULT Clear(IDirect3DDevice7*, DWORD, D3DRECT*, DWORD, D3DCOLOR, D3DVALUE, DWORD) { return D3D_OK; }
HRESULT SetViewport(IDirect3DDevice7*, D3DVIEWPORT7* viewport) {
    if (viewport) g_switchOver.SetViewport(*viewport);
    return D3D_OK;
}
HRESULT SetTexture(IDirect3DDevice7*, DWORD stage, IDirectDrawSurface7* texture) {
    return g_switchOver.SetTexture(stage, texture);
}

HRESULT GetCaps(IDirectDraw7*, DDCAPS* driver, DDCAPS* emulated) { return FillDirectDrawCaps(driver, emulated); }
HRESULT EnumDisplayModes(IDirectDraw7*, DWORD, DDSURFACEDESC2*, void* context, LPDDENUMMODESCALLBACK2 callback) {
    return EnumMode(context, callback);
}
HRESULT RestoreDisplayMode(IDirectDraw7*) { return DD_OK; }
HRESULT SetCooperativeLevel(IDirectDraw7*, HWND, DWORD) { return DD_OK; }
HRESULT GetAvailableVidMem(IDirectDraw7*, DDSCAPS2*, DWORD* total, DWORD* free) { return VideoMemory(total, free); }
HRESULT RestoreAllSurfaces(IDirectDraw7*) { return DD_OK; }
HRESULT FlipToGDISurface(IDirectDraw7*) { return DD_OK; }

} // namespace device
