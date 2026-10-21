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
#include "game_data.h"
#include "game_singletons.h"

namespace {

int& HolderCount() { return ei::GameData<int>(0x007AEE94); }
// The holder's vtable and its base class's.
void SetHolderVtable(CDXSurface* holder) { ei::SetVtable(holder, 0x0073D774); }
void SetBaseVtable(CDXSurface* holder) { ei::SetVtable(holder, 0x0073D768); }

// 004800E0 / 00480080 (cdecl): DirectX error / fatal error reports (the second printf-like);
// nonzero to go on.
// The thrown error object: operator new (007005AF), its constructor (GameErrorConstruct, 00480250,
// cdecl: object, format, width, height) and _CxxThrowException with its throw info (originals_engine.h).

// A DDPIXELFORMAT as dwords (PixelFormat): size, flags, FourCC, bits, R, G, B, A masks.
enum : int { kFormatSize = 0, kFormatFlags = 1, kBits = 3, kRed = 4, kGreen = 5, kBlue = 6, kAlpha = 7 };
constexpr std::uint32_t kAlphaPixels = 0x1, kRgb = 0x40;   // its flags

// --- The new renderer's memory -----------------------------------------------------------------------

int g_clientFailures = 0;

// Memory of the new renderer's for `holder`, `width` x `height` pixels in `format` (16 or 32-bit
// RGB). False when there is none.
bool AttachClientMemory(const CDXSurface* holder, int width, int height, const PixelFormat& format) {
    const std::uint32_t* f = format.words;
    std::uint32_t bits = f[kBits];
    if ((bits != 16 && bits != 32) || !(f[kFormatFlags] & kRgb)) return false;
    std::uint32_t alpha = (f[kFormatFlags] & kAlphaPixels) ? f[kAlpha] : 0;
    return render::NewSurfaceMemory(holder, width, height, static_cast<int>(bits), f[kRed], f[kGreen], f[kBlue],
                                    alpha) != nullptr;
}

// The format of `pixelFormat` (a DDPIXELFORMAT as dwords).
PixelFormat FormatOf(const std::uint32_t* pixelFormat) {
    PixelFormat format;
    std::memcpy(format.words, pixelFormat, sizeof(format.words));
    return format;
}

// What DirectDraw's CreateSurface answered for a surface it refused (DDERR_UNSUPPORTED, the
// stand-in's answer before R11b): the device's result when a holder's memory cannot be made.
constexpr long kSurfaceRefused = static_cast<long>(0x80004001);

[[noreturn]] void ThrowCreateFailed(int width, int height) {
    void* object = gamecrt::operator_new(0xC);
    if (object) object = GameErrorConstruct(static_cast<GameError*>(object), "Can't create DDSurface %d:%d with default BPP.\n", width, height);   // "Can't create DDSurface %d %d ..."
    libcrt::CxxThrowException(&object, GameErrorThrowInfo());
    for (;;) {
    }
}

// The display mode's pixel format into `format` (IDirectDraw7::GetDisplayMode). False when
// DirectDraw does not give an RGB one.
bool ReadDisplayModeFormat(CTextureDevice* device, PixelFormat* format) {
    if (!device->ddraw_) return false;
    DDSURFACEDESC2 mode = {};
    mode.dwSize = sizeof(mode);
    if (device::GetDisplayMode(device::DDraw(device), &mode) != 0 || !(mode.ddpfPixelFormat.dwFlags & kRgb))
        return false;
    std::memcpy(format->words, &mode.ddpfPixelFormat, sizeof(format->words));
    return true;
}

// The screen's pixel format into `format`: the display mode's (ticket R8: the original reads the
// primary surface's, which is in the display mode's format; the switch-over always answers with an RGB
// one). Otherwise the original's report (`format` stays as it was).
void ReadScreenFormat(CTextureDevice* device, PixelFormat* format) {
    if (ReadDisplayModeFormat(device, format)) return;
    app::FatalErrorCode(kSurfaceRefused, "Can't get pixel format from surface.\n");   // "Can't get pixel format from surface"
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

PixelFormat SurfacePixels::Format() const {
    PixelFormat format = {};
    if (!bitsPerPixel) return format;
    std::uint32_t* f = format.words;
    f[kFormatSize] = sizeof(format);
    f[kFormatFlags] = kRgb | (alphaMask ? kAlphaPixels : 0);
    f[kBits] = static_cast<std::uint32_t>(bitsPerPixel);
    f[kRed] = redMask;
    f[kGreen] = greenMask;
    f[kBlue] = blueMask;
    f[kAlpha] = alphaMask;
    return format;
}

bool DirectPixels(const void* holder, SurfacePixels* pixels, bool reads) {
    *pixels = {};
    const render::SurfaceMemory* m = holder ? render::SurfaceMemoryOf(holder) : nullptr;
    if (!m) return false;
    ::GdiFlush();   // GDI may still hold drawing for the memory (not imported by the game)
    if (!reads) render::SurfaceWritten(holder, nullptr);
    pixels->width = m->width;
    pixels->height = m->height;
    pixels->pitch = m->pitch;
    pixels->bits = static_cast<std::uint8_t*>(m->bits);
    pixels->bitsPerPixel = m->bitsPerPixel;
    pixels->redMask = m->redMask;
    pixels->greenMask = m->greenMask;
    pixels->blueMask = m->blueMask;
    pixels->alphaMask = m->alphaMask;
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
    FinishTextureHolder(holder, width, height, static_cast<int>(pixelFormat[kBits]));
    return holder;
}

CDXSurface* NewMemoryTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat) {
    if (!device) return nullptr;
    CDXSurface* holder = StartTextureHolder(device);
    if (!holder) return nullptr;
    if (!AttachClientMemory(holder, width, height, FormatOf(pixelFormat))) {
        if (g_clientFailures++ < 5)
            ei::Log("cpuwindows: no memory for a %dx%d texture (format flags %08X, %u bits)", width, height,
                    pixelFormat[kFormatFlags], pixelFormat[kBits]);
        gamecrt::operator_delete(holder);
        return nullptr;
    }
    // The memory is the surface.
    FinishTextureHolder(holder, width, height, static_cast<int>(pixelFormat[kBits]));
    return holder;
}

// 004EF7B0. Returns the holder; when the surface cannot be made a C++ exception is thrown (as the
// original: an error object by pointer). The holder gets the new renderer's memory and no
// DirectDraw surface; without that memory the error ends the game.
CDXSurface* CDXSurface::Construct(CTextureDevice* deviceIn, int widthIn, int heightIn) {
    refCount = 0;
    result = 0;
    this->device = deviceIn;
    this->surface = nullptr;
    width = 0;
    height = 0;
    bitsPerPixel = 0;
    SetHolderVtable(this);
    PixelFormat format = {};
    ReadScreenFormat(deviceIn, &format);

    if (!this->device) {
        // "... Can't create DDSurface - no DXDD." (a report that says go on: the original reads the
        // missing surface's description and fails there; every caller passes the device object)
        game::FatalError("CDXDirectDrawSurface::CreateDDSurface.\n Can't create DDSurface - no DXDD.\n");
        ThrowCreateFailed(widthIn, heightIn);
    }
    if (!AttachClientMemory(this, widthIn, heightIn, format)) ThrowSurfaceNotMade(this->device, widthIn, heightIn);
    // The memory is the surface: described by the request, the creation counted as done.
    this->device->lastLoadResult_ = 0;
    width = widthIn;
    height = heightIn;
    bitsPerPixel = static_cast<int>(format.words[kBits]);
    ++HolderCount();
    return this;
}
EI_REWRITTEN(0x004EF7B0, CDXSurface::Construct);

namespace {

constexpr std::uint32_t kFormat8888 = 0x8888;

// The picture's pixels after the .mmp header and its extra block.
const std::uint8_t* PicturePixels(const MmpHeader* picture) {
    return reinterpret_cast<const std::uint8_t*>(picture) + sizeof(MmpHeader) + picture->extraSize;
}

// The picture's pixels into the holder's memory `pixels`. The device's pixel converter (2DintMMX's
// graphics processor): slot 1 takes the target format (0 5:6:5, 1 5:5:5, 2 1:5:5:5, -1 another)
// and its alpha, red, green and blue masks; slot 2 converts one row of 8888 pixels (out, in,
// format, count).
void FillPicture(CDXSurface* holder, const MmpHeader* picture, const SurfacePixels& pixels) {
    const std::uint8_t* source = PicturePixels(picture);
    std::uint8_t* out = pixels.bits;
    const std::uint32_t bits = static_cast<std::uint32_t>(pixels.bitsPerPixel);
    if (bits == 32) {
        for (std::uint32_t row = 0; row < picture->height; ++row) {
            std::memcpy(out, source, picture->width * 4);
            out += pixels.pitch;
            source += picture->width * 4;
        }
    } else if (bits == 16) {
        ChannelLayout layout;
        const PixelFormat format = pixels.Format();
        layout.Split(&format);
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
            out += pixels.pitch;
            source += picture->width * 4;
        }
    } else if (bits == 24) {
        game::FatalError("Can't load picture to 24bit surface! Only 32bit and 16bit supported\n");   // "Can't load picture to 24bit surface! ..."
        // (The surface cleared as if it had no row padding, as the original.)
        std::memset(out, 0, (bits >> 3) * static_cast<std::uint32_t>(pixels.height * pixels.width));
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
    if (!picture) return game::FatalError("Can't load picture from NULL pointer.\n");                  // "Can't load picture from NULL pointer."
    if (picture->format != kFormat8888) return game::FatalError("Can't load picture from non-8888 format.\n");   // "... from non-8888 format."
    if (!render::SurfaceMemoryOf(this)) {
        // The screen's pixel format. Without a owner the original reads through a null pointer
        // here; 004EFC60 always passes one. (The low byte of the flag asked for video memory.)
        PixelFormat format = {};
        gameapi::EnterCriticalSection(TheDeviceLock());
        ReadScreenFormat(this->device, &format);
        gameapi::LeaveCriticalSection(TheDeviceLock());
        // (The original's "no DXDD" report for a this without a owner is unreachable after the
        // read above, as is its release of an earlier surface: there is none on this path.)
        const int pictureWidth = static_cast<int>(picture->width), pictureHeight = static_cast<int>(picture->height);
        if (!AttachClientMemory(this, pictureWidth, pictureHeight, format)) {
            this->device->lastLoadResult_ = kSurfaceRefused;
            // "Can't create newSurface to load picture %d:%d:%d."
            return game::FatalError("Can't create surface to load picture %d:%d:%d.\n", picture->width, picture->height, picture->bitsPerPixel);
        }
        this->device->lastLoadResult_ = 0;    // the memory is the surface
        width = pictureWidth;
        height = pictureHeight;
        bitsPerPixel = static_cast<int>(format.words[kBits]);
    } else if (picture->width != static_cast<std::uint32_t>(width) ||
               picture->width != static_cast<std::uint32_t>(height)) {
        return game::FatalError("Dimentions of the DD surface are differ from dimentions of the MMP file");   // "Dimentions of the DD newSurface are differ from ..."
    }

    SurfacePixels pixels;
    // (Always memory here: made above, or the dead loaders' holder.)
    if (!DirectPixels(this, &pixels)) return game::FatalError("Can't get surface pixel format to load picture.\n");   // "Can't get newSurface pixel format to load picture."
    gameapi::EnterCriticalSection(TheDeviceLock());
    FillPicture(this, picture, pixels);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    return 1;
}
EI_REWRITTEN(0x004F0060, CDXSurface::LoadPictureData);


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
        if (object) object = GameErrorConstruct(static_cast<GameError*>(object), "Can't load picture to surface.\n");   // "Can't load picture to surface"
        libcrt::CxxThrowException(&object, GameErrorThrowInfo());
    }
    render::PictureFromMmp(this, picture);
    ++HolderCount();
    return this;
}
EI_REWRITTEN(0x004EFC60, CDXSurface::ConstructFromPicture);

// 004EFD20. Releases the new renderer's memory (the original: the surface), clears the device,
// counts the holder out and leaves the base class's vtable.
void __fastcall DXSurfaceDestroy(CDXSurface* holder) {
    SetHolderVtable(holder);
    render::HolderGone(holder);   // its memory, and a picture kept for it
    holder->device = nullptr;
    --HolderCount();
    SetBaseVtable(holder);
}
EI_REWRITTEN(0x004EFD20, DXSurfaceDestroy);
