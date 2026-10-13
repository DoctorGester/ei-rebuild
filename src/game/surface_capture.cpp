// Reading a surface back into a bitmap (006D6D40, 006D6FC0; dib.h): the screenshots (Scroll Lock,
// ui_interface_keys.cpp) and the movie recorder's frames (app_frame.cpp). The surface is locked
// for reading and its rows copied from the last to the first, as a .bmp keeps them.
#include "dib.h"

#include "app.h"
#include "texture.h"
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <ddraw.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

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

} // namespace

// A failed DirectDraw call is reported and the capture goes on, as the original's.
CDib* __fastcall CaptureSurfaceRect(CDXSurface* self, const RECT* rect) {
    auto* surface = static_cast<IDirectDrawSurface7*>(self->surface);
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
