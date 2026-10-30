// The surface holder (Surface, surface.h): a holder of the screen's pixel format, one from an .mmp picture, the texture
// holders and the destructor; the pixel formats and their channels. Used for the interface windows, the software
// cursor and the textures. No holder has a DirectDraw surface or memory: an interface window's pixels are its canvas
// on the GPU, a picture's the renderer's copy of the file's pixels (render::PictureFromMmp), a texture's decoded from
// its file or drawn on the GPU. See docs/notes/renderer.md, sections 6 and 7.
#include "surface.h"

#include "dib.h"
#include "dx_errors.h"
#include "texture.h"
#include "../hook/log.h"
#include "../render/render.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "render/render.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kFormat8888 = 0x8888;

// The holder takes the picture's size and the screen's pixel size (the original filled memory of that size with the
// picture; the cursor builds its mask from the file's pixels instead). 1 when done; otherwise the result of the fatal
// error report (nonzero when the player goes on).
int LoadPicture(Surface* holder, const MmpHeader* picture) {
    if (!picture) return game::FatalError("Can't load picture from NULL pointer.\n");
    if (picture->format != kFormat8888) return game::FatalError("Can't load picture from non-8888 format.\n");
    const int width = static_cast<int>(picture->width), height = static_cast<int>(picture->height);
    if (width <= 0 || height <= 0)
        return game::FatalError("Can't create surface to load picture %d:%d:%d.\n", picture->width, picture->height,
                                picture->bitsPerPixel);
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = ScreenFormat(holder->device->bitsPerPixel).bitsPerPixel;
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

render::PixelFormat ScreenFormat(int bitsPerPixel) {
    if (bitsPerPixel == 16) return render::PixelFormat{16, 0xF800, 0x07E0, 0x001F, 0};
    return render::PixelFormat{32, 0xFF0000, 0xFF00, 0xFF, 0};
}

bool ChannelLayout::Split(const render::PixelFormat& format) {
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

// ---- The holders ------------------------------------------------------------------------------------------

void ThrowSurfaceNotMade(int width, int height) {
    ThrowGameError("Can't create DDSurface %d:%d with default BPP.\n", width, height);
}

Surface* NewDecodedTextureHolder(TextureDevice* device, int width, int height, int bitsPerPixel) {
    if (!device) return nullptr;
    auto* holder = new Surface(device);
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = bitsPerPixel;
    return holder;
}

Surface::Surface(TextureDevice* deviceIn) : device(deviceIn) {}

Surface::Surface(TextureDevice* deviceIn, int widthIn, int heightIn) : device(deviceIn) {
    if (widthIn <= 0 || heightIn <= 0) ThrowSurfaceNotMade(widthIn, heightIn);
    width = widthIn;
    height = heightIn;
    bitsPerPixel = ScreenFormat(device->bitsPerPixel).bitsPerPixel;
}

// The renderer keeps the picture's pixels from the file data (render::PictureFromMmp).
Surface::Surface(TextureDevice* deviceIn, const MmpHeader* picture) : device(deviceIn) {
    if (!LoadPicture(this, picture)) ThrowGameError("Can't load picture to surface.\n");
    render::PictureFromMmp(this, picture);
}

// The new renderer forgets whatever it kept for the holder.
Surface::~Surface() {
    render::HolderGone(this);
    device = nullptr;
}

// --- Capture -----------------------------------------------------------------------------------------

// Reading the screen back into a bitmap (dib.h): the screenshots (Scroll Lock, ui_interface.cpp) and the movie
// recorder's frames (app_frame.cpp). The rows come from the new renderer's last frame, copied from the last to the
// first, as a .bmp keeps them.

namespace {

// The pixel sizes the capture copies; a surface of any other depth gives a bitmap with its bits
// left as allocated.
int BytesPerPixel(int bits) {
    switch (bits) {
    case 16: return 2;
    case 24: return 3;
    case 32: return 4;
    default: return 0;
    }
}

} // namespace

// The bitmap is made from the new renderer's last frame (render::ReadFrame), in the screen's depth and channel masks
// (ScreenFormat), rows bottom-up, the part of the screen outside the frame black. Null (logged) when the new renderer
// has no frame.
Dib* CaptureScreen(const TextureDevice* device) {
    int frameWidth = 0, frameHeight = 0;
    std::vector<std::uint8_t> frame;
    if (!render::ReadFrame(&frameWidth, &frameHeight, &frame)) {
        static bool logged = false;
        if (!logged) ei::Log("capture: the new renderer has no frame: no bitmap");
        logged = true;
        return nullptr;
    }
    const int bitsPerPixel = device->bitsPerPixel;
    const render::PixelFormat format = ScreenFormat(bitsPerPixel);
    const int width = device->screen.right, height = device->screen.bottom;
    auto* dib = new Dib(width, height, bitsPerPixel, format.redMask, format.greenMask, format.blueMask);
    std::uint8_t* out = dib->AllocateBits();
    const int pixelBytes = BytesPerPixel(bitsPerPixel);
    if (!out || (pixelBytes != 2 && pixelBytes != 4) || width <= 0 || height <= 0) return dib;
    const std::size_t rowBytes = DibRowBytes(dib->header);   // padded to four bytes (a 16-bit row of odd width)
    for (int row = height - 1; row >= 0; --row, out += rowBytes) {
        std::memset(out, 0, rowBytes);
        if (row >= frameHeight) continue;
        for (int x = 0; x < width && x < frameWidth; ++x) {
            const std::uint8_t* bgr = &frame[(static_cast<std::size_t>(row) * frameWidth + x) * 3];
            if (pixelBytes == 2) {
                const std::uint16_t pixel = static_cast<std::uint16_t>(((bgr[2] >> 3) << 11) | ((bgr[1] >> 2) << 5) | (bgr[0] >> 3));
                std::memcpy(out + x * 2, &pixel, 2);
            } else {
                const std::uint32_t pixel = (static_cast<std::uint32_t>(bgr[2]) << 16) | (static_cast<std::uint32_t>(bgr[1]) << 8) | bgr[0];
                lib::StoreBytes(out + x * 4, pixel);
            }
        }
    }
    return dib;
}
