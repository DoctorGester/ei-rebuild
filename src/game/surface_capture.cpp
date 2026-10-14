// Reading a surface back into a bitmap (006D6D40, 006D6FC0; dib.h): the screenshots (Scroll Lock,
// ui_interface_keys.cpp) and the movie recorder's frames (app_frame.cpp). The surface is locked
// for reading and its rows copied from the last to the first, as a .bmp keeps them.
#include "dib.h"

#include "app.h"
#include "device.h"
#include "texture.h"
#include "ui.h"

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

// The two captures through their addresses, so that their verify wrappers see the calls.
__declspec(naked) CDib* __fastcall CallCaptureSurfaceRect(CDXSurface*, const RECT*) EI_ORIGINAL(0x006D6D40)
__declspec(naked) CDib* __fastcall CallCaptureSurface(CDXSurface*) EI_ORIGINAL(0x006D6FC0)

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
const char* NoPixelFormatText() { return Text(0x0078E714); }   // "Can't get pixel format from surface.\n"
const char* CantLockText() { return Text(0x0078E30C); }        // "Can't lock surface.\n"
const char* CantUnlockText() { return Text(0x0078E2F4); }      // "Can't unlock surface.\n"

constexpr DWORD kLockReadWait = DDLOCK_WAIT | DDLOCK_READONLY;   // 0x11

// The bitmap's construction and block through their addresses (their verify wrappers).
__declspec(naked) CDib* __fastcall ConstructSizedAt(CDib*, void*, int /*width*/, int /*height*/, int /*bitCount*/,
                                                    std::uint32_t /*red*/, std::uint32_t /*green*/,
                                                    std::uint32_t /*blue*/) EI_ORIGINAL(0x006D4B30)
__declspec(naked) std::uint8_t* __fastcall AllocateBitsAt(CDib*) EI_ORIGINAL(0x006D6AB0)

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

// A holder without a DirectDraw surface (the switch-over's render target and primary, device.h):
// the bitmap is made from the new renderer's last frame (render::ReadFrame), in the depth and
// channel masks the surface would have had (device::ScreenFormat), rows bottom-up, the part of the
// rectangle outside the frame black. Null (logged) when the new renderer has no frame.
CDib* CaptureFromFrame(const CDXSurface* self, const RECT* rect) {
    int frameWidth = 0, frameHeight = 0;
    std::vector<std::uint8_t> frame;
    if (!render::ReadFrame(&frameWidth, &frameHeight, &frame)) {
        static bool logged = false;
        if (!logged) ei::Log("capture: the surface has no DirectDraw surface and the new renderer has no frame: no bitmap");
        logged = true;
        return nullptr;
    }
    DDPIXELFORMAT format;
    device::ScreenFormat(self->bitsPerPixel == 16 ? 16 : 32, &format);
    const int width = rect->right - rect->left, height = rect->bottom - rect->top;
    auto* dib = static_cast<CDib*>(gamecrt::operator_new(sizeof(CDib)));
    if (dib) {
        ConstructSizedAt(dib, nullptr, width, height, self->bitsPerPixel, format.dwRBitMask, format.dwGBitMask,
                         format.dwBBitMask);
    }
    std::uint8_t* out = AllocateBitsAt(dib);
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

} // namespace

// A failed DirectDraw call is reported and the capture goes on, as the original's.
CDib* __fastcall CaptureSurfaceRect(CDXSurface* self, const RECT* rect) {
    auto* surface = static_cast<IDirectDrawSurface7*>(self->surface);
    // A holder without a surface (the switch-over's render target, device.h): the new renderer's frame.
    if (!surface) return CaptureFromFrame(self, rect);
    DDPIXELFORMAT format;
    std::memset(&format, 0, sizeof(format));
    format.dwSize = sizeof(format);
    self->result = surface->GetPixelFormat(&format);
    if (self->result != DD_OK) app::FatalErrorCode(self->result, NoPixelFormatText());
    ChannelLayout layout;
    layout.Split(&format);   // the original's call; nothing reads the layout
    EnterCriticalSection(GameDeviceLock());
    DDSURFACEDESC2 desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    self->result = surface->Lock(const_cast<RECT*>(rect), &desc, kLockReadWait, nullptr);
    if (self->result != DD_OK) app::FatalErrorCode(self->result, CantLockText());
    auto* dib = static_cast<CDib*>(gamecrt::operator_new(sizeof(CDib)));
    if (dib) {
        ConstructSizedAt(dib, nullptr, rect->right - rect->left, rect->bottom - rect->top, self->bitsPerPixel,
                         format.dwRBitMask, format.dwGBitMask, format.dwBBitMask);
    }
    std::uint8_t* out = AllocateBitsAt(dib);
    if (const int pixelBytes = BytesPerPixel(self->bitsPerPixel)) {
        for (int row = rect->bottom - rect->top - 1; row >= 0; --row) {
            const std::size_t rowBytes = static_cast<std::size_t>((rect->right - rect->left) * pixelBytes);
            std::memcpy(out, static_cast<const std::uint8_t*>(desc.lpSurface) + desc.lPitch * row, rowBytes);
            out += rowBytes;
        }
    }
    LeaveCriticalSection(GameDeviceLock());
    self->result = surface->Unlock(const_cast<RECT*>(rect));
    if (self->result != DD_OK) app::FatalErrorCode(self->result, CantUnlockText());
    return dib;
}
EI_REPLACE(0x006D6D40, CaptureSurfaceRect);

CDib* __fastcall CaptureSurface(CDXSurface* self) {
    const RECT whole = {0, 0, self->width, self->height};
    return CallCaptureSurfaceRect(self, &whole);
}
EI_REPLACE(0x006D6FC0, CaptureSurface);
