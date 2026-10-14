// The switch-over device's stand-ins (ticket R9a; device.h, docs/notes/renderer.md section 9). With
// the new renderer presenting, the device object has no DirectDraw object and no Direct3D device;
// its IDirectDraw7 (+0x0C) and IDirect3DDevice7 (+0x54) pointers hold the two objects below. They
// draw nothing. What the game reads back stays consistent: the device keeps the render and texture
// stage states set on it (fed to the new renderer's mirror as well, render_state.h), records state
// blocks and applies them, and answers the capability, display mode and video memory queries with
// fixed values (the old device's under dgVoodoo where the game's logic depends on them). Every
// method logs its first call from each calling place ("old device: ..."): those callers are what
// ticket R9b guards before the stand-ins go.
#include "device.h"

#include "render/render_state.h"

#include "../hook/log.h"

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#pragma intrinsic(_ReturnAddress)

namespace {

// ---- Calls logged once per method and calling place ----------------------------------------------

constexpr int kSeenSlots = 4096;
constexpr int kMaxLogged = 400;
std::atomic<std::uint64_t> g_seen[kSeenSlots];
std::atomic<int> g_logged{0};

// True the first time `method` is called from `caller`.
bool FirstCall(std::uint32_t method, const void* caller) {
    const std::uint64_t key = (static_cast<std::uint64_t>(method) << 32) | reinterpret_cast<std::uintptr_t>(caller);
    std::uint32_t slot = static_cast<std::uint32_t>((key * 0x9E3779B97F4A7C15ull) >> 52) % kSeenSlots;
    for (int probe = 0; probe < kSeenSlots; ++probe, slot = (slot + 1) % kSeenSlots) {
        std::uint64_t expected = 0;
        if (g_seen[slot].compare_exchange_strong(expected, key)) return true;
        if (expected == key) return false;
    }
    return false;   // full: nothing more is logged
}

// "old device: IDirect3DDevice7::SetRenderState from game.exe 0050B22E" (or a module of ours with
// its offset, resolvable through the build's map file).
void Note(const char* interfaceName, const char* method, std::uint32_t id, const void* caller) {
    if (!FirstCall(id, caller) || g_logged.fetch_add(1) >= kMaxLogged) return;
    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCSTR>(caller), &module);
    char name[MAX_PATH] = "?";
    if (module) {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(module, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            const char* base = std::strrchr(path, '\\');
            strncpy_s(name, base ? base + 1 : path, _TRUNCATE);
        }
    }
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    if (module == GetModuleHandleA(nullptr))
        ei::Log("old device: %s::%s from %s %08X", interfaceName, method, name, static_cast<unsigned>(address));
    else
        ei::Log("old device: %s::%s from %s+%X", interfaceName, method, name,
                static_cast<unsigned>(address - reinterpret_cast<std::uintptr_t>(module)));
}

#define EI_NOTE_D3D(method, id) Note("IDirect3DDevice7", method, id, _ReturnAddress())
#define EI_NOTE_DD(method, id) Note("IDirectDraw7", method, 0x100 + (id), _ReturnAddress())

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
    FourCCFormat(MAKEFOURCC('D', 'X', 'T', '3')),
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

// ---- The IDirect3DDevice7 stand-in ----------------------------------------------------------------

constexpr int kRenderStates = 256;
constexpr int kStages = 8;
constexpr int kStageStates = 32;

class NullDirect3DDevice7 final : public IDirect3DDevice7 {
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

    // IUnknown. One object for the whole run: references are not counted.
    STDMETHOD(QueryInterface)(REFIID, LPVOID* out) override {
        EI_NOTE_D3D("QueryInterface", 0);
        if (out) *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 1; }
    STDMETHOD_(ULONG, Release)() override { return 1; }

    STDMETHOD(GetCaps)(LPD3DDEVICEDESC7 desc) override {
        EI_NOTE_D3D("GetCaps", 3);
        if (!desc) return DDERR_INVALIDPARAMS;
        *desc = DeviceCaps();
        return D3D_OK;
    }
    STDMETHOD(EnumTextureFormats)(LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context) override {
        EI_NOTE_D3D("EnumTextureFormats", 4);
        int count = 0;
        const DDPIXELFORMAT* formats = device::SwitchOverTextureFormats(&count);
        for (int i = 0; i < count && callback; ++i) {
            DDPIXELFORMAT copy = formats[i];
            if (callback(&copy, context) != D3DENUMRET_OK) break;
        }
        return D3D_OK;
    }
    STDMETHOD(BeginScene)() override {
        EI_NOTE_D3D("BeginScene", 5);
        if (inScene_) return D3DERR_SCENE_IN_SCENE;
        inScene_ = true;
        return D3D_OK;
    }
    STDMETHOD(EndScene)() override {
        EI_NOTE_D3D("EndScene", 6);
        if (!inScene_) return D3DERR_SCENE_NOT_IN_SCENE;
        inScene_ = false;
        return D3D_OK;
    }
    STDMETHOD(GetDirect3D)(LPDIRECT3D7* out) override {
        EI_NOTE_D3D("GetDirect3D", 7);
        if (out) *out = nullptr;
        return DDERR_UNSUPPORTED;
    }
    STDMETHOD(SetRenderTarget)(LPDIRECTDRAWSURFACE7, DWORD) override {
        EI_NOTE_D3D("SetRenderTarget", 8);
        return D3D_OK;
    }
    STDMETHOD(GetRenderTarget)(LPDIRECTDRAWSURFACE7* out) override {
        EI_NOTE_D3D("GetRenderTarget", 9);
        if (out) *out = nullptr;
        return DDERR_UNSUPPORTED;
    }
    STDMETHOD(Clear)(DWORD, LPD3DRECT, DWORD, D3DCOLOR, D3DVALUE, DWORD) override {
        EI_NOTE_D3D("Clear", 10);
        return D3D_OK;
    }
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override {
        EI_NOTE_D3D("SetTransform", 11);
        return D3D_OK;
    }
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE, LPD3DMATRIX matrix) override {
        EI_NOTE_D3D("GetTransform", 12);
        if (matrix) *matrix = D3DMATRIX{};
        return D3D_OK;
    }
    STDMETHOD(SetViewport)(LPD3DVIEWPORT7 viewport) override {
        EI_NOTE_D3D("SetViewport", 13);
        if (viewport) viewport_ = *viewport;
        return D3D_OK;
    }
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override {
        EI_NOTE_D3D("MultiplyTransform", 14);
        return D3D_OK;
    }
    STDMETHOD(GetViewport)(LPD3DVIEWPORT7 viewport) override {
        EI_NOTE_D3D("GetViewport", 15);
        if (viewport) *viewport = viewport_;
        return D3D_OK;
    }
    STDMETHOD(SetMaterial)(LPD3DMATERIAL7) override {
        EI_NOTE_D3D("SetMaterial", 16);
        return D3D_OK;
    }
    STDMETHOD(GetMaterial)(LPD3DMATERIAL7 material) override {
        EI_NOTE_D3D("GetMaterial", 17);
        if (material) *material = D3DMATERIAL7{};
        return D3D_OK;
    }
    STDMETHOD(SetLight)(DWORD, LPD3DLIGHT7) override {
        EI_NOTE_D3D("SetLight", 18);
        return D3D_OK;
    }
    STDMETHOD(GetLight)(DWORD, LPD3DLIGHT7 light) override {
        EI_NOTE_D3D("GetLight", 19);
        if (light) *light = D3DLIGHT7{};
        return D3D_OK;
    }
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE state, DWORD value) override {
        EI_NOTE_D3D("SetRenderState", 20);
        SetState(Entry{false, 0, static_cast<DWORD>(state), value});
        return D3D_OK;
    }
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE state, LPDWORD value) override {
        EI_NOTE_D3D("GetRenderState", 21);
        if (!value) return DDERR_INVALIDPARAMS;
        *value = static_cast<DWORD>(state) < kRenderStates ? render_[state] : 0;
        return D3D_OK;
    }
    STDMETHOD(BeginStateBlock)() override {
        EI_NOTE_D3D("BeginStateBlock", 22);
        blocks_.push_back(Block{});
        recording_ = &blocks_.back();
        return D3D_OK;
    }
    STDMETHOD(EndStateBlock)(LPDWORD handle) override {
        EI_NOTE_D3D("EndStateBlock", 23);
        if (!recording_) return D3DERR_NOTINBEGINSTATEBLOCK;
        recording_->handle = NextHandle();
        if (handle) *handle = recording_->handle;
        recording_ = nullptr;
        return D3D_OK;
    }
    STDMETHOD(PreLoad)(LPDIRECTDRAWSURFACE7) override {
        EI_NOTE_D3D("PreLoad", 24);
        return D3D_OK;
    }
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, DWORD) override {
        EI_NOTE_D3D("DrawPrimitive", 25);
        return D3D_OK;
    }
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, LPWORD, DWORD, DWORD) override {
        EI_NOTE_D3D("DrawIndexedPrimitive", 26);
        return D3D_OK;
    }
    STDMETHOD(SetClipStatus)(LPD3DCLIPSTATUS) override {
        EI_NOTE_D3D("SetClipStatus", 27);
        return D3D_OK;
    }
    STDMETHOD(GetClipStatus)(LPD3DCLIPSTATUS status) override {
        EI_NOTE_D3D("GetClipStatus", 28);
        if (status) *status = D3DCLIPSTATUS{};
        return D3D_OK;
    }
    STDMETHOD(DrawPrimitiveStrided)(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, DWORD) override {
        EI_NOTE_D3D("DrawPrimitiveStrided", 29);
        return D3D_OK;
    }
    STDMETHOD(DrawIndexedPrimitiveStrided)(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, LPWORD, DWORD,
                                           DWORD) override {
        EI_NOTE_D3D("DrawIndexedPrimitiveStrided", 30);
        return D3D_OK;
    }
    STDMETHOD(DrawPrimitiveVB)(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, DWORD) override {
        EI_NOTE_D3D("DrawPrimitiveVB", 31);
        return D3D_OK;
    }
    STDMETHOD(DrawIndexedPrimitiveVB)(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, LPWORD, DWORD, DWORD) override {
        EI_NOTE_D3D("DrawIndexedPrimitiveVB", 32);
        return D3D_OK;
    }
    STDMETHOD(ComputeSphereVisibility)(LPD3DVECTOR, LPD3DVALUE, DWORD count, DWORD, LPDWORD results) override {
        EI_NOTE_D3D("ComputeSphereVisibility", 33);
        if (results)
            for (DWORD i = 0; i < count; ++i) results[i] = 0;
        return D3D_OK;
    }
    STDMETHOD(GetTexture)(DWORD stage, LPDIRECTDRAWSURFACE7* texture) override {
        EI_NOTE_D3D("GetTexture", 34);
        if (!texture) return DDERR_INVALIDPARAMS;
        *texture = stage < kStages ? textures_[stage] : nullptr;
        if (*texture) (*texture)->AddRef();
        return D3D_OK;
    }
    STDMETHOD(SetTexture)(DWORD stage, LPDIRECTDRAWSURFACE7 texture) override {
        EI_NOTE_D3D("SetTexture", 35);
        if (stage >= kStages) return DDERR_INVALIDPARAMS;
        if (texture) texture->AddRef();
        if (textures_[stage]) textures_[stage]->Release();
        textures_[stage] = texture;
        return D3D_OK;
    }
    STDMETHOD(GetTextureStageState)(DWORD stage, D3DTEXTURESTAGESTATETYPE state, LPDWORD value) override {
        EI_NOTE_D3D("GetTextureStageState", 36);
        if (!value) return DDERR_INVALIDPARAMS;
        *value = stage < kStages && static_cast<DWORD>(state) < kStageStates ? stage_[stage][state] : 0;
        return D3D_OK;
    }
    STDMETHOD(SetTextureStageState)(DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value) override {
        EI_NOTE_D3D("SetTextureStageState", 37);
        SetState(Entry{true, stage, static_cast<DWORD>(state), value});
        return D3D_OK;
    }
    STDMETHOD(ValidateDevice)(LPDWORD passes) override {
        EI_NOTE_D3D("ValidateDevice", 38);
        if (passes) *passes = 1;
        return D3D_OK;
    }
    STDMETHOD(ApplyStateBlock)(DWORD handle) override {
        EI_NOTE_D3D("ApplyStateBlock", 39);
        const Block* block = Find(handle);
        if (!block) return D3DERR_INVALIDSTATEBLOCK;
        const std::vector<Entry> entries = block->entries;   // (an apply while recording is recorded)
        for (const Entry& e : entries) SetState(e);
        return D3D_OK;
    }
    STDMETHOD(CaptureStateBlock)(DWORD handle) override {
        EI_NOTE_D3D("CaptureStateBlock", 40);
        Block* block = Find(handle);
        if (!block) return D3DERR_INVALIDSTATEBLOCK;
        for (Entry& e : block->entries) e.value = Current(e);
        return D3D_OK;
    }
    STDMETHOD(DeleteStateBlock)(DWORD handle) override {
        EI_NOTE_D3D("DeleteStateBlock", 41);
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
            if (it->handle != handle || &*it == recording_) continue;
            blocks_.erase(it);
            return D3D_OK;
        }
        return D3DERR_INVALIDSTATEBLOCK;
    }
    // Every state (D3DSBT_ALL and the two halves alike), as it is now.
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE, LPDWORD handle) override {
        EI_NOTE_D3D("CreateStateBlock", 42);
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
    STDMETHOD(Load)(LPDIRECTDRAWSURFACE7, LPPOINT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override {
        EI_NOTE_D3D("Load", 43);
        return D3D_OK;
    }
    STDMETHOD(LightEnable)(DWORD, BOOL) override {
        EI_NOTE_D3D("LightEnable", 44);
        return D3D_OK;
    }
    STDMETHOD(GetLightEnable)(DWORD, BOOL* enabled) override {
        EI_NOTE_D3D("GetLightEnable", 45);
        if (enabled) *enabled = FALSE;
        return D3D_OK;
    }
    STDMETHOD(SetClipPlane)(DWORD, D3DVALUE*) override {
        EI_NOTE_D3D("SetClipPlane", 46);
        return D3D_OK;
    }
    STDMETHOD(GetClipPlane)(DWORD, D3DVALUE* plane) override {
        EI_NOTE_D3D("GetClipPlane", 47);
        if (plane)
            for (int i = 0; i < 4; ++i) plane[i] = 0.0f;
        return D3D_OK;
    }
    STDMETHOD(GetInfo)(DWORD, LPVOID info, DWORD size) override {
        EI_NOTE_D3D("GetInfo", 48);
        if (info) std::memset(info, 0, size);
        return S_FALSE;
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
    DWORD Current(const Entry& e) const {
        if (!e.stageState) return e.state < kRenderStates ? render_[e.state] : 0;
        return e.stage < kStages && e.state < kStageStates ? stage_[e.stage][e.state] : 0;
    }
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
};

// ---- The IDirectDraw7 stand-in --------------------------------------------------------------------

class NullDirectDraw7 final : public IDirectDraw7 {
public:
    void SetMode(int width, int height, int bitsPerPixel) {
        width_ = width;
        height_ = height;
        bits_ = bitsPerPixel;
    }

    STDMETHOD(QueryInterface)(REFIID, LPVOID* out) override {
        EI_NOTE_DD("QueryInterface", 0);
        if (out) *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 1; }
    STDMETHOD_(ULONG, Release)() override { return 1; }

    STDMETHOD(Compact)() override {
        EI_NOTE_DD("Compact", 3);
        return DD_OK;
    }
    STDMETHOD(CreateClipper)(DWORD, LPDIRECTDRAWCLIPPER* out, IUnknown*) override {
        EI_NOTE_DD("CreateClipper", 4);
        if (out) *out = nullptr;
        return DDERR_UNSUPPORTED;
    }
    STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE* out, IUnknown*) override {
        EI_NOTE_DD("CreatePalette", 5);
        if (out) *out = nullptr;
        return DDERR_UNSUPPORTED;
    }
    // No surfaces in the switch-over: the holders that need pixels have the new renderer's memory.
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE7* out, IUnknown*) override {
        EI_NOTE_DD("CreateSurface", 6);
        if (out) *out = nullptr;
        if (refused_++ < 10)
            ei::Log("old device: a %lux%lu surface (caps %08lX) refused: the switch-over has no DirectDraw",
                    desc ? desc->dwWidth : 0ul, desc ? desc->dwHeight : 0ul, desc ? desc->ddsCaps.dwCaps : 0ul);
        return DDERR_UNSUPPORTED;
    }
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE7, LPDIRECTDRAWSURFACE7* out) override {
        EI_NOTE_DD("DuplicateSurface", 7);
        if (out) *out = nullptr;
        return DDERR_UNSUPPORTED;
    }
    // The one display mode: the screen's.
    STDMETHOD(EnumDisplayModes)(DWORD, LPDDSURFACEDESC2, LPVOID context, LPDDENUMMODESCALLBACK2 callback) override {
        EI_NOTE_DD("EnumDisplayModes", 8);
        DDSURFACEDESC2 mode = Mode();
        if (callback) callback(&mode, context);
        return DD_OK;
    }
    STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK7) override {
        EI_NOTE_DD("EnumSurfaces", 9);
        return DD_OK;
    }
    STDMETHOD(FlipToGDISurface)() override {
        EI_NOTE_DD("FlipToGDISurface", 10);
        return DD_OK;
    }
    STDMETHOD(GetCaps)(LPDDCAPS driver, LPDDCAPS emulated) override {
        EI_NOTE_DD("GetCaps", 11);
        if (driver) *driver = DirectDrawCaps();
        if (emulated) {
            *emulated = DDCAPS{};
            emulated->dwSize = sizeof(DDCAPS);
        }
        return DD_OK;
    }
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC2 desc) override {
        EI_NOTE_DD("GetDisplayMode", 12);
        if (!desc) return DDERR_INVALIDPARAMS;
        *desc = Mode();
        return DD_OK;
    }
    STDMETHOD(GetFourCCCodes)(LPDWORD count, LPDWORD) override {
        EI_NOTE_DD("GetFourCCCodes", 13);
        if (count) *count = 0;
        return DD_OK;
    }
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE7* out) override {
        EI_NOTE_DD("GetGDISurface", 14);
        if (out) *out = nullptr;
        return DDERR_NOTFOUND;
    }
    STDMETHOD(GetMonitorFrequency)(LPDWORD frequency) override {
        EI_NOTE_DD("GetMonitorFrequency", 15);
        if (frequency) *frequency = kRefreshRate;
        return DD_OK;
    }
    STDMETHOD(GetScanLine)(LPDWORD line) override {
        EI_NOTE_DD("GetScanLine", 16);
        if (line) *line = 0;
        return DDERR_VERTICALBLANKINPROGRESS;
    }
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL inBlank) override {
        EI_NOTE_DD("GetVerticalBlankStatus", 17);
        if (inBlank) *inBlank = TRUE;
        return DD_OK;
    }
    STDMETHOD(Initialize)(GUID*) override {
        EI_NOTE_DD("Initialize", 18);
        return DDERR_ALREADYINITIALIZED;
    }
    STDMETHOD(RestoreDisplayMode)() override {
        EI_NOTE_DD("RestoreDisplayMode", 19);
        return DD_OK;
    }
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) override {
        EI_NOTE_DD("SetCooperativeLevel", 20);
        return DD_OK;
    }
    STDMETHOD(SetDisplayMode)(DWORD, DWORD, DWORD, DWORD, DWORD) override {
        EI_NOTE_DD("SetDisplayMode", 21);
        return DD_OK;
    }
    STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) override {
        EI_NOTE_DD("WaitForVerticalBlank", 22);
        return DD_OK;
    }
    STDMETHOD(GetAvailableVidMem)(LPDDSCAPS2, LPDWORD total, LPDWORD free) override {
        EI_NOTE_DD("GetAvailableVidMem", 23);
        if (total) *total = kVideoMemory;
        if (free) *free = kVideoMemory;
        return DD_OK;
    }
    STDMETHOD(GetSurfaceFromDC)(HDC, LPDIRECTDRAWSURFACE7* out) override {
        EI_NOTE_DD("GetSurfaceFromDC", 24);
        if (out) *out = nullptr;
        return DDERR_NOTFOUND;
    }
    STDMETHOD(RestoreAllSurfaces)() override {
        EI_NOTE_DD("RestoreAllSurfaces", 25);
        return DD_OK;
    }
    STDMETHOD(TestCooperativeLevel)() override {
        EI_NOTE_DD("TestCooperativeLevel", 26);
        return DD_OK;
    }
    STDMETHOD(GetDeviceIdentifier)(LPDDDEVICEIDENTIFIER2 id, DWORD) override {
        EI_NOTE_DD("GetDeviceIdentifier", 27);
        if (id) *id = DDDEVICEIDENTIFIER2{};
        return DD_OK;
    }
    STDMETHOD(StartModeTest)(LPSIZE, DWORD, DWORD) override {
        EI_NOTE_DD("StartModeTest", 28);
        return DDERR_UNSUPPORTED;
    }
    STDMETHOD(EvaluateMode)(DWORD, DWORD*) override {
        EI_NOTE_DD("EvaluateMode", 29);
        return DDERR_UNSUPPORTED;
    }

private:
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

    int width_ = 0, height_ = 0, bits_ = 32;
    int refused_ = 0;
};

NullDirect3DDevice7 g_device;
NullDirectDraw7 g_directDraw;
bool g_withoutDirectDraw = false;

} // namespace

namespace device {

bool WithoutDirectDraw() { return g_withoutDirectDraw; }

IDirectDraw7* StartWithoutDirectDraw(int width, int height, int bitsPerPixel) {
    g_withoutDirectDraw = true;
    g_directDraw.SetMode(width, height, bitsPerPixel);
    ei::Log("device: built without DirectDraw or Direct3D (%dx%d, %d bits); the new renderer presents", width, height,
            bitsPerPixel);
    return &g_directDraw;
}

IDirect3DDevice7* NullDirect3DDevice() {
    g_device.Reset();
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

} // namespace device
