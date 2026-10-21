// The switch-over device (ticket R9a; device.h, docs/notes/renderer.md section 9). With the new
// renderer presenting, the device object has no DirectDraw object and no Direct3D device: its
// IDirectDraw7 (+0x0C) and IDirect3DDevice7 (+0x54) pointers hold two tokens (below), not interfaces.
// The calls of device.h know them and answer from the switch-over device's state: what the game reads
// back stays consistent (the render and texture stage states set, fed to the new renderer's mirror as
// well, render_state.h; state blocks recorded and applied; the capability, display mode and video
// memory queries answered with fixed values, the old device's under dgVoodoo where the game's logic
// depends on them). A method called through a token directly ends the run naming its caller (R9b2).
#include "device.h"

#include "render/render_state.h"

#include "../hook/log.h"

#include <intrin.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#pragma intrinsic(_ReturnAddress)

namespace ei {
void DescribeCode(std::uintptr_t address, char* out, std::size_t size);   // hook/standalone.cpp
void ReportImageFailure(const char* what);                                // hook/standalone.cpp
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

DDPIXELFORMAT FourCCFormat(DWORD code) {
    DDPIXELFORMAT f{};
    f.dwSize = sizeof(f);
    f.dwFlags = DDPF_FOURCC;
    f.dwFourCC = code;
    return f;
}

const DDPIXELFORMAT kTextureFormats[] = {
    RgbFormat(DDPF_RGB, 16, 0xF800, 0x07E0, 0x001F, 0),                                     // 5:6:5
    RgbFormat(DDPF_RGB | DDPF_ALPHAPIXELS, 16, 0x7C00, 0x03E0, 0x001F, 0x8000),             // 1:5:5:5
    RgbFormat(DDPF_RGB | DDPF_ALPHAPIXELS, 16, 0x0F00, 0x00F0, 0x000F, 0xF000),             // 4:4:4:4
    RgbFormat(DDPF_RGB | DDPF_ALPHAPIXELS, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000u),        // 8:8:8:8
    FourCCFormat(MAKEFOURCC('D', 'X', 'T', '1')),
    FourCCFormat(MAKEFOURCC('D', 'X', 'T', '2')),
    FourCCFormat(MAKEFOURCC('D', 'X', 'T', '3')),
    FourCCFormat(MAKEFOURCC('D', 'X', 'T', '4')),
    // Rebirth's battle screen texture (battle00, 1536x1536) and footprints are DXT5: without it
    // ParseMmpHeader finds no format, LoadTexture fails (Rebirth's DisableExitOnCrashReport goes
    // on), the battle screen misses a texture and its sprites bind garbage (ticket R9c).
    FourCCFormat(MAKEFOURCC('D', 'X', 'T', '5')),
};

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
    d.deviceGUID = device::IidHalDevice();
    return d;
}

DDCAPS DirectDrawCaps() {
    DDCAPS c{};
    c.dwSize = sizeof(c);
    c.dwCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_BLTSTRETCH | DDCAPS_COLORKEY | DDCAPS_CANBLTSYSMEM;
    c.dwCaps2 = DDCAPS2_NONLOCALVIDMEM | DDCAPS2_WIDESURFACES;   // no DDCAPS2_PRIMARYGAMMA: no gamma ramp (R9c)
    c.dwVidMemTotal = kVideoMemory;
    c.dwVidMemFree = kVideoMemory;
    c.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY |
                       DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    return c;
}

// ---- The switch-over device's state -------------------------------------------------------------
//
// What a Direct3D 7 device and its DirectDraw object would keep, without either: the render and
// texture stage states, the state blocks, the bound textures, the scene bracket, the viewport and the
// display mode. The code reaches it through device::SetRenderState and the other calls below.

constexpr int kRenderStates = 256;
constexpr int kStages = 8;
constexpr int kStageStates = 32;

class SwitchOverDevice {
public:
    // A new device's states (the Direct3D 7 defaults), no blocks, no textures.
    void Reset() {
        std::memset(render_, 0, sizeof(render_));
        const std::pair<int, DWORD> defaults[] = {
            {D3DRENDERSTATE_TEXTUREPERSPECTIVE, TRUE}, {D3DRENDERSTATE_ZENABLE, D3DZB_TRUE},
            {D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID}, {D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD},
            {D3DRENDERSTATE_ZWRITEENABLE, TRUE}, {D3DRENDERSTATE_LASTPIXEL, TRUE},
            {D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE}, {D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO},
            {D3DRENDERSTATE_CULLMODE, D3DCULL_CCW}, {D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL},
            {D3DRENDERSTATE_ALPHAFUNC, D3DCMP_ALWAYS}, {D3DRENDERSTATE_FOGEND, 0x3F800000},   // 1.0f
            {D3DRENDERSTATE_FOGDENSITY, 0x3F800000}, {D3DRENDERSTATE_STENCILFAIL, D3DSTENCILOP_KEEP},
            {D3DRENDERSTATE_STENCILZFAIL, D3DSTENCILOP_KEEP}, {D3DRENDERSTATE_STENCILPASS, D3DSTENCILOP_KEEP},
            {D3DRENDERSTATE_STENCILFUNC, D3DCMP_ALWAYS}, {D3DRENDERSTATE_STENCILMASK, 0xFFFFFFFF},
            {D3DRENDERSTATE_STENCILWRITEMASK, 0xFFFFFFFF}, {D3DRENDERSTATE_TEXTUREFACTOR, 0xFFFFFFFF},
            {D3DRENDERSTATE_CLIPPING, TRUE}, {D3DRENDERSTATE_LIGHTING, TRUE},
            {D3DRENDERSTATE_COLORVERTEX, TRUE}, {D3DRENDERSTATE_LOCALVIEWER, TRUE},
            {D3DRENDERSTATE_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1},
            {D3DRENDERSTATE_SPECULARMATERIALSOURCE, D3DMCS_COLOR2},
        };
        for (const auto& [state, value] : defaults) render_[state] = value;
        std::memset(stage_, 0, sizeof(stage_));
        for (DWORD s = 0; s < kStages; ++s) {
            DWORD* st = stage_[s];
            st[D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
            st[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
            st[D3DTSS_COLORARG2] = D3DTA_CURRENT;
            st[D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
            st[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
            st[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
            st[D3DTSS_TEXCOORDINDEX] = s;
            st[D3DTSS_ADDRESS] = D3DTADDRESS_WRAP;
            st[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
            st[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
            st[D3DTSS_MAGFILTER] = D3DTFG_POINT;
            st[D3DTSS_MINFILTER] = D3DTFN_POINT;
            st[D3DTSS_MIPFILTER] = D3DTFP_NONE;
            st[D3DTSS_MAXANISOTROPY] = 1;
        }
        for (IDirectDrawSurface7*& texture : textures_) {
            if (texture) texture->Release();
            texture = nullptr;
        }
        blocks_.clear();
        recording_ = nullptr;
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
    const D3DVIEWPORT7& Viewport() const { return viewport_; }

    void SetRenderState(DWORD state, DWORD value) { SetState(Entry{false, 0, state, value}); }
    DWORD RenderState(DWORD state) const { return state < kRenderStates ? render_[state] : 0; }
    void SetStageState(DWORD stage, DWORD state, DWORD value) { SetState(Entry{true, stage, state, value}); }
    DWORD StageState(DWORD stage, DWORD state) const {
        return stage < kStages && state < kStageStates ? stage_[stage][state] : 0;
    }

    HRESULT BeginStateBlock() {
        blocks_.push_back(Block{});
        recording_ = &blocks_.back();
        return D3D_OK;
    }
    HRESULT EndStateBlock(LPDWORD handle) {
        if (!recording_) return D3DERR_NOTINBEGINSTATEBLOCK;
        recording_->handle = NextHandle();
        if (handle) *handle = recording_->handle;
        recording_ = nullptr;
        return D3D_OK;
    }
    HRESULT ApplyStateBlock(DWORD handle) {
        const Block* block = Find(handle);
        if (!block) return D3DERR_INVALIDSTATEBLOCK;
        const std::vector<Entry> entries = block->entries;   // (an apply while recording is recorded)
        for (const Entry& e : entries) SetState(e);
        return D3D_OK;
    }
    HRESULT CaptureStateBlock(DWORD handle) {
        Block* block = Find(handle);
        if (!block) return D3DERR_INVALIDSTATEBLOCK;
        for (Entry& e : block->entries) e.value = Current(e);
        return D3D_OK;
    }
    HRESULT DeleteStateBlock(DWORD handle) {
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
            if (it->handle != handle || &*it == recording_) continue;
            blocks_.erase(it);
            return D3D_OK;
        }
        return D3DERR_INVALIDSTATEBLOCK;
    }
    // Every state (D3DSBT_ALL and the two halves alike), as it is now.
    HRESULT CreateStateBlock(LPDWORD handle) {
        Block block;
        for (DWORD s = 0; s < kRenderStates; ++s) block.entries.push_back(Entry{false, 0, s, render_[s]});
        for (DWORD stage = 0; stage < kStages; ++stage)
            for (DWORD s = 1; s < kStageStates; ++s) block.entries.push_back(Entry{true, stage, s, stage_[stage][s]});
        block.handle = NextHandle();
        if (handle) *handle = block.handle;
        blocks_.push_back(std::move(block));
        if (recording_) recording_ = &blocks_[blocks_.size() - 2];
        return D3D_OK;
    }

    HRESULT GetTexture(DWORD stage, LPDIRECTDRAWSURFACE7* texture) {
        if (!texture) return DDERR_INVALIDPARAMS;
        *texture = stage < kStages ? textures_[stage] : nullptr;
        if (*texture) (*texture)->AddRef();
        return D3D_OK;
    }
    HRESULT SetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 texture) {
        if (stage >= kStages) return DDERR_INVALIDPARAMS;
        if (texture) texture->AddRef();
        if (textures_[stage]) textures_[stage]->Release();
        textures_[stage] = texture;
        return D3D_OK;
    }

    // The one display mode: the screen's.
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
        device::ScreenFormat(bits_, &mode.ddpfPixelFormat);
        return mode;
    }

private:
    // One state set: a render state, or a texture stage's state.
    struct Entry {
        bool stageState;
        DWORD stage, state, value;
    };
    struct Block {
        DWORD handle = 0;
        std::vector<Entry> entries;
    };

    // While a block is recorded the states go into it (not to the device, as Direct3D does).
    // Otherwise they are kept, and those of the two stages the new renderer draws with go to its
    // mirror too (render_state.h; the game code feeds it the same values where it is rewritten).
    void SetState(const Entry& e) {
        if (recording_) {
            for (Entry& r : recording_->entries) {
                if (r.stageState == e.stageState && r.stage == e.stage && r.state == e.state) {
                    r.value = e.value;
                    return;
                }
            }
            recording_->entries.push_back(e);
            return;
        }
        if (!e.stageState) {
            if (e.state >= kRenderStates) return;
            render_[e.state] = e.value;
            render::state::Render(e.state, e.value);
            return;
        }
        if (e.stage >= kStages || e.state >= kStageStates) return;
        stage_[e.stage][e.state] = e.value;
        render::state::Stage(e.stage, e.state, e.value);
    }
    DWORD Current(const Entry& e) const { return e.stageState ? StageState(e.stage, e.state) : RenderState(e.state); }
    Block* Find(DWORD handle) {
        for (Block& b : blocks_)
            if (b.handle == handle && handle != 0) return &b;
        return nullptr;
    }
    // Block handles go on counting over the whole run (never 0, never reused).
    DWORD NextHandle() { return ++lastHandle_; }

    DWORD render_[kRenderStates];
    DWORD stage_[kStages][kStageStates];
    IDirectDrawSurface7* textures_[kStages] = {};
    std::vector<Block> blocks_;
    Block* recording_ = nullptr;
    DWORD lastHandle_ = 0;
    bool inScene_ = false;
    D3DVIEWPORT7 viewport_{};
    int width_ = 0, height_ = 0, bits_ = 32;
};

SwitchOverDevice g_switchOver;

// The display modes' enumeration: the one mode.
HRESULT EnumMode(LPVOID context, LPDDENUMMODESCALLBACK2 callback) {
    DDSURFACEDESC2 mode = g_switchOver.Mode();
    if (callback) callback(&mode, context);
    return DD_OK;
}

// The texture formats' enumeration: the fixed table.
HRESULT EnumFormats(LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context) {
    int count = 0;
    const DDPIXELFORMAT* formats = device::SwitchOverTextureFormats(&count);
    for (int i = 0; i < count && callback; ++i) {
        DDPIXELFORMAT copy = formats[i];
        if (callback(&copy, context) != D3DENUMRET_OK) break;
    }
    return D3D_OK;
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
//
// Tokens, not interfaces: the calls of device.h compare the pointer they are given with them. Each
// slot of a token's method table ends the run naming its caller, so code that calls a method of the
// old device directly instead of through device.h fails at once.

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
[[noreturn]] void __stdcall Trap() {
    OldInterfaceCalled(InterfaceId, Slot, _ReturnAddress());
}

using TrapFn = void(__stdcall*)();
template <int InterfaceId, int... Slots>
constexpr std::array<TrapFn, sizeof...(Slots)> TrapTable(std::integer_sequence<int, Slots...>) {
    return {&Trap<InterfaceId, Slots>...};
}

// What +0x54 / +0x0C point at: an object whose first word is its method table, as a COM object's.
struct InterfaceToken {
    const TrapFn* methods;
};
constexpr auto kDeviceTraps = TrapTable<0>(std::make_integer_sequence<int, kTrapSlots>{});
constexpr auto kDirectDrawTraps = TrapTable<1>(std::make_integer_sequence<int, kTrapSlots>{});
InterfaceToken g_device{kDeviceTraps.data()};
InterfaceToken g_directDraw{kDirectDrawTraps.data()};
bool g_withoutDirectDraw = false;

} // namespace

namespace device {

void* StartWithoutDirectDraw(int width, int height, int bitsPerPixel) {
    g_withoutDirectDraw = true;
    g_switchOver.SetMode(width, height, bitsPerPixel);
    ei::Log("device: built without DirectDraw or Direct3D (%dx%d, %d bits); the new renderer presents", width, height,
            bitsPerPixel);
    return &g_directDraw;
}

void* Direct3DDeviceToken() {
    g_switchOver.Reset();
    return &g_device;
}

void ScreenFormat(int bitsPerPixel, DDPIXELFORMAT* format) {
    *format = bitsPerPixel == 16 ? RgbFormat(DDPF_RGB, 16, 0xF800, 0x07E0, 0x001F, 0)
                                 : RgbFormat(DDPF_RGB, 32, 0xFF0000, 0xFF00, 0xFF, 0);
}

const DDPIXELFORMAT* SwitchOverTextureFormats(int* count) {
    *count = static_cast<int>(sizeof(kTextureFormats) / sizeof(kTextureFormats[0]));
    return kTextureFormats;
}

std::uint32_t SwitchOverRenderState(std::uint32_t state) { return g_switchOver.RenderState(state); }
std::uint32_t SwitchOverStageState(std::uint32_t stage, std::uint32_t state) {
    return g_switchOver.StageState(stage, state);
}

// ---- The rewritten code's device calls (ticket R9b) ----------------------------------------------
//
// The switch-over's state answers when the interface given is the device object's token (or none);
// any other interface is called (game.exe's own, in the harness's code that runs in the original).

namespace {
bool NoDevice(const IDirect3DDevice7* d3d) {
    return g_withoutDirectDraw && (!d3d || static_cast<const void*>(d3d) == &g_device);
}
bool NoDirectDraw(const IDirectDraw7* ddraw) {
    return g_withoutDirectDraw && (!ddraw || static_cast<const void*>(ddraw) == &g_directDraw);
}
} // namespace

void AddRef(IDirect3DDevice7* d3d) {
    if (!NoDevice(d3d)) d3d->AddRef();
}
void Release(IDirect3DDevice7* d3d) {
    if (!NoDevice(d3d)) d3d->Release();
}
void Release(IDirectDraw7* ddraw) {
    if (!NoDirectDraw(ddraw)) ddraw->Release();
}

HRESULT GetCaps(IDirect3DDevice7* d3d, D3DDEVICEDESC7* desc) {
    return NoDevice(d3d) ? FillDeviceCaps(desc) : d3d->GetCaps(desc);
}
HRESULT EnumTextureFormats(IDirect3DDevice7* d3d, LPD3DENUMPIXELFORMATSCALLBACK callback, void* context) {
    return NoDevice(d3d) ? EnumFormats(callback, context) : d3d->EnumTextureFormats(callback, context);
}
HRESULT BeginScene(IDirect3DDevice7* d3d) { return NoDevice(d3d) ? g_switchOver.BeginScene() : d3d->BeginScene(); }
HRESULT EndScene(IDirect3DDevice7* d3d) { return NoDevice(d3d) ? g_switchOver.EndScene() : d3d->EndScene(); }
HRESULT Clear(IDirect3DDevice7* d3d, DWORD count, D3DRECT* rects, DWORD flags, D3DCOLOR colour, D3DVALUE z,
              DWORD stencil) {
    return NoDevice(d3d) ? D3D_OK : d3d->Clear(count, rects, flags, colour, z, stencil);
}
HRESULT SetViewport(IDirect3DDevice7* d3d, D3DVIEWPORT7* viewport) {
    if (!NoDevice(d3d)) return d3d->SetViewport(viewport);
    if (viewport) g_switchOver.SetViewport(*viewport);
    return D3D_OK;
}
HRESULT SetRenderTarget(IDirect3DDevice7* d3d, IDirectDrawSurface7* target, DWORD flags) {
    return NoDevice(d3d) ? D3D_OK : d3d->SetRenderTarget(target, flags);
}
HRESULT SetRenderState(IDirect3DDevice7* d3d, D3DRENDERSTATETYPE state, DWORD value) {
    if (!NoDevice(d3d)) return d3d->SetRenderState(state, value);
    g_switchOver.SetRenderState(static_cast<DWORD>(state), value);
    return D3D_OK;
}
HRESULT GetRenderState(IDirect3DDevice7* d3d, D3DRENDERSTATETYPE state, DWORD* value) {
    if (!NoDevice(d3d)) return d3d->GetRenderState(state, value);
    if (!value) return DDERR_INVALIDPARAMS;
    *value = g_switchOver.RenderState(static_cast<DWORD>(state));
    return D3D_OK;
}
HRESULT SetTextureStageState(IDirect3DDevice7* d3d, DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) {
    if (!NoDevice(d3d)) return d3d->SetTextureStageState(stage, state, value);
    g_switchOver.SetStageState(stage, static_cast<DWORD>(state), value);
    return D3D_OK;
}
HRESULT GetTextureStageState(IDirect3DDevice7* d3d, DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD* value) {
    if (!NoDevice(d3d)) return d3d->GetTextureStageState(stage, state, value);
    if (!value) return DDERR_INVALIDPARAMS;
    *value = g_switchOver.StageState(stage, static_cast<DWORD>(state));
    return D3D_OK;
}
HRESULT BeginStateBlock(IDirect3DDevice7* d3d) {
    return NoDevice(d3d) ? g_switchOver.BeginStateBlock() : d3d->BeginStateBlock();
}
HRESULT EndStateBlock(IDirect3DDevice7* d3d, DWORD* handle) {
    return NoDevice(d3d) ? g_switchOver.EndStateBlock(handle) : d3d->EndStateBlock(handle);
}
HRESULT ApplyStateBlock(IDirect3DDevice7* d3d, DWORD handle) {
    return NoDevice(d3d) ? g_switchOver.ApplyStateBlock(handle) : d3d->ApplyStateBlock(handle);
}
HRESULT DeleteStateBlock(IDirect3DDevice7* d3d, DWORD handle) {
    return NoDevice(d3d) ? g_switchOver.DeleteStateBlock(handle) : d3d->DeleteStateBlock(handle);
}
HRESULT SetTexture(IDirect3DDevice7* d3d, DWORD stage, IDirectDrawSurface7* texture) {
    return NoDevice(d3d) ? g_switchOver.SetTexture(stage, texture) : d3d->SetTexture(stage, texture);
}
HRESULT DrawPrimitive(IDirect3DDevice7* d3d, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD count,
                      DWORD flags) {
    return NoDevice(d3d) ? D3D_OK : d3d->DrawPrimitive(type, fvf, vertices, count, flags);
}
HRESULT DrawIndexedPrimitive(IDirect3DDevice7* d3d, D3DPRIMITIVETYPE type, DWORD fvf, void* vertices, DWORD count,
                             WORD* indices, DWORD indexCount, DWORD flags) {
    return NoDevice(d3d) ? D3D_OK : d3d->DrawIndexedPrimitive(type, fvf, vertices, count, indices, indexCount, flags);
}

HRESULT GetCaps(IDirectDraw7* ddraw, DDCAPS* driver, DDCAPS* emulated) {
    return NoDirectDraw(ddraw) ? FillDirectDrawCaps(driver, emulated) : ddraw->GetCaps(driver, emulated);
}
HRESULT GetDisplayMode(IDirectDraw7* ddraw, DDSURFACEDESC2* desc) {
    if (!NoDirectDraw(ddraw)) return ddraw->GetDisplayMode(desc);
    if (!desc) return DDERR_INVALIDPARAMS;
    *desc = g_switchOver.Mode();
    return DD_OK;
}
HRESULT EnumDisplayModes(IDirectDraw7* ddraw, DWORD flags, DDSURFACEDESC2* filter, void* context,
                         LPDDENUMMODESCALLBACK2 callback) {
    return NoDirectDraw(ddraw) ? EnumMode(context, callback) : ddraw->EnumDisplayModes(flags, filter, context, callback);
}
HRESULT RestoreDisplayMode(IDirectDraw7* ddraw) { return NoDirectDraw(ddraw) ? DD_OK : ddraw->RestoreDisplayMode(); }
HRESULT SetCooperativeLevel(IDirectDraw7* ddraw, HWND window, DWORD flags) {
    return NoDirectDraw(ddraw) ? DD_OK : ddraw->SetCooperativeLevel(window, flags);
}
HRESULT GetAvailableVidMem(IDirectDraw7* ddraw, DDSCAPS2* caps, DWORD* total, DWORD* free) {
    return NoDirectDraw(ddraw) ? VideoMemory(total, free) : ddraw->GetAvailableVidMem(caps, total, free);
}
HRESULT RestoreAllSurfaces(IDirectDraw7* ddraw) { return NoDirectDraw(ddraw) ? DD_OK : ddraw->RestoreAllSurfaces(); }
HRESULT FlipToGDISurface(IDirectDraw7* ddraw) { return NoDirectDraw(ddraw) ? DD_OK : ddraw->FlipToGDISurface(); }
HRESULT WaitForVerticalBlank(IDirectDraw7* ddraw, DWORD flags, HANDLE event) {
    return NoDirectDraw(ddraw) ? DD_OK : ddraw->WaitForVerticalBlank(flags, event);
}

} // namespace device
