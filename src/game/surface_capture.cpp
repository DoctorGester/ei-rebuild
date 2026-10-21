// Reading a surface back into a bitmap (006D6D40, 006D6FC0; dib.h): the screenshots (Scroll Lock,
// ui_interface_keys.cpp) and the movie recorder's frames (app_frame.cpp). The original locks the
// surface for reading and copies its rows from the last to the first, as a .bmp keeps them; no
// holder has a DirectDraw surface, so the rows come from the new renderer's last frame.
#include "dib.h"

#include "device.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include "../hook/log.h"
#include "../render/render.h"

#include <ddraw.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

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

// The bitmap is made from the new renderer's last frame (render::ReadFrame), in the depth and
// channel masks the surface would have had (device::ScreenFormat), rows bottom-up, the part of the
// rectangle outside the frame black. Null (logged) when the new renderer has no frame.
CDib* __fastcall CaptureSurfaceRect(CDXSurface* self, const RECT* rect) {
    int frameWidth = 0, frameHeight = 0;
    std::vector<std::uint8_t> frame;
    if (!render::ReadFrame(&frameWidth, &frameHeight, &frame)) {
        static bool logged = false;
        if (!logged) ei::Log("capture: the new renderer has no frame: no bitmap");
        logged = true;
        return nullptr;
    }
    DDPIXELFORMAT format;
    device::ScreenFormat(self->bitsPerPixel == 16 ? 16 : 32, &format);
    const int width = rect->right - rect->left, height = rect->bottom - rect->top;
    auto* dib = static_cast<CDib*>(gamecrt::operator_new(sizeof(CDib)));
    if (dib) {
        dib->ConstructSized(width, height, self->bitsPerPixel, format.dwRBitMask, format.dwGBitMask,
                         format.dwBBitMask);
    }
    std::uint8_t* out = dib->AllocateBits();
    const int pixelBytes = BytesPerPixel(self->bitsPerPixel);
    if (!out || (pixelBytes != 2 && pixelBytes != 4) || width <= 0 || height <= 0) return dib;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * pixelBytes;
    for (int row = height - 1; row >= 0; --row, out += rowBytes) {
        std::memset(out, 0, rowBytes);
        const int y = rect->top + row;
        if (y < 0 || y >= frameHeight) continue;
        for (int x = 0; x < width; ++x) {
            const int fx = rect->left + x;
            if (fx < 0 || fx >= frameWidth) continue;
            const std::uint8_t* bgr = &frame[(static_cast<std::size_t>(y) * frameWidth + fx) * 3];
            if (pixelBytes == 2) {
                const std::uint16_t pixel = static_cast<std::uint16_t>(((bgr[2] >> 3) << 11) | ((bgr[1] >> 2) << 5) | (bgr[0] >> 3));
                std::memcpy(out + x * 2, &pixel, 2);
            } else {
                const std::uint32_t pixel = (static_cast<std::uint32_t>(bgr[2]) << 16) | (static_cast<std::uint32_t>(bgr[1]) << 8) | bgr[0];
                std::memcpy(out + x * 4, &pixel, 4);
            }
        }
    }
    return dib;
}
EI_REPLACE(0x006D6D40, CaptureSurfaceRect);

CDib* __fastcall CaptureSurface(CDXSurface* self) {
    const RECT whole = {0, 0, self->width, self->height};
    return CaptureSurfaceRect(self, &whole);
}
