// The interface surfaces' pixel work (CUiSurface, ui.h): copying the screen into a surface
// (005FD3D0), a 16-bit 5:6:5 copy of a surface at a given size (005FD440, the save game's picture)
// and greying a surface in place (005FE440, the escape menu's background). The escape menu's
// drawing (005F5320, Rebirth-patched) runs all three when the menu opens. See docs/notes/ui.md, "Item
// descriptions, item view".
//
// These work on the surfaces' memory (the new renderer's; the holders have no DirectDraw surface)
// directly (DirectPixels): no Lock, the screen copy from the new renderer's frame, the stretched
// copy on the CPU. A holder without that memory (none is made) is left alone.
#include "app.h"
#include "ui.h"
#include "game_strings.h"

#include "device.h"   // device::SameSurface
#include "dx_surface.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_engine.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace {

// What a surface's holder_ points at: a counted holder of the DirectDraw surface (texture.h).
using SurfaceHolder = CDXSurface;

SurfaceHolder* HolderOf(const CUiSurface* surface) { return surface->holder_; }


CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// DDSURFACEDESC2 as dwords.
constexpr int kDescHeight = 2;      // +0x08
constexpr int kDescWidth = 3;       // +0x0C
constexpr int kDescPitch = 4;       // +0x10
constexpr int kDescSurface = 9;     // +0x24 lpSurface
constexpr int kDescFormat = 0x12;   // +0x48 DDPIXELFORMAT
constexpr int kDescBits = 0x15;     // +0x54 dwRGBBitCount
constexpr int kDescRed = 0x16;      // +0x58
constexpr int kDescGreen = 0x17;    // +0x5C
constexpr int kDescBlue = 0x18;     // +0x60

// Pixels of a DirectDraw format go to and from 0xAARRGGBB through ChannelLayout (texture.h:
// Split 004EDAA0, FromNative 004EDC70, ToNative 004EDC00).

// SHL / SHR by CL (the count taken mod 32).
std::uint32_t Shl(std::uint32_t v, int count) { return v << (count & 31); }
std::uint32_t Shr(std::uint32_t v, int count) { return v >> (count & 31); }

// A pixel of `bytes` bytes, little-endian.
std::uint32_t ReadPixel(const std::uint8_t* at, int bytes) {
    std::uint32_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= Shl(at[i], i * 8);
    return value;
}

// `from` stretched into `to` (both described as by a Lock, the same pixel format), nearest texel:
// destination pixel x takes the source pixel under its centre, (2 x + 1) * source width / (2 width).
// Windows' DirectDraw does exactly that at whole-number ratios (checked offline: 800x600 and
// 1280x720 to 160x120, 1920x1080 to 128x96); at other ratios it picks a neighbour now and then.
void StretchCopy(const std::uint32_t* to, const std::uint32_t* from) {
    int bytes = static_cast<int>(to[kDescBits] >> 3);
    std::uint32_t dw = to[kDescWidth], dh = to[kDescHeight], sw = from[kDescWidth], sh = from[kDescHeight];
    if (dw == 0 || dh == 0 || from[kDescBits] != to[kDescBits]) return;
    for (std::uint32_t y = 0; y < dh; ++y) {
        const auto* src = reinterpret_cast<const std::uint8_t*>(from[kDescSurface]) +
                          static_cast<std::size_t>((2 * static_cast<std::uint64_t>(y) + 1) * sh / (2 * dh)) * from[kDescPitch];
        auto* out = reinterpret_cast<std::uint8_t*>(to[kDescSurface]) + static_cast<std::size_t>(y) * to[kDescPitch];
        for (std::uint32_t x = 0; x < dw; ++x)
            std::memcpy(out + static_cast<std::size_t>(x) * bytes,
                        src + static_cast<std::size_t>((2 * static_cast<std::uint64_t>(x) + 1) * sw / (2 * dw)) * bytes,
                        static_cast<std::size_t>(bytes));
    }
}

// The grey of 0x00RRGGBB: (2 r + 4 g + b) / 16 in every channel (each weight on the 8-bit value).
std::uint32_t Grey32(std::uint32_t pixel) {
    std::uint32_t grey = (((pixel >> 6) & 0x3FC) + ((pixel >> 15) & 0x1FE) + (pixel & 0xFF)) >> 4;
    return ((grey << 8 | grey) << 8) | grey;
}

} // namespace

void (*g_uiSurfacePixelObserver)(const CUiSurface* surface, const char* what, bool writes, void* const* caller) = nullptr;

namespace {
// `caller`: the observed helper's _AddressOfReturnAddress().
void ObservePixels(const CUiSurface* surface, const char* what, bool writes, void* caller) {
    if (g_uiSurfacePixelObserver) g_uiSurfacePixelObserver(surface, what, writes, static_cast<void* const*>(caller));
}
} // namespace

void CUiSurface::CaptureScreen() {
    ObservePixels(this, "CaptureScreen", true, _AddressOfReturnAddress());
    SurfaceHolder* holder = HolderOf(this);
    if (!holder) return;
    SurfaceHolder* screen = holder->device->renderTarget_;
    if (device::SameSurface(screen, holder)) return;
    // The copy is taken from the new renderer's frame (the original's Blt from the render target is
    // gone with the DirectDraw fallback, R11a).
    render::CopyFrameTo(holder, render::Rect{rect_.left, rect_.top, rect_.right, rect_.bottom});
}

std::uint16_t* CUiSurface::Rgb565Copy(int width, int height) {
    ObservePixels(this, "Rgb565Copy", false, _AddressOfReturnAddress());
    if (!HolderOf(this)) return nullptr;
    // A surface of the wanted size in this one's format, and this one stretched into it.
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    CUiSurface* copy = nullptr;
    if (memory) {
        int format = static_cast<int>(flags_);   // the format, as the constructors store it
        copy = static_cast<CUiSurface*>(memory)->Construct(interface_, 0, 0, width, height, format, 0);
    }
    SurfaceHolder* from = HolderOf(this);
    SurfaceHolder* to = HolderOf(copy);
    std::uint32_t fromDesc[0x1F], toDesc[0x1F];
    if (DirectPixels(from, fromDesc) && DirectPixels(to, toDesc)) StretchCopy(toDesc, fromDesc);

    int count = static_cast<int>(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height));
    auto* pixels = static_cast<std::uint16_t*>(gamecrt::operator_new(static_cast<std::size_t>(count) * 2));
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    SurfaceHolder* holder = HolderOf(copy);
    gameapi::EnterCriticalSection(DeviceLock());
    std::memset(desc, 0, sizeof(desc));
    desc[0] = 0x7C;
    bool direct = DirectPixels(holder, desc);
    int bytes = static_cast<int>(desc[kDescBits] >> 3);
    ChannelLayout converter;
    converter.Split(&desc[kDescFormat]);
    const std::uint8_t* source = reinterpret_cast<const std::uint8_t*>(desc[kDescSurface]);
    std::uint16_t* out = pixels;
    if (direct && count != 0) {
        std::uint32_t left = static_cast<std::uint32_t>(count);
        do {
            std::uint32_t rgb = converter.FromNative(ReadPixel(source, bytes));
            std::uint32_t packed = ((((rgb >> 3) & 0x1F0000) | (rgb & 0xFC00)) >> 2 | (rgb & 0xF8)) >> 3;
            *out++ = static_cast<std::uint16_t>(packed);
            source += bytes;
        } while (--left != 0);
    }
    gameapi::LeaveCriticalSection(DeviceLock());
    if (copy) {
        copy->ReleaseContents();
        gamecrt::operator_delete(copy);
    }
    return pixels;
}

void CUiSurface::Greyscale() {
    ObservePixels(this, "Greyscale", true, _AddressOfReturnAddress());
    if (!HolderOf(this)) return;
    SurfaceHolder* holder = HolderOf(this);
    gameapi::EnterCriticalSection(DeviceLock());
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    if (!DirectPixels(holder, desc)) {
        gameapi::LeaveCriticalSection(DeviceLock());
        return;
    }
    std::uint32_t count = desc[kDescWidth] * desc[kDescHeight];
    std::uint32_t bits = desc[kDescBits];
    std::uint32_t red = desc[kDescRed], green = desc[kDescGreen], blue = desc[kDescBlue];
    bool done = false;
    if (bits == 16 && red == 0xF800 && green == 0x7E0 && blue == 0x1F) {
        // 5:6:5: (2 r + 2 g6 + b) / 16 in every channel.
        auto* p = reinterpret_cast<std::uint16_t*>(desc[kDescSurface]);
        if (count != 0) {
            do {
                std::uint32_t pixel = *p;
                std::uint32_t grey = (((pixel >> 4) & 0x7E) + ((pixel >> 10) & 0x3E) + (pixel & 0x1F)) >> 4;
                *p++ = static_cast<std::uint16_t>(((grey << 5 | grey) << 6) | grey);
            } while (--count != 0);
        }
        done = true;
    } else if (bits == 16 && red == 0x7C00 && green == 0x3E0 && blue == 0x1F) {
        // 5:5:5: (2 r + 4 g + b) / 16.
        auto* p = reinterpret_cast<std::uint16_t*>(desc[kDescSurface]);
        if (count != 0) {
            do {
                std::uint32_t pixel = *p;
                std::uint32_t grey = (((pixel >> 3) & 0x7C) + ((pixel >> 9) & 0x3E) + (pixel & 0x1F)) >> 4;
                *p++ = static_cast<std::uint16_t>(((grey << 5 | grey) << 5) | grey);
            } while (--count != 0);
        }
        done = true;
    } else if (bits == 32 && red == 0xFF0000 && green == 0xFF00 && blue == 0xFF) {
        auto* p = reinterpret_cast<std::uint32_t*>(desc[kDescSurface]);
        if (count != 0) {
            do {
                *p = Grey32(*p);
                ++p;
            } while (--count != 0);
        }
        done = true;
    }
    if (!done) {
        // Any other format through the converter, a pixel of bits / 8 bytes at a time.
        auto* p = reinterpret_cast<std::uint8_t*>(desc[kDescSurface]);
        ChannelLayout converter;
        converter.Split(&desc[kDescFormat]);
        int bytes = static_cast<int>(bits >> 3);
        if (count != 0) {
            do {
                std::uint32_t native = converter.ToNative(Grey32(converter.FromNative(ReadPixel(p, bytes))));
                for (int i = 0; i < bytes; ++i) p[i] = static_cast<std::uint8_t>(Shr(native, i * 8));
                p += bytes;
            } while (--count != 0);
        }
    }
    gameapi::LeaveCriticalSection(DeviceLock());
}

namespace {

// 004E6BA0 (thiscall): drops a holder reference.
void ReleaseHolder(SurfaceHolder* holder) { holder->Release(); }

std::uint32_t& KeptBytesTotal() { return *reinterpret_cast<std::uint32_t*>(0x007C7E74); }
std::uint32_t& SurfaceBytesTotal() { return *reinterpret_cast<std::uint32_t*>(0x007C7E78); }

} // namespace

void CUiSurface::SwapContents(int restore) {
    ObservePixels(this, restore ? "SwapContents(restore)" : "SwapContents(save)", restore != 0,
                  _AddressOfReturnAddress());
    std::uint32_t desc[0x1F];
    if (restore) {
        if (!holder_) {
            // A new surface of the surface's size.
            void* memory = gamecrt::operator_new(0x20);
            SurfaceHolder* made = nullptr;
            if (memory) {
                made = reinterpret_cast<SurfaceHolder*>(
                    DXSurfaceConstructOriginal(memory, nullptr, interface_->renderer_, width_, height_));
            }
            SurfaceHolder* previous = holder_;
            holder_ = made;
            if (made) ++made->refCount;
            if (previous) ReleaseHolder(previous);
            SurfaceHolder* holder = holder_;
            std::memset(desc, 0, sizeof(desc));
            desc[0] = 0x7C;
            DirectPixels(holder, desc);   // (no memory: the description stays empty)
            SurfaceBytesTotal() += desc[kDescHeight] * desc[kDescPitch];
        }
        if (!kept_) return;
        SurfaceHolder* holder = holder_;
        gameapi::EnterCriticalSection(DeviceLock());
        std::memset(desc, 0, sizeof(desc));
        desc[0] = 0x7C;
        if (DirectPixels(holder, desc)) std::memcpy(reinterpret_cast<void*>(desc[kDescSurface]), kept_, keptSize_);
        KeptBytesTotal() -= keptSize_;
        gameapi::LeaveCriticalSection(DeviceLock());
        gamecrt::operator_delete(kept_);
        kept_ = nullptr;
        keptSize_ = 0;
        return;
    }
    SurfaceHolder* holder = holder_;
    if (!holder) return;
    gameapi::EnterCriticalSection(DeviceLock());
    std::memset(desc, 0, sizeof(desc));
    desc[0] = 0x7C;
    bool direct = DirectPixels(holder, desc);   // (no memory: an empty copy)
    if (kept_) gamecrt::operator_delete(kept_);
    keptSize_ = desc[kDescHeight] * desc[kDescPitch];
    kept_ = static_cast<std::uint8_t*>(gamecrt::operator_new(keptSize_));
    if (direct) std::memcpy(kept_, reinterpret_cast<const void*>(desc[kDescSurface]), keptSize_);
    KeptBytesTotal() += keptSize_;
    gameapi::LeaveCriticalSection(DeviceLock());
    SurfaceBytesTotal() -= keptSize_;
    SurfaceHolder* released = holder_;
    holder_ = nullptr;
    if (released && --released->refCount == 0) {
        using DeleteFn = void(__thiscall*)(SurfaceHolder*, int);
        reinterpret_cast<DeleteFn>(*static_cast<void**>(released->vtable))(released, 1);
    }
}

// 005FD240 (thiscall, RET 8). The pixel at (x, y) of the surface as 0x00RRGGBB (through the
// format converter); 0 outside the surface or without one.
std::uint32_t CUiSurface::PixelAt(int x, int y) {
    ObservePixels(this, "PixelAt", false, _AddressOfReturnAddress());
    SurfaceHolder* holder = HolderOf(this);
    if (!holder || x < 0 || y < 0) return 0;
    if (x >= width_ || y >= height_) return 0;
    gameapi::EnterCriticalSection(DeviceLock());
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    if (!DirectPixels(holder, desc)) {
        gameapi::LeaveCriticalSection(DeviceLock());
        return 0;
    }
    int bytes = static_cast<int>(desc[kDescBits] >> 3);
    std::uint32_t at = desc[kDescSurface] + desc[kDescPitch] * static_cast<std::uint32_t>(y) +
                       static_cast<std::uint32_t>(bytes) * static_cast<std::uint32_t>(x);
    std::uint32_t value = bytes > 0 ? ReadPixel(reinterpret_cast<const std::uint8_t*>(at), bytes) : 0;
    ChannelLayout converter;
    converter.Split(&desc[kDescFormat]);
    std::uint32_t rgb = converter.FromNative(value);
    gameapi::LeaveCriticalSection(DeviceLock());
    return rgb;
}
EI_REPLACE_METHOD(0x005FD240, CUiSurface::PixelAt);

EI_REPLACE_METHOD(0x005FD7A0, CUiSurface::SwapContents);
EI_REPLACE_METHOD(0x005FD3D0, CUiSurface::CaptureScreen);
EI_REPLACE_METHOD(0x005FD440, CUiSurface::Rgb565Copy);
EI_REPLACE_METHOD(0x005FE440, CUiSurface::Greyscale);
