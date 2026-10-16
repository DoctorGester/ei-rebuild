// The off-screen surface holder (CDXSurface, texture.h; 0x20 bytes, vtable 0073D774): its
// constructor for a system-memory surface of the screen's pixel format (004EF7B0, thiscall: device
// object, width, height; RET 0xC), its constructor from an .mmp picture (004EFC60) with the picture
// loader it calls (004F0060) and its destructor body (004EFD20). Used for the interface windows'
// surfaces (005FCC00 / 005FCDD0 / 005FD7A0), the software cursor's (005E60B0) and its images
// (005E4EB0 -> 004EFC60), and the movies' (005DA400, 005DB3F0, 005DBA50).
//
// No holder has a DirectDraw surface (CDXSurface::surface stays null; R7, R11a, R11b): the pixels
// live in memory the new renderer owns (render::NewSurfaceMemory, a DIB section), the game code works
// on it directly (DirectPixels; GDI through a memory DC, ui_surface_dc.cpp) and the new renderer
// reads it. Picture surfaces (004F0060) get the same memory; every texture the texture manager makes
// for the CPU (00691D80, texture_blank.cpp) gets a holder with only such memory
// (NewMemoryTextureHolder), an .mmp texture one with none (NewDecodedTextureHolder). When that memory
// cannot be made the original's "Can't create DDSurface" error is thrown, as when DirectDraw refused
// the surface. See docs/notes/renderer.md (sections 6, 7).
#include "app.h"
#include "dx_surface.h"
#include "dx_errors.h"
#include "game_strings.h"
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

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
int& HolderCount() { return *reinterpret_cast<int*>(0x007AEE94); }
// The holder's vtable and its base class's.
void SetHolderVtable(CDXSurface* holder) { ei::SetVtable(holder, 0x0073D774); }
void SetBaseVtable(CDXSurface* holder) { ei::SetVtable(holder, 0x0073D768); }

// 004800E0 / 00480080 (cdecl): DirectX error / fatal error reports (the second printf-like);
// nonzero to go on.
// The thrown error object: operator new (007005AF), its constructor (GameErrorConstruct, 00480250,
// cdecl: object, format, width, height) and _CxxThrowException with its throw info (originals_engine.h).

constexpr std::uint32_t kDescFlags = 0x1007;    // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
constexpr std::uint32_t kPixelFormatFlag = 0x1000;   // DDSD_PIXELFORMAT
constexpr std::uint32_t kClientMemory = 0x808;  // DDSD_LPSURFACE | DDSD_PITCH
constexpr std::uint32_t kCaps = 0x840;          // DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY
constexpr std::uint32_t kAlphaPixels = 0x1, kRgb = 0x40;   // DDPIXELFORMAT flags

// DDSURFACEDESC2 as dwords: +0x04 flags, +0x08 height, +0x0C width, +0x10 pitch, +0x24 memory,
// +0x48 the pixel format (+0x4C flags, +0x54 bits, +0x58..+0x64 R, G, B, A masks), +0x68 caps.
enum : int { kFlags = 1, kHeight = 2, kWidth = 3, kPitch = 4, kSurface = 9, kFormat = 18, kBits = 21, kRed = 22,
             kGreen = 23, kBlue = 24, kAlpha = 25, kCapsWord = 26 };

// --- The new renderer's memory -----------------------------------------------------------------------

int g_clientFailures = 0;

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

// What DirectDraw's CreateSurface answered for a surface it refused (DDERR_UNSUPPORTED, the
// stand-in's answer before R11b): the device's result when a holder's memory cannot be made.
constexpr long kSurfaceRefused = static_cast<long>(0x80004001);

[[noreturn]] void ThrowCreateFailed(int width, int height) {
    void* object = gamecrt::operator_new(0xC);
    if (object) object = GameErrorConstruct(static_cast<GameError*>(object), game::Text(0x0078E89C), width, height);   // "Can't create DDSurface %d %d ..."
    libcrt::CxxThrowException(&object, GameErrorThrowInfo());
    for (;;) {
    }
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

// The screen's pixel format into `format` (a DDPIXELFORMAT): the display mode's (ticket R8: the
// original reads the primary surface's, which is in the display mode's format; the stand-in always
// answers with an RGB one). Otherwise the original's report.
void ReadScreenFormat(CTextureDevice* device, std::uint32_t* format) {
    if (ReadDisplayModeFormat(device, format)) return;
    app::FatalErrorCode(kSurfaceRefused, game::Text(0x0078E714));   // "Can't get pixel format from surface"
}

} // namespace

void ThrowSurfaceNotMade(CTextureDevice* device, int width, int height) {
    if (device) device->lastLoadResult_ = kSurfaceRefused;
    ThrowCreateFailed(width, height);
}

HBITMAP ClientBitmap(const void* holder) {
    const render::SurfaceMemory* memory = render::SurfaceMemoryOf(holder);
    return memory ? static_cast<HBITMAP>(memory->bitmap) : nullptr;
}

bool DirectPixels(const void* holder, std::uint32_t* desc) { return holder && ClientPixels(holder, desc); }

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
    if (!holder) return false;
    if (render::SurfaceMemoryOf(holder)) return true;
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags;
    desc[kWidth] = static_cast<std::uint32_t>(width);
    desc[kHeight] = static_cast<std::uint32_t>(height);
    std::memcpy(&desc[kFormat], pixelFormat, 8 * sizeof(std::uint32_t));
    if (!AttachClientMemory(static_cast<const CDXSurface*>(holder), desc)) {
        if (g_clientFailures++ < 5)
            ei::Log("cpuwindows: no game memory for a %dx%d texture (format flags %08X, %u bits)", width, height,
                    desc[kFormat + 1], desc[kBits]);
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
    if (!device) return nullptr;
    CDXSurface* holder = StartTextureHolder(device);
    if (!holder) return nullptr;
    // (A DDPIXELFORMAT: its bit count, as a surface of that format reports it.)
    FinishTextureHolder(holder, width, height, static_cast<int>(pixelFormat[kBits - kFormat]));
    return holder;
}

CDXSurface* NewMemoryTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat) {
    if (!device) return nullptr;
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
            ei::Log("cpuwindows: no memory for a %dx%d texture (format flags %08X, %u bits)", width, height,
                    desc[kFormat + 1], desc[kBits]);
        gamecrt::operator_delete(holder);
        return nullptr;
    }
    // The memory is the surface.
    FinishTextureHolder(holder, width, height, static_cast<int>(desc[kBits]));
    return holder;
}

// 004EF7B0. Returns the holder; when the surface cannot be made a C++ exception is thrown (as the
// original: an error object by pointer). The holder gets the new renderer's memory and no
// DirectDraw surface; without that memory the error ends the game.
CDXSurface* CDXSurface::Construct(CTextureDevice* deviceIn, int widthIn, int heightIn) {
    refCount = 0;
    result = 0;
    std::uint32_t desc[0x1F] = {};
    desc[kWidth] = static_cast<std::uint32_t>(widthIn);
    desc[kHeight] = static_cast<std::uint32_t>(heightIn);
    this->device = deviceIn;
    this->surface = nullptr;
    width = 0;
    height = 0;
    bitsPerPixel = 0;
    SetHolderVtable(this);
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags;
    desc[kCapsWord] = kCaps;
    ReadScreenFormat(deviceIn, &desc[kFormat]);

    if (!this->device) {
        // "... Can't create DDSurface - no DXDD." (a report that says go on: the original reads the
        // missing surface's description and fails there; every caller passes the device object)
        game::FatalError(game::Text(0x0078E8CC));
        ThrowCreateFailed(widthIn, heightIn);
    }
    if (!AttachClientMemory(this, desc)) ThrowSurfaceNotMade(this->device, widthIn, heightIn);
    // The memory is the surface: described by the request, the creation counted as done.
    this->device->lastLoadResult_ = 0;
    width = widthIn;
    height = heightIn;
    bitsPerPixel = static_cast<int>(desc[kBits]);
    ++HolderCount();
    return this;
}
EI_REPLACE_METHOD(0x004EF7B0, CDXSurface::Construct);

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
        game::FatalError(game::Text(0x0078EA10));   // "Can't load picture to 24bit surface! ..."
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
// The surface is the new renderer's memory (as 004EF7B0; also for a video-memory request: ticket
// R9a2): no DirectDraw surface, and the picture is written straight into the memory, no Lock /
// Unlock, its size and pixel format from that memory (DirectPixels). No memory: the original's
// report for a surface that could not be made.
// Original details kept: a holder that already has its pixels (only the dead loaders 004EFF30 /
// 004EFFF0 pass one) must have the picture's width in both +0x14 and +0x18; a 24-bit surface is
// reported, then cleared.
int CDXSurface::LoadPictureData(const void* data, int /*flag*/) {
    const auto* picture = static_cast<const MmpHeader*>(data);
    if (!picture) return game::FatalError(game::Text(0x0078EB10));                  // "Can't load picture from NULL pointer."
    if (picture->format != kFormat8888) return game::FatalError(game::Text(0x0078EAE4));   // "... from non-8888 format."
    if (!render::SurfaceMemoryOf(this)) {
        std::uint32_t desc[0x1F] = {};
        desc[0] = 0x7C;
        // The screen's pixel format. Without a owner the original reads through a null pointer
        // here; 004EFC60 always passes one.
        gameapi::EnterCriticalSection(DeviceLock());
        ReadScreenFormat(this->device, &desc[kFormat]);
        gameapi::LeaveCriticalSection(DeviceLock());
        // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH: the format (the primary's) is not passed.
        desc[kFlags] = 7;
        desc[kWidth] = picture->width;
        desc[kHeight] = picture->height;
        // (The low byte of the flag asked for video memory.)
        desc[kCapsWord] = kCaps;
        // (The original's "no DXDD" report for a this without a owner is unreachable after the
        // read above, as is its release of an earlier surface: there is none on this path.)
        if (!AttachClientMemory(this, desc)) {
            this->device->lastLoadResult_ = kSurfaceRefused;
            // "Can't create newSurface to load picture %d:%d:%d."
            return game::FatalError(game::Text(0x0078EAB4), picture->width, picture->height, picture->bitsPerPixel);
        }
        desc[kFlags] |= kPixelFormatFlag;     // the DIB's format, the one DirectDraw would pick
        this->device->lastLoadResult_ = 0;    // the memory is the surface
        width = static_cast<int>(desc[kWidth]);
        height = static_cast<int>(desc[kHeight]);
        bitsPerPixel = static_cast<int>(desc[kBits]);
    } else if (picture->width != static_cast<std::uint32_t>(width) ||
               picture->width != static_cast<std::uint32_t>(height)) {
        return game::FatalError(game::Text(0x0078E99C));   // "Dimentions of the DD newSurface are differ from ..."
    }

    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    std::uint32_t format[8] = {};
    // (Always memory here: made above, or the dead loaders' holder.)
    if (!DirectPixels(this, desc)) return game::FatalError(game::Text(0x0078EA80));   // "Can't get newSurface pixel format to load picture."
    std::memcpy(format, &desc[kFormat], sizeof(format));
    gameapi::EnterCriticalSection(DeviceLock());
    FillPicture(this, picture, desc, format);
    gameapi::LeaveCriticalSection(DeviceLock());
    return 1;
}
EI_REPLACE_METHOD(0x004F0060, CDXSurface::LoadPictureData);


// 004EFC60 (thiscall, RET 0xC): a holder whose surface shows an .mmp picture (the software cursor's
// images 005E4EB0, windows made from a picture 005FCF80). The new renderer keeps the picture's
// pixels from the file data (render::PictureFromMmp) instead of reading the surface back. Throws
// the game's error object when the picture cannot be loaded.
CDXSurface* CDXSurface::ConstructFromPicture(CTextureDevice* deviceIn, const void* picture,
                                            int flag) {
    refCount = 0;
    result = 0;
    device = deviceIn;
    surface = nullptr;
    width = 0;
    height = 0;
    bitsPerPixel = 0;
    SetHolderVtable(this);
    if (!LoadPictureData(picture, flag)) {
        void* object = gamecrt::operator_new(0xC);
        if (object) object = GameErrorConstruct(static_cast<GameError*>(object), game::Text(0x0078E918));   // "Can't load picture to surface"
        libcrt::CxxThrowException(&object, GameErrorThrowInfo());
    }
    render::PictureFromMmp(this, picture);
    ++HolderCount();
    return this;
}
EI_REPLACE_METHOD(0x004EFC60, CDXSurface::ConstructFromPicture);

// 004EFD20. Releases the new renderer's memory (the original: the surface), clears the device,
// counts the holder out and leaves the base class's vtable.
void __fastcall DXSurfaceDestroy(CDXSurface* holder) {
    SetHolderVtable(holder);
    render::HolderGone(holder);   // its memory, and a picture kept for it
    holder->device = nullptr;
    --HolderCount();
    SetBaseVtable(holder);
}
EI_REPLACE(0x004EFD20, DXSurfaceDestroy);
