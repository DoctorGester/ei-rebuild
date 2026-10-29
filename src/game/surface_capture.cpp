// Reading the screen back into a bitmap (dib.h): the screenshots (Scroll Lock, ui_interface_keys.cpp) and the movie
// recorder's frames (app_frame.cpp). The rows come from the new renderer's last frame, copied from the last to the
// first, as a .bmp keeps them.
#include "dib.h"

#include "texture.h"

#include "lib/bytes.h"
#include "lib/crt.h"

#include "../hook/log.h"
#include "../render/render.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

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
