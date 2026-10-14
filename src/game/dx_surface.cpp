// The off-screen surface holder (CDXSurface, texture.h; 0x20 bytes, vtable 0073D774): its
// constructor for a system-memory surface of the screen's pixel format (004EF7B0, thiscall: device
// object, width, height; RET 0xC), its constructor from an .mmp picture (004EFC60) with the picture
// loader it calls (004F0060) and its destructor body (004EFD20). Used for the interface windows'
// surfaces (005FCC00 / 005FCDD0 / 005FD7A0), the software cursor's (005E60B0) and its images
// (005E4EB0 -> 004EFC60), and the movies' (005DA400, 005DB3F0, 005DBA50).
//
// With the new renderer's "cpuwindows" option the surface's pixels live in memory the new renderer
// owns (render::NewSurfaceMemory, a DIB section). Outside the switch-over that memory is given to
// DirectDraw as the surface's (client memory: DDSD_LPSURFACE / DDSD_PITCH): GDI through the
// surface's DC, the pixel code through Lock and the old path's Blt all work on it. In the
// switch-over ("present") the holder gets no DirectDraw surface at all (ticket R7): the game code
// works on the memory directly (DirectPixels; GDI through a memory DC, ui_surface_dc.cpp) and the
// new renderer reads it. Picture surfaces (004F0060) get the same memory when they are asked for in
// system memory. The save / load screen's picture textures get such memory beside their DirectDraw
// texture (AttachTextureMemory, ui_saveload.cpp); in the switch-over every texture the texture
// manager makes for the CPU (00691D80, texture_blank.cpp) gets a holder with only such memory
// (NewMemoryTextureHolder, ticket R8). See docs/notes/renderer.md (sections 5, 10, 14, 15).
#include "dx_surface.h"
#include "device.h"
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include "../hook/log.h"

#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_library.h"   // CxxThrowException

namespace {

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
int& HolderCount() { return *reinterpret_cast<int*>(0x007AEE94); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
// The holder's vtable and its base class's.
void SetHolderVtable(CDXSurface* holder) { ei::SetVtable(holder, 0x0073D774); }
void SetBaseVtable(CDXSurface* holder) { ei::SetVtable(holder, 0x0073D768); }

// 004800E0 / 00480080 (cdecl): DirectX error / fatal error reports (the second printf-like);
// nonzero to go on.
// The thrown error object: operator new (007005AF), its constructor (00480250, cdecl: object,
// format, width, height) and _CxxThrowException with its throw info (originals_engine.h).
__declspec(naked) void* __cdecl ErrorObject(void*, const char*, int, int) EI_ORIGINAL(0x00480250)

constexpr int kRelease = 2, kGetPixelFormat = 21, kGetSurfaceDesc = 22, kLock = 25, kUnlock = 32;   // IDirectDrawSurface7
constexpr std::uint32_t kDescFlags = 0x1007;    // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
constexpr std::uint32_t kPixelFormatFlag = 0x1000;   // DDSD_PIXELFORMAT
constexpr std::uint32_t kClientMemory = 0x808;  // DDSD_LPSURFACE | DDSD_PITCH
constexpr std::uint32_t kCaps = 0x840;          // DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY
constexpr std::uint32_t kAlphaPixels = 0x1, kRgb = 0x40;   // DDPIXELFORMAT flags

// DDSURFACEDESC2 as dwords: +0x04 flags, +0x08 height, +0x0C width, +0x10 pitch, +0x24 memory,
// +0x48 the pixel format (+0x4C flags, +0x54 bits, +0x58..+0x64 R, G, B, A masks), +0x68 caps.
enum : int { kFlags = 1, kHeight = 2, kWidth = 3, kPitch = 4, kSurface = 9, kFormat = 18, kBits = 21, kRed = 22,
             kGreen = 23, kBlue = 24, kAlpha = 25, kCapsWord = 26 };

// --- The new renderer's memory ("cpuwindows") ------------------------------------------------------

int g_clientFailures = 0;
bool g_memoryOnlyLogged = false;
bool g_memoryTexturesLogged = false;
bool g_decodedTexturesLogged = false;

// Memory of the new renderer's for `holder` in the format `desc` gives (16 or 32-bit RGB): its pitch
// and address go into the description as the surface's client memory. False when there is none.
bool AttachClientMemory(const CDXSurface* holder, std::uint32_t* desc) {
    std::uint32_t bits = desc[kBits];
    if ((bits != 16 && bits != 32) || !(desc[kFormat + 1] & kRgb)) return false;
    std::uint32_t alpha = (desc[kFormat + 1] & kAlphaPixels) ? desc[kAlpha] : 0;
    const render::SurfaceMemory* memory =
        render::NewSurfaceMemory(holder, static_cast<int>(desc[kWidth]), static_cast<int>(desc[kHeight]),
                                 static_cast<int>(bits), desc[kRed], desc[kGreen], desc[kBlue], alpha);
    if (!memory) return false;
    desc[kFlags] |= kClientMemory;
    desc[kPitch] = static_cast<std::uint32_t>(memory->pitch);
    desc[kSurface] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory->bits));
    return true;
}

// DirectDraw refused the client memory: the memory freed, the description without it (and without
// `addedFlags`, set for it).
void DropClientMemory(const CDXSurface* holder, std::uint32_t* desc, std::uint32_t addedFlags) {
    render::FreeSurfaceMemory(holder);
    desc[kFlags] &= ~addedFlags;
    desc[kPitch] = 0;
    desc[kSurface] = 0;
}

// In the switch-over ("present") a holder whose pixels are in the new renderer's memory (`client`)
// gets no DirectDraw surface at all: nothing of the old path draws from it.
bool MemoryOnly(bool client) {
    if (!client || !render::Presenting()) return false;
    if (!g_memoryOnlyLogged) {
        g_memoryOnlyLogged = true;
        ei::Log("cpuwindows: surface holders in the switch-over get no DirectDraw surface, only the new renderer's memory");
    }
    return true;
}

[[noreturn]] void ThrowCreateFailed(int width, int height) {
    void* object = gamecrt::operator_new(0xC);
    if (object) object = ErrorObject(object, Text(0x0078E89C), width, height);   // "Can't create DDSurface %d %d ..."
    CxxThrowException(&object, GameErrorThrowInfo());
    for (;;) {
    }
}

// The surface's width, height and bits per pixel into the holder (GetSurfaceDesc, the result in
// +8); false when that failed and the report said stop.
bool ReadSize(CDXSurface* holder) {
    std::uint32_t d[0x1F] = {};
    holder->width = 0;
    holder->height = 0;
    holder->bitsPerPixel = 0;
    d[0] = 0x7C;
    long hr = ComCall(holder->surface, kGetSurfaceDesc, static_cast<void*>(d));
    holder->result = hr;
    if (hr != 0 && !FatalDirectX(hr, Text(0x0078E6C4))) return false;   // "Can't get surface description"
    holder->width = static_cast<int>(d[kWidth]);
    holder->height = static_cast<int>(d[kHeight]);
    holder->bitsPerPixel = static_cast<int>(d[kBits]);
    return true;
}

// The display mode's pixel format into `format` (a DDPIXELFORMAT; IDirectDraw7::GetDisplayMode).
// False when DirectDraw does not give an RGB one.
bool ReadDisplayModeFormat(CTextureDevice* device, std::uint32_t* format) {
    if (!device->ddraw_) return false;
    std::uint32_t mode[0x1F] = {};
    mode[0] = 0x7C;
    if (device::GetDisplayMode(device::DDraw(device), reinterpret_cast<DDSURFACEDESC2*>(mode)) != 0 ||
        !(mode[kFormat + 1] & kRgb))
        return false;
    std::memcpy(format, &mode[kFormat], 8 * sizeof(std::uint32_t));
    return true;
}

// The screen's pixel format (the device's primary surface) into `format` (a DDPIXELFORMAT). In the
// switch-over with "cpuwindows" the display mode's instead (ticket R8, towards a device without a
// primary surface): the primary is in the display mode's format (full screen and windowed), so the
// holders get the same format, and nothing of theirs needs the primary surface.
void ReadScreenFormat(CTextureDevice* device, std::uint32_t* format) {
    if (render::Presenting() && render::CpuWindows() && ReadDisplayModeFormat(device, format)) return;
    CDXSurface* primary = device->primary_;
    format[0] = 0x20;
    long hr = ComCall(primary->surface, kGetPixelFormat, static_cast<void*>(format));
    primary->result = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E714));   // "Can't get pixel format from surface"
}

} // namespace

HBITMAP ClientBitmap(const void* holder) {
    const render::SurfaceMemory* memory = render::SurfaceMemoryOf(holder);
    return memory ? static_cast<HBITMAP>(memory->bitmap) : nullptr;
}

bool DirectPixels(const void* holder, std::uint32_t* desc) {
    if (!holder) return false;
    bool noSurface = !static_cast<const CDXSurface*>(holder)->surface;
    return (render::Presenting() || noSurface) && ClientPixels(holder, desc);
}

bool ClientPixels(const void* holder, std::uint32_t* desc) {
    const render::SurfaceMemory* m = render::SurfaceMemoryOf(holder);
    if (!m) return false;
    ::GdiFlush();   // GDI may still hold drawing for the memory (not imported by the game)
    std::memset(desc, 0, 0x7C);
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags | kClientMemory;
    desc[kWidth] = static_cast<std::uint32_t>(m->width);
    desc[kHeight] = static_cast<std::uint32_t>(m->height);
    desc[kPitch] = static_cast<std::uint32_t>(m->pitch);
    desc[kSurface] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m->bits));
    desc[kFormat] = 0x20;
    desc[kFormat + 1] = kRgb | (m->alphaMask ? kAlphaPixels : 0);
    desc[kBits] = static_cast<std::uint32_t>(m->bitsPerPixel);
    desc[kRed] = m->redMask;
    desc[kGreen] = m->greenMask;
    desc[kBlue] = m->blueMask;
    desc[kAlpha] = m->alphaMask;
    desc[kCapsWord] = kCaps;
    return true;
}

bool AttachTextureMemory(void* holder, int width, int height, const std::uint32_t* pixelFormat) {
    if (!holder || !render::CpuWindows()) return false;
    if (render::SurfaceMemoryOf(holder)) return true;
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags;
    desc[kWidth] = static_cast<std::uint32_t>(width);
    desc[kHeight] = static_cast<std::uint32_t>(height);
    std::memcpy(&desc[kFormat], pixelFormat, 8 * sizeof(std::uint32_t));
    if (!AttachClientMemory(static_cast<const CDXSurface*>(holder), desc)) {
        if (g_clientFailures++ < 5)
            ei::Log("cpuwindows: no game memory for a %dx%d texture (format flags %08X, %u bits); it stays in "
                    "DirectDraw only", width, height, desc[kFormat + 1], desc[kBits]);
        return false;
    }
    return true;
}

namespace {

// A texture holder as 004EFA60 starts one (vtable, device, no surface yet), on the game's heap.
CDXSurface* StartTextureHolder(CTextureDevice* device) {
    auto* holder = static_cast<CDXSurface*>(gamecrt::operator_new(sizeof(CDXSurface)));
    if (!holder) return nullptr;
    holder->refCount = 0;
    holder->result = 0;
    holder->device = device;
    holder->surface = nullptr;
    holder->width = 0;
    holder->height = 0;
    holder->bitsPerPixel = 0;
    SetHolderVtable(holder);
    return holder;
}

// The holder stands for a texture made as requested: described by the request, the creation
// counted as done (the device's result 0, the holder count one higher).
void FinishTextureHolder(CDXSurface* holder, int width, int height, int bitsPerPixel) {
    holder->device->lastLoadResult_ = 0;
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = bitsPerPixel;
    ++HolderCount();
}

} // namespace

CDXSurface* NewDecodedTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat) {
    if (!device || !render::Presenting() || !render::CpuWindows()) return nullptr;
    CDXSurface* holder = StartTextureHolder(device);
    if (!holder) return nullptr;
    if (!g_decodedTexturesLogged) {
        g_decodedTexturesLogged = true;
        ei::Log("cpuwindows: the texture manager's .mmp textures in the switch-over get no DirectDraw texture, only "
                "the new renderer's decoded copy");
    }
    // (A DDPIXELFORMAT: its bit count, as a surface of that format reports it.)
    FinishTextureHolder(holder, width, height, static_cast<int>(pixelFormat[kBits - kFormat]));
    return holder;
}

CDXSurface* NewMemoryTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat) {
    if (!device || !render::Presenting() || !render::CpuWindows()) return nullptr;
    CDXSurface* holder = StartTextureHolder(device);
    if (!holder) return nullptr;
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags;
    desc[kWidth] = static_cast<std::uint32_t>(width);
    desc[kHeight] = static_cast<std::uint32_t>(height);
    std::memcpy(&desc[kFormat], pixelFormat, 8 * sizeof(std::uint32_t));
    if (!AttachClientMemory(holder, desc)) {
        if (g_clientFailures++ < 5)
            ei::Log("cpuwindows: no memory for a %dx%d texture (format flags %08X, %u bits); a DirectDraw texture "
                    "instead", width, height, desc[kFormat + 1], desc[kBits]);
        gamecrt::operator_delete(holder);
        return nullptr;
    }
    if (!g_memoryTexturesLogged) {
        g_memoryTexturesLogged = true;
        ei::Log("cpuwindows: the texture manager's CPU-written textures in the switch-over get no DirectDraw "
                "texture, only the new renderer's memory");
    }
    // The memory is the surface.
    FinishTextureHolder(holder, width, height, static_cast<int>(desc[kBits]));
    return holder;
}

// 004EF7B0. Returns the holder; when the surface cannot be made a C++ exception is thrown (as the
// original: an error object by pointer). In the switch-over with "cpuwindows" the holder gets the
// new renderer's memory and no DirectDraw surface (MemoryOnly).
CDXSurface* __fastcall DXSurfaceConstruct(CDXSurface* holder, void*, CTextureDevice* device, int width, int height) {
    holder->refCount = 0;
    holder->result = 0;
    std::uint32_t desc[0x1F] = {};
    desc[kWidth] = static_cast<std::uint32_t>(width);
    desc[kHeight] = static_cast<std::uint32_t>(height);
    holder->device = device;
    holder->surface = nullptr;
    holder->width = 0;
    holder->height = 0;
    holder->bitsPerPixel = 0;
    SetHolderVtable(holder);
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags;
    desc[kCapsWord] = kCaps;
    ReadScreenFormat(device, &desc[kFormat]);

    if (!holder->device) {
        if (!game::FatalError(Text(0x0078E8CC))) ThrowCreateFailed(width, height);
        // (The original goes on with no surface and fails in GetSurfaceDesc.)
        ReadSize(holder);
        ++HolderCount();
        return holder;
    }
    bool client = render::CpuWindows() && AttachClientMemory(holder, desc);
    CTextureDevice* owner = holder->device;
    if (MemoryOnly(client)) {
        // The memory is the surface: described by the request, the creation counted as done.
        owner->lastLoadResult_ = 0;
        holder->width = width;
        holder->height = height;
        holder->bitsPerPixel = static_cast<int>(desc[kBits]);
        ++HolderCount();
        return holder;
    }
    void* surface = nullptr;
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = device::CreateSurface(device::DDraw(owner), reinterpret_cast<DDSURFACEDESC2*>(desc),
                                   reinterpret_cast<IDirectDrawSurface7**>(&surface));
    owner->lastLoadResult_ = hr;
    if (hr != 0 && client) {
        // DirectDraw refused the client memory: the surface keeps its own.
        if (g_clientFailures++ < 5)
            ei::Log("cpuwindows: client-memory surface %dx%d refused (%08lX); a plain one instead", width, height,
                    static_cast<unsigned long>(hr));
        DropClientMemory(holder, desc, kClientMemory);
        client = false;
        surface = nullptr;
        hr = device::CreateSurface(device::DDraw(owner), reinterpret_cast<DDSURFACEDESC2*>(desc),
                                   reinterpret_cast<IDirectDrawSurface7**>(&surface));
        owner->lastLoadResult_ = hr;
    }
    gameapi::LeaveCriticalSection(DeviceLock());
    if (hr != 0) surface = nullptr;
    holder->surface = surface;
    if (!surface) {
        if (client) render::FreeSurfaceMemory(holder);
        ThrowCreateFailed(width, height);
    }
    if (!ReadSize(holder)) {
        if (client) {
            ComCall(surface, kRelease);
            holder->surface = nullptr;
            render::FreeSurfaceMemory(holder);
        }
        ThrowCreateFailed(width, height);
    }
    ReadSize(holder);
    ++HolderCount();
    return holder;
}
EI_REPLACE(0x004EF7B0, DXSurfaceConstruct);

namespace {

constexpr std::uint32_t kFormat8888 = 0x8888;

// The picture's pixels after the .mmp header and its extra block.
const std::uint8_t* PicturePixels(const MmpHeader* picture) {
    return reinterpret_cast<const std::uint8_t*>(picture) + sizeof(MmpHeader) + picture->extraSize;
}

// The picture's pixels into the memory `desc` describes (locked, or the new renderer's memory), of
// the surface's pixel format `format` (a DDPIXELFORMAT). The device's pixel converter (2DintMMX's
// graphics processor): slot 1 takes the target format (0 5:6:5, 1 5:5:5, 2 1:5:5:5, -1 another)
// and its alpha, red, green and blue masks; slot 2 converts one row of 8888 pixels (out, in,
// format, count).
void FillPicture(CDXSurface* holder, const MmpHeader* picture, const std::uint32_t* desc, const std::uint32_t* format) {
    const std::uint8_t* source = PicturePixels(picture);
    auto* out = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc[kSurface]));
    std::uint32_t bits = format[kBits - kFormat];
    if (bits == 32) {
        for (std::uint32_t row = 0; row < picture->height; ++row) {
            std::memcpy(out, source, picture->width * 4);
            out += static_cast<std::int32_t>(desc[kPitch]);
            source += picture->width * 4;
        }
    } else if (bits == 16) {
        ChannelLayout layout;
        layout.Split(format);
        int target = -1;
        if (layout.redBits == 5) {
            if (layout.greenBits == 5) {
                if (layout.blueBits == 5) {
                    if (layout.alphaBits == 1) target = 2;
                    else if (layout.alphaBits == 0) target = 1;
                }
            } else if (layout.greenBits == 6 && layout.blueBits == 5 && layout.alphaBits == 0) {
                target = 0;
            }
        }
        auto* converter = static_cast<CPixelConverter*>(holder->device->converter_);
        converter->SetChannelMasks(target, layout.alphaMask, layout.redMask, layout.greenMask, layout.blueMask);
        for (std::uint32_t row = 0; row < picture->height; ++row) {
            converter->ConvertTo16(reinterpret_cast<std::uint16_t*>(out), reinterpret_cast<const std::uint32_t*>(source), target,
                                   picture->width);
            out += static_cast<std::int32_t>(desc[kPitch]);
            source += picture->width * 4;
        }
    } else if (bits == 24) {
        game::FatalError(Text(0x0078EA10));   // "Can't load picture to 24bit surface! ..."
        // (The surface cleared as if it had no row padding, as the original.)
        std::memset(out, 0, (bits >> 3) * desc[kHeight] * desc[kWidth]);
    }
}

} // namespace

// 004F0060 (thiscall on the holder, RET 8): makes the holder's surface (when it has none yet) of
// the size of an 8888 .mmp picture, in system memory, or in video memory when the low byte of
// `flag` is set, with the screen's pixel format, and loads the picture into it: copied to a 32-bit
// surface, converted row by row through the device's converter to a 16-bit one. 1 when done;
// otherwise the result of the fatal error report (nonzero when the player goes on).
//
// With "cpuwindows" a system-memory surface gets the new renderer's memory (as 004EF7B0; refused ->
// a plain surface, logged); in the switch-over ("present") there is no DirectDraw surface then and
// the picture is written straight into the memory: no Lock / Unlock, its size and pixel format from
// that memory (DirectPixels).
// Original details kept: a holder that already has a surface (only the dead loaders 004EFF30 /
// 004EFFF0 pass one) must have the picture's width in both +0x14 and +0x18; a failed Lock whose
// report says stop leaves the device lock held; a 24-bit surface is reported, then cleared.
int __fastcall DXSurfaceLoadPicture(CDXSurface* holder, void*, const void* data, int flag) {
    const auto* picture = static_cast<const MmpHeader*>(data);
    if (!picture) return game::FatalError(Text(0x0078EB10));                  // "Can't load picture from NULL pointer."
    if (picture->format != kFormat8888) return game::FatalError(Text(0x0078EAE4));   // "... from non-8888 format."
    if (!holder->surface && !render::SurfaceMemoryOf(holder)) {
        std::uint32_t desc[0x1F] = {};
        desc[0] = 0x7C;
        // The screen's pixel format. Without a device the original reads through a null pointer
        // here; 004EFC60 always passes one.
        gameapi::EnterCriticalSection(DeviceLock());
        ReadScreenFormat(holder->device, &desc[kFormat]);
        gameapi::LeaveCriticalSection(DeviceLock());
        // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH: the format (the primary's) is not passed.
        desc[kFlags] = 7;
        desc[kWidth] = picture->width;
        desc[kHeight] = picture->height;
        // Without DirectDraw (device.h) a video-memory request would be refused: the picture gets the
        // new renderer's memory as a system-memory one does (ticket R9a2).
        bool video = (flag & 0xFF) != 0 && !device::WithoutDirectDraw();
        desc[kCapsWord] = video ? 0x4040 : kCaps;   // DDSCAPS_VIDEOMEMORY | DDSCAPS_OFFSCREENPLAIN, or system memory
        // (The original's "no DXDD" report for a holder without a device is unreachable after the
        // read above, as is its release of an earlier surface: there is none on this path.)
        bool client = !video && render::CpuWindows() && AttachClientMemory(holder, desc);
        if (client) desc[kFlags] |= kPixelFormatFlag;   // the DIB's format, the one DirectDraw would pick
        bool memoryOnly = MemoryOnly(client);
        CTextureDevice* device = holder->device;
        void* surface = nullptr;
        if (memoryOnly) {
            device->lastLoadResult_ = 0;   // the memory is the surface
        } else {
            gameapi::EnterCriticalSection(DeviceLock());
            long hr = device::CreateSurface(device::DDraw(device), reinterpret_cast<DDSURFACEDESC2*>(desc),
                                   reinterpret_cast<IDirectDrawSurface7**>(&surface));
            device->lastLoadResult_ = hr;
            if (hr != 0 && client) {
                // DirectDraw refused the client memory: the surface keeps its own.
                if (g_clientFailures++ < 5)
                    ei::Log("cpuwindows: client-memory surface %ux%u refused (%08lX) for a picture; a plain one instead",
                            picture->width, picture->height, static_cast<unsigned long>(hr));
                DropClientMemory(holder, desc, kClientMemory | kPixelFormatFlag);
                client = false;
                surface = nullptr;
                hr = device::CreateSurface(device::DDraw(device), reinterpret_cast<DDSURFACEDESC2*>(desc),
                                   reinterpret_cast<IDirectDrawSurface7**>(&surface));
                device->lastLoadResult_ = hr;
            }
            gameapi::LeaveCriticalSection(DeviceLock());
            if (device->lastLoadResult_ != 0) surface = nullptr;
        }
        holder->surface = surface;
        // 004EFEA0's description of the surface (a holder with no surface is described by its
        // memory).
        bool described = memoryOnly;
        if (surface) described = ReadSize(holder);
        else if (client && !memoryOnly) render::FreeSurfaceMemory(holder);
        if (!described)   // "Can't create surface to load picture %d:%d:%d."
            return game::FatalError(Text(0x0078EAB4), picture->width, picture->height, picture->bitsPerPixel);
        holder->width = static_cast<int>(desc[kWidth]);
        holder->height = static_cast<int>(desc[kHeight]);
        holder->bitsPerPixel = static_cast<int>(desc[kBits]);
    } else if (picture->width != static_cast<std::uint32_t>(holder->width) ||
               picture->width != static_cast<std::uint32_t>(holder->height)) {
        return game::FatalError(Text(0x0078E99C));   // "Dimentions of the DD surface are differ from ..."
    }

    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    std::uint32_t format[8] = {};
    bool direct = DirectPixels(holder, desc);
    if (direct) {
        std::memcpy(format, &desc[kFormat], sizeof(format));
        gameapi::EnterCriticalSection(DeviceLock());
    } else {
        format[0] = 0x20;
        long hr = ComCall(holder->surface, kGetPixelFormat, static_cast<void*>(format));
        holder->result = hr;
        if (hr != 0 && !FatalDirectX(hr, Text(0x0078E714)))   // "Can't get pixel format from surface."
            return game::FatalError(Text(0x0078EA80));               // "Can't get surface pixel format to load picture."
        gameapi::EnterCriticalSection(DeviceLock());
        // DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK
        hr = ComCall(holder->surface, kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), 0x821ul,
                     static_cast<void*>(nullptr));
        holder->result = hr;
        if (hr != 0 && !FatalDirectX(hr, Text(0x0078E30C)))   // "Can't lock surface."
            return game::FatalError(Text(0x0078EA58));               // "Can't lock surface to load picture." (lock held)
    }
    FillPicture(holder, picture, desc, format);
    gameapi::LeaveCriticalSection(DeviceLock());
    if (!direct) {
        long hr = ComCall(holder->surface, kUnlock, static_cast<void*>(nullptr));
        holder->result = hr;
        if (hr != 0 && !FatalDirectX(hr, Text(0x0078E2F4)))   // "Can't unlock surface."
            return game::FatalError(Text(0x0078E9E4));               // "Can't unlock surface after load picture."
    }
    return 1;
}
EI_REPLACE(0x004F0060, DXSurfaceLoadPicture);

namespace {
// 004F0060 through its address (the verify build's wrapper runs there).
__declspec(naked) int __fastcall LoadPicture(CDXSurface*, void*, const void*, int) EI_ORIGINAL(0x004F0060)
__declspec(naked) void* __cdecl ErrorObjectText(void*, const char*) EI_ORIGINAL(0x00480250)
} // namespace

// 004EFC60 (thiscall, RET 0xC): a holder whose surface shows an .mmp picture (the software cursor's
// images 005E4EB0, windows made from a picture 005FCF80). The new renderer keeps the picture's
// pixels from the file data (render::PictureFromMmp) instead of reading the surface back. Throws
// the game's error object when the picture cannot be loaded.
CDXSurface* __fastcall DXSurfaceFromPicture(CDXSurface* holder, void*, CTextureDevice* device, const void* picture,
                                            int flag) {
    holder->refCount = 0;
    holder->result = 0;
    holder->device = device;
    holder->surface = nullptr;
    holder->width = 0;
    holder->height = 0;
    holder->bitsPerPixel = 0;
    SetHolderVtable(holder);
    if (!LoadPicture(holder, nullptr, picture, flag)) {
        void* object = gamecrt::operator_new(0xC);
        if (object) object = ErrorObjectText(object, Text(0x0078E918));   // "Can't load picture to surface"
        CxxThrowException(&object, GameErrorThrowInfo());
    }
    if (render::Active()) render::PictureFromMmp(holder, picture);
    ++HolderCount();
    return holder;
}
EI_REPLACE(0x004EFC60, DXSurfaceFromPicture);

// 004EFEA0 (A-S13; thiscall on the holder, plain RET). The surface's width, height and bits per
// pixel into the holder (GetSurfaceDesc, the result in +8). 1; 0 with the sizes zero when that
// failed and the report ("Can't get surface description.") said stop.
int __fastcall DXSurfaceDescribe(CDXSurface* holder) { return ReadSize(holder) ? 1 : 0; }
EI_REPLACE(0x004EFEA0, DXSurfaceDescribe);

// 004EFA60 (A-S13; thiscall on the holder, RET 8). The holder's constructor from a surface
// description: the surface made by the device's DirectDraw object under the device lock (the result
// in the device's +8) and described. No surface, or a failed description whose report said stop:
// the error "Can't create DDSurface %d:%d with default BPP." is thrown (with the holder's sizes,
// zero by then). Returns the holder.
// Original details kept: the surface is described twice; without a device ("no DXDD", reported) a
// report that says go on reads through the null surface.
CDXSurface* __fastcall DXSurfaceFromDesc(CDXSurface* holder, void*, CTextureDevice* device, const std::uint32_t* desc) {
    holder->refCount = 0;
    holder->result = 0;
    holder->device = device;
    holder->surface = nullptr;
    holder->width = 0;
    holder->height = 0;
    holder->bitsPerPixel = 0;
    SetHolderVtable(holder);
    bool made;
    if (!device) {
        made = game::FatalError(Text(0x0078E8CC)) != 0;   // "... Can't create DDSurface - no DXDD."
    } else {
        IDirectDrawSurface7* surface = nullptr;
        gameapi::EnterCriticalSection(DeviceLock());
        device->lastLoadResult_ = device::CreateSurface(
            device::DDraw(device), reinterpret_cast<DDSURFACEDESC2*>(const_cast<std::uint32_t*>(desc)), &surface);
        gameapi::LeaveCriticalSection(DeviceLock());
        holder->surface = device->lastLoadResult_ == 0 ? surface : nullptr;
        if (!holder->surface) ThrowCreateFailed(holder->width, holder->height);
        made = ReadSize(holder);
    }
    if (!made) ThrowCreateFailed(holder->width, holder->height);
    ReadSize(holder);
    ++HolderCount();
    return holder;
}
EI_REPLACE(0x004EFA60, DXSurfaceFromDesc);

// 004EFD20. Releases the surface (when the holder has one) and the new renderer's memory, clears
// the device, counts the holder out and leaves the base class's vtable.
void __fastcall DXSurfaceDestroy(CDXSurface* holder) {
    SetHolderVtable(holder);
    if (holder->surface) ComCall(holder->surface, kRelease);
    render::HolderGone(holder);   // its memory, and a picture kept for it
    holder->device = nullptr;
    --HolderCount();
    SetBaseVtable(holder);
}
EI_REPLACE(0x004EFD20, DXSurfaceDestroy);
