// The off-screen surface holder (CDXSurface): a system-memory surface of the screen's pixel format, one from an
// .mmp picture, and the destructor. Used for the interface windows, the software cursor and the movies. No
// holder has a DirectDraw surface: the pixels live in memory the new renderer owns (render::NewSurfaceMemory, a
// DIB section), which game code works on directly (DirectPixels). See docs/notes/renderer.md, sections 6 and 7.
#include "app.h"
#include "dx_surface.h"
#include "dx_errors.h"
#include "game_strings.h"
#include "device.h"
#include "texture.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include "../hook/log.h"

#include <cstdint>
#include <cstring>
#include "game_data.h"
#include "game_singletons.h"

namespace {

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

// What DirectDraw's CreateSurface answered for a surface it refused (DDERR_UNSUPPORTED): the device's result when a
// holder's memory cannot be made.
constexpr long kSurfaceRefused = static_cast<long>(0x80004001);

[[noreturn]] void ThrowCreateFailed(int width, int height) {
    ThrowGameError("Can't create DDSurface %d:%d with default BPP.\n", width, height);   // "Can't create DDSurface %d %d ..."
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

// The screen's pixel format into `format`: the display mode's (the primary surface's format is the display mode's;
// the switch-over always answers with an RGB one). Otherwise the failure report (`format` stays as it was).
void ReadScreenFormat(CTextureDevice* device, PixelFormat* format) {
    if (ReadDisplayModeFormat(device, format)) return;
    app::FatalErrorCode(kSurfaceRefused, "Can't get pixel format from surface.\n");   // "Can't get pixel format from surface"
}

// The holder stands for a texture made as requested: described by the request, the creation
// counted as done (the device's result 0, the holder count one higher).
void FinishTextureHolder(CDXSurface* holder, int width, int height, int bitsPerPixel) {
    holder->device->lastLoadResult_ = 0;
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = bitsPerPixel;
    ++device::g_holderCount;
}

constexpr std::uint32_t kFormat8888 = 0x8888;

// The picture's pixels after the .mmp header and its extra block.
const std::uint8_t* PicturePixels(const MmpHeader* picture) {
    return reinterpret_cast<const std::uint8_t*>(picture) + sizeof(MmpHeader) + picture->extraSize;
}

// The picture's pixels into the holder's memory `pixels`. The device's pixel converter (2DintMMX's graphics
// processor): slot 1 takes the target format (0 5:6:5, 1 5:5:5, 2 1:5:5:5, -1 another) and its alpha, red, green
// and blue masks; slot 2 converts one row of 8888 pixels (out, in, format, count).
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
        CPixelConverter* converter = holder->device->converter_;
        converter->SetChannelMasks(target, layout.alphaMask, layout.redMask, layout.greenMask, layout.blueMask);
        for (std::uint32_t row = 0; row < picture->height; ++row) {
            converter->ConvertTo16(reinterpret_cast<std::uint16_t*>(out), reinterpret_cast<const std::uint32_t*>(source), target,
                                   picture->width);
            out += pixels.pitch;
            source += picture->width * 4;
        }
    } else if (bits == 24) {
        game::FatalError("Can't load picture to 24bit surface! Only 32bit and 16bit supported\n");   // "Can't load picture to 24bit surface! ..."
        // (The surface is cleared as if it had no row padding.)
        std::memset(out, 0, (bits >> 3) * static_cast<std::uint32_t>(pixels.height * pixels.width));
    }
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

CDXSurface* NewDecodedTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat) {
    if (!device) return nullptr;
    auto* holder = new CDXSurface(device);
    if (!holder) return nullptr;
    // (A DDPIXELFORMAT: its bit count, as a surface of that format reports it.)
    FinishTextureHolder(holder, width, height, static_cast<int>(pixelFormat[kBits]));
    return holder;
}

CDXSurface* NewMemoryTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat) {
    if (!device) return nullptr;
    auto* holder = new CDXSurface(device);
    if (!holder) return nullptr;
    if (!AttachClientMemory(holder, width, height, FormatOf(pixelFormat))) {
        if (g_clientFailures++ < 5)
            ei::Log("cpuwindows: no memory for a %dx%d texture (format flags %08X, %u bits)", width, height,
                    pixelFormat[kFormatFlags], pixelFormat[kBits]);
        ::operator delete(holder);   // not counted in yet: no destructor
        return nullptr;
    }
    FinishTextureHolder(holder, width, height, static_cast<int>(pixelFormat[kBits]));
    return holder;
}

CDXSurface::CDXSurface(CTextureDevice* deviceIn) : device(deviceIn) {}

// When the surface cannot be made a C++ exception is thrown (an error object by pointer). The holder gets the new
// renderer's memory and no DirectDraw surface; without that memory the error ends the game.
CDXSurface::CDXSurface(CTextureDevice* deviceIn, int widthIn, int heightIn) : device(deviceIn) {
    PixelFormat format = {};
    ReadScreenFormat(deviceIn, &format);

    if (!this->device) {
        // "... Can't create DDSurface - no DXDD." (a report that says go on: the code reads the missing surface's
        // description and fails there; every caller passes the device object)
        game::FatalError("CDXDirectDrawSurface::CreateDDSurface.\n Can't create DDSurface - no DXDD.\n");
        ThrowCreateFailed(widthIn, heightIn);
    }
    if (!AttachClientMemory(this, widthIn, heightIn, format)) ThrowSurfaceNotMade(this->device, widthIn, heightIn);
    // The memory is the surface: described by the request, the creation counted as done.
    this->device->lastLoadResult_ = 0;
    width = widthIn;
    height = heightIn;
    bitsPerPixel = static_cast<int>(format.words[kBits]);
    ++device::g_holderCount;
}

// Makes the holder's memory (when it has none yet) of the size of an 8888 .mmp picture with the screen's pixel
// format (also for a video-memory request) and loads the picture into it, converted row by row through the
// device's converter for a 16-bit one. 1 when done; otherwise the result of the fatal error report (nonzero
// when the player goes on). Kept: a holder that already has its pixels must have the picture's width as both
// width and height; a 24-bit surface is reported, then cleared.
int CDXSurface::LoadPictureData(const MmpHeader* picture, int /*flag*/) {
    if (!picture) return game::FatalError("Can't load picture from NULL pointer.\n");
    if (picture->format != kFormat8888) return game::FatalError("Can't load picture from non-8888 format.\n");
    if (!render::SurfaceMemoryOf(this)) {
        // The screen's pixel format. Without an owner the game reads through a null pointer here; the callers always pass
        // one. (The low byte of the flag asked for video memory.)
        PixelFormat format = {};
        gameapi::EnterCriticalSection(&g_deviceLock);
        ReadScreenFormat(this->device, &format);
        gameapi::LeaveCriticalSection(&g_deviceLock);
        // (The "no DXDD" report for a this without an owner is unreachable after the read above, as is its release of an
        // earlier surface: there is none on this path.)
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
    gameapi::EnterCriticalSection(&g_deviceLock);
    FillPicture(this, picture, pixels);
    gameapi::LeaveCriticalSection(&g_deviceLock);
    return 1;
}

// A holder whose surface shows an .mmp picture (the software cursor's images, windows made from a picture). The new
// renderer keeps the picture's pixels from the file data (render::PictureFromMmp) instead of reading the surface
// back. Throws the game's error object when the picture cannot be loaded.
CDXSurface::CDXSurface(CTextureDevice* deviceIn, const MmpHeader* picture, int flag) : device(deviceIn) {
    if (!LoadPictureData(picture, flag)) ThrowGameError("Can't load picture to surface.\n");
    render::PictureFromMmp(this, picture);
    ++device::g_holderCount;
}

// Releases the new renderer's memory (the surface, in the game).
CDXSurface::~CDXSurface() {
    render::HolderGone(this);   // its memory, and a picture kept for it
    device = nullptr;
    --device::g_holderCount;
}
