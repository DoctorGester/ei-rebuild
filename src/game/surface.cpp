// The surface holder (Surface, surface.h): a holder of the screen's pixel format, one from an .mmp picture, the texture
// holders and the destructor; the pixel formats and their channels. Used for the interface windows, the software
// cursor, the movies and the textures. No holder has a DirectDraw surface: the pixels live in memory the new renderer
// owns (render::NewSurfaceMemory, a DIB section), which game code works on directly (DirectPixels); a texture has none
// (decoded from its file or drawn on the GPU). See docs/notes/renderer.md, sections 6 and 7.
#include "surface.h"

#include "dx_errors.h"
#include "texture.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <cstring>
#include "game_singletons.h"

namespace {

// Memory of the new renderer's for `holder`, `width` x `height` pixels in `format` (16 or 32-bit RGB). False when
// there is none.
bool AttachMemory(const Surface* holder, int width, int height, const PixelFormat& format) {
    if (format.bitsPerPixel != 16 && format.bitsPerPixel != 32) return false;
    return render::NewSurfaceMemory(holder, width, height, format.bitsPerPixel, format.redMask, format.greenMask,
                                    format.blueMask, format.alphaMask) != nullptr;
}

// The screen's pixel format of the holder's device.
PixelFormat ScreenFormatOf(const Surface* holder) { return ScreenFormat(holder->device->bitsPerPixel_); }

constexpr std::uint32_t kFormat8888 = 0x8888;

// The picture's pixels after the .mmp header and its extra block.
const std::uint8_t* PicturePixels(const MmpHeader* picture) {
    return reinterpret_cast<const std::uint8_t*>(picture) + sizeof(MmpHeader) + picture->extraSize;
}

// The picture's pixels into the holder's memory `pixels` (16 or 32 bits): 32 bits row by row as they are; 16 bits each
// 8888 pixel cut to the format's channels (each channel's top bits at its place).
void FillPicture(const MmpHeader* picture, const SurfacePixels& pixels) {
    const std::uint8_t* source = PicturePixels(picture);
    std::uint8_t* out = pixels.bits;
    if (pixels.format.bitsPerPixel == 32) {
        for (std::uint32_t row = 0; row < picture->height; ++row) {
            std::memcpy(out, source, picture->width * 4);
            out += pixels.pitch;
            source += picture->width * 4;
        }
        return;
    }
    ChannelLayout layout;
    layout.Split(pixels.format);
    for (std::uint32_t row = 0; row < picture->height; ++row) {
        for (std::uint32_t x = 0; x < picture->width; ++x) {
            const std::uint8_t* in = source + x * 4;
            const std::uint32_t argb = in[0] | (in[1] << 8) | (in[2] << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
            const auto native = static_cast<std::uint16_t>(layout.ToNative(argb));
            out[x * 2] = static_cast<std::uint8_t>(native);
            out[x * 2 + 1] = static_cast<std::uint8_t>(native >> 8);
        }
        out += pixels.pitch;
        source += picture->width * 4;
    }
}

// The holder gets memory of the picture's size in the screen's pixel format with the picture in it. 1 when done;
// otherwise the result of the fatal error report (nonzero when the player goes on).
int LoadPicture(Surface* holder, const MmpHeader* picture) {
    if (!picture) return game::FatalError("Can't load picture from NULL pointer.\n");
    if (picture->format != kFormat8888) return game::FatalError("Can't load picture from non-8888 format.\n");
    const PixelFormat format = ScreenFormatOf(holder);
    const int width = static_cast<int>(picture->width), height = static_cast<int>(picture->height);
    if (!AttachMemory(holder, width, height, format))
        return game::FatalError("Can't create surface to load picture %d:%d:%d.\n", picture->width, picture->height,
                                picture->bitsPerPixel);
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = format.bitsPerPixel;
    SurfacePixels pixels;
    DirectPixels(holder, &pixels);
    gameapi::EnterCriticalSection(&g_deviceLock);
    FillPicture(picture, pixels);
    gameapi::LeaveCriticalSection(&g_deviceLock);
    return 1;
}

// One channel mask: the shift of its lowest set bit and the length of the run of set bits from there (a zero mask: 0
// and 0). False when the run is longer than 8 bits.
bool SplitMask(std::uint32_t mask, std::uint32_t* bits, std::uint32_t* shift) {
    *bits = 0;
    *shift = 0;
    if (mask == 0) return true;
    std::uint32_t bit = 1;
    while (!(mask & bit)) {
        bit <<= 1;
        ++*shift;
    }
    while (mask & bit) {
        bit <<= 1;
        ++*bits;
    }
    return *bits <= 8;
}

// SHR / SHL by CL: the count is taken modulo 32.
std::uint32_t Shr(std::uint32_t value, std::uint32_t count) { return value >> (count & 31); }
std::uint32_t Shl(std::uint32_t value, std::uint32_t count) { return value << (count & 31); }

} // namespace

// ---- Pixel formats ------------------------------------------------------------------------------------

PixelFormat ScreenFormat(int bitsPerPixel) {
    if (bitsPerPixel == 16) return PixelFormat{16, 0xF800, 0x07E0, 0x001F, 0};
    return PixelFormat{32, 0xFF0000, 0xFF00, 0xFF, 0};
}

bool ChannelLayout::Split(const PixelFormat& format) {
    *this = {};
    if (!format.bitsPerPixel) {
        game::DebugLog("Format have no RGB information.\n");
        return false;
    }
    std::uint32_t bits, shift;
    // Each channel is stored only when it passes, so a failure leaves it and the later ones zero.
    if (!SplitMask(format.alphaMask, &bits, &shift)) return false;
    alphaMask = format.alphaMask;
    alphaBits = bits;
    alphaShift = shift;
    if (!SplitMask(format.redMask, &bits, &shift)) return false;
    redMask = format.redMask;
    redBits = bits;
    redShift = shift;
    if (!SplitMask(format.greenMask, &bits, &shift)) return false;
    greenMask = format.greenMask;
    greenBits = bits;
    greenShift = shift;
    if (!SplitMask(format.blueMask, &bits, &shift)) return false;
    blueMask = format.blueMask;
    blueBits = bits;
    blueShift = shift;
    return true;
}

std::uint32_t ChannelLayout::ToNative(std::uint32_t argb) const {
    auto channel = [](std::uint32_t value, std::uint32_t bits, std::uint32_t shift) {
        return Shl(Shr(value, 8 - bits), shift);
    };
    return channel(argb >> 24, alphaBits, alphaShift) | channel((argb >> 8) & 0xFF, greenBits, greenShift) |
           channel((argb >> 16) & 0xFF, redBits, redShift) | channel(argb & 0xFF, blueBits, blueShift);
}

std::uint32_t ChannelLayout::FromNative(std::uint32_t pixel) const {
    auto channel = [pixel](std::uint32_t mask, std::uint32_t bits, std::uint32_t shift) {
        return Shl(Shr(pixel & mask, shift), 8 - bits);
    };
    // Each step shifts everything so far up a byte and ORs the next channel in (a channel wider
    // than 8 bits would spill into its neighbour; Split refuses those).
    std::uint32_t argb = channel(alphaMask, alphaBits, alphaShift);
    argb = (argb << 8) | channel(redMask, redBits, redShift);
    argb = (argb << 8) | channel(greenMask, greenBits, greenShift);
    argb = (argb << 8) | channel(blueMask, blueBits, blueShift);
    return argb;
}

// ---- The renderer's memory ------------------------------------------------------------------------------

void ThrowSurfaceNotMade(int width, int height) {
    ThrowGameError("Can't create DDSurface %d:%d with default BPP.\n", width, height);
}

HBITMAP ClientBitmap(const void* holder) {
    const render::SurfaceMemory* memory = render::SurfaceMemoryOf(holder);
    return memory ? static_cast<HBITMAP>(memory->bitmap) : nullptr;
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
    pixels->format = PixelFormat{m->bitsPerPixel, m->redMask, m->greenMask, m->blueMask, m->alphaMask};
    return true;
}

// ---- The holders ------------------------------------------------------------------------------------------

Surface* NewDecodedTextureHolder(CTextureDevice* device, int width, int height, int bitsPerPixel) {
    if (!device) return nullptr;
    auto* holder = new Surface(device);
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = bitsPerPixel;
    return holder;
}

Surface::Surface(CTextureDevice* deviceIn) : device(deviceIn) {}

Surface::Surface(CTextureDevice* deviceIn, int widthIn, int heightIn) : device(deviceIn) {
    const PixelFormat format = ScreenFormatOf(this);
    if (!AttachMemory(this, widthIn, heightIn, format)) ThrowSurfaceNotMade(widthIn, heightIn);
    width = widthIn;
    height = heightIn;
    bitsPerPixel = format.bitsPerPixel;
}

// The renderer keeps the picture's pixels from the file data (render::PictureFromMmp) instead of reading the memory
// back.
Surface::Surface(CTextureDevice* deviceIn, const MmpHeader* picture) : device(deviceIn) {
    if (!LoadPicture(this, picture)) ThrowGameError("Can't load picture to surface.\n");
    render::PictureFromMmp(this, picture);
}

// Releases the new renderer's memory and whatever it kept for the holder.
Surface::~Surface() {
    render::HolderGone(this);
    device = nullptr;
}
