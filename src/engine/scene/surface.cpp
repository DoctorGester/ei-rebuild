// The surface holder (Surface, surface.h): a holder of the screen's pixel format, one from an .mmp picture, the texture
// holders and the destructor; the pixel formats and their channels. Used for the software cursor and the textures. No
// holder has a DirectDraw surface or memory: a picture's pixels are the renderer's copy of the file's pixels (render::PictureFromMmp), a texture's decoded from
// its file or drawn on the GPU. See docs/notes/renderer.md, sections 6 and 7.
#include "engine/scene/surface.h"

#include "engine/models/dib.h"
#include "engine/scene/device.h"
#include "engine/models/texture.h"
#include "platform/log.h"
#include "render/render.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/str.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

// The holder takes the picture's size and the screen's pixel size (the original filled memory of that size with the
// picture; the cursor builds its mask from the file's pixels instead). A missing picture, one of another format than
// A8R8G8B8 or one without pixels ends the game.
void LoadPicture(Surface* holder, const MmpFile& file) {
    if (!file.data) game::FatalExit("Can't load picture from NULL pointer.\n");
    const MmpHeader* picture = &file.header;
    if (picture->format != MmpFormat::A8R8G8B8) game::FatalExit("Can't load picture from non-8888 format.\n");
    const int width = static_cast<int>(picture->width), height = static_cast<int>(picture->height);
    if (width <= 0 || height <= 0)
        game::FatalExit("Can't create surface to load picture %d:%d:%d.\n", picture->width, picture->height,
                        picture->bitsPerPixel);
    holder->width = width;
    holder->height = height;
    holder->bitsPerPixel = ScreenFormat().bitsPerPixel;
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

render::PixelFormat ScreenFormat() { return render::PixelFormat{32, 0xFF0000, 0xFF00, 0xFF, 0}; }

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
    throw std::runtime_error(str::Format("Can't create DDSurface %d:%d with default BPP.\n", width, height));
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

// The renderer keeps the picture's pixels from the file data (render::PictureFromMmp).
Surface::Surface(TextureDevice* deviceIn, const MmpFile& picture) : device(deviceIn) {
    LoadPicture(this, picture);
    render::PictureFromMmp(this, picture);
}

// The new renderer forgets whatever it kept for the holder.
Surface::~Surface() {
    render::HolderGone(this);
    device = nullptr;
}

// --- Capture -----------------------------------------------------------------------------------------

// Reading the screen back into a bitmap (dib.h): the screenshots (Scroll Lock, ui_interface.cpp) and the movie
// recorder's frames (app/main_loop.cpp). The rows come from the new renderer's last frame, copied from the last to the
// first, as a .bmp keeps them.

// The bitmap is made from the new renderer's last frame (render::ReadFrame), in the screen's depth and channel masks
// (ScreenFormat), rows bottom-up, the part of the screen outside the frame black. Null (logged) when the new renderer
// has no frame.
Dib* CaptureScreen(const TextureDevice* device) {
    const std::optional<render::FrameImage> frame = render::ReadFrame();
    if (!frame) {
        static bool logged = false;
        if (!logged) ei::Log("capture: the new renderer has no frame: no bitmap");
        logged = true;
        return nullptr;
    }
    const render::PixelFormat format = ScreenFormat();
    const int width = device->screen.right, height = device->screen.bottom;
    auto* dib = new Dib(width, height, format.bitsPerPixel, format.redMask, format.greenMask, format.blueMask);
    std::uint8_t* out = dib->AllocateBits();
    if (!out || width <= 0 || height <= 0) return dib;
    const std::size_t rowBytes = DibRowBytes(*dib->header);
    for (int row = height - 1; row >= 0; --row, out += rowBytes) {
        std::memset(out, 0, rowBytes);
        if (row >= frame->height) continue;
        for (int x = 0; x < width && x < frame->width; ++x) {
            const std::uint8_t* bgr = &frame->bgr[(static_cast<std::size_t>(row) * frame->width + x) * 3];
            const std::uint32_t pixel = (static_cast<std::uint32_t>(bgr[2]) << 16) | (static_cast<std::uint32_t>(bgr[1]) << 8) | bgr[0];
            lib::Store(out + x * 4, pixel);
        }
    }
    return dib;
}
