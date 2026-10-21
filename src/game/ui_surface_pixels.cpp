// The interface surfaces' pixel work (CUiSurface, ui.h): copying the screen into a surface
// (005FD3D0), a 16-bit 5:6:5 copy of a surface at a given size (005FD440, the save game's picture)
// and greying a surface in place (005FE440, the escape menu's background). The escape menu's
// drawing (005F5320, Rebirth-patched) runs all three when the menu opens. See docs/notes/ui.md, "Item
// descriptions, item view".
//
// These work on the surfaces' memory (the new renderer's; the holders have no DirectDraw surface)
// directly (DirectPixels): no Lock, the screen copy from the new renderer's frame, the stretched
// copy on the CPU. A holder without that memory (none is made) is left alone. A native surface
// (ui_native.h) gets the screen copy and the greying on its canvas too (under uinative instead of in its
// memory), and its canvas is read back before its memory is read or kept.
#include "app.h"
#include "ui.h"
#include "ui_native.h"
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
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace {

// What a surface's holder_ points at: a counted holder of the DirectDraw surface (texture.h).
using SurfaceHolder = CDXSurface;

SurfaceHolder* HolderOf(const CUiSurface* surface) { return surface->holder_; }



// Pixels of a surface's format go to and from 0xAARRGGBB through ChannelLayout (texture.h:
// Split 004EDAA0, FromNative 004EDC70, ToNative 004EDC00).
ChannelLayout LayoutOf(const SurfacePixels& pixels) {
    ChannelLayout layout;
    const PixelFormat format = pixels.Format();
    layout.Split(&format);
    return layout;
}

// SHL / SHR by CL (the count taken mod 32).
std::uint32_t Shl(std::uint32_t v, int count) { return v << (count & 31); }
std::uint32_t Shr(std::uint32_t v, int count) { return v >> (count & 31); }

// A pixel of `bytes` bytes, little-endian.
std::uint32_t ReadPixel(const std::uint8_t* at, int bytes) {
    std::uint32_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= Shl(at[i], i * 8);
    return value;
}

// `from` stretched into `to` (the same pixel format), nearest texel:
// destination pixel x takes the source pixel under its centre, (2 x + 1) * source width / (2 width).
// Windows' DirectDraw does exactly that at whole-number ratios (checked offline: 800x600 and
// 1280x720 to 160x120, 1920x1080 to 128x96); at other ratios it picks a neighbour now and then.
void StretchCopy(const SurfacePixels& to, const SurfacePixels& from) {
    int bytes = to.bitsPerPixel >> 3;
    std::uint32_t dw = static_cast<std::uint32_t>(to.width), dh = static_cast<std::uint32_t>(to.height);
    std::uint32_t sw = static_cast<std::uint32_t>(from.width), sh = static_cast<std::uint32_t>(from.height);
    if (dw == 0 || dh == 0 || from.bitsPerPixel != to.bitsPerPixel) return;
    for (std::uint32_t y = 0; y < dh; ++y) {
        const std::uint8_t* src = from.bits +
                                  static_cast<std::size_t>((2 * static_cast<std::uint64_t>(y) + 1) * sh / (2 * dh)) * from.pitch;
        std::uint8_t* out = to.bits + static_cast<std::size_t>(y) * to.pitch;
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
    if (!ui_native::Capture(this, rect_)) return;
    // The copy is taken from the new renderer's frame (the original's Blt from the render target is
    // gone with the DirectDraw fallback, R11a).
    render::CopyFrameTo(holder, render::Rect{rect_.left, rect_.top, rect_.right, rect_.bottom});
}

std::uint16_t* CUiSurface::Rgb565Copy(int width, int height) {
    ObservePixels(this, "Rgb565Copy", false, _AddressOfReturnAddress());
    if (!HolderOf(this)) return nullptr;
    ui_native::Sync(this);
    // A surface of the wanted size in this one's format, and this one stretched into it.
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    CUiSurface* copy = nullptr;
    if (memory) {
        int format = static_cast<int>(flags_);   // the format, as the constructors store it
        copy = static_cast<CUiSurface*>(memory)->Construct(interface_, 0, 0, width, height, format, 0);
    }
    SurfaceHolder* from = HolderOf(this);
    SurfaceHolder* to = HolderOf(copy);
    SurfacePixels fromPixels, toPixels;
    if (DirectPixels(from, &fromPixels, true) && DirectPixels(to, &toPixels)) StretchCopy(toPixels, fromPixels);

    int count = static_cast<int>(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height));
    auto* pixels = static_cast<std::uint16_t*>(gamecrt::operator_new(static_cast<std::size_t>(count) * 2));
    SurfaceHolder* holder = HolderOf(copy);
    gameapi::EnterCriticalSection(TheDeviceLock());
    SurfacePixels copied;
    bool direct = DirectPixels(holder, &copied, true);
    int bytes = copied.bitsPerPixel >> 3;
    const ChannelLayout converter = LayoutOf(copied);
    const std::uint8_t* source = copied.bits;
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
    gameapi::LeaveCriticalSection(TheDeviceLock());
    if (copy) {
        copy->ReleaseContents();
        gamecrt::operator_delete(copy);
    }
    return pixels;
}

void CUiSurface::Greyscale() {
    ObservePixels(this, "Greyscale", true, _AddressOfReturnAddress());
    if (!HolderOf(this)) return;
    if (!ui_native::Greyscale(this)) return;
    SurfaceHolder* holder = HolderOf(this);
    gameapi::EnterCriticalSection(TheDeviceLock());
    SurfacePixels desc;
    if (!DirectPixels(holder, &desc)) {
        gameapi::LeaveCriticalSection(TheDeviceLock());
        return;
    }
    std::uint32_t count = static_cast<std::uint32_t>(desc.width) * static_cast<std::uint32_t>(desc.height);
    std::uint32_t bits = static_cast<std::uint32_t>(desc.bitsPerPixel);
    std::uint32_t red = desc.redMask, green = desc.greenMask, blue = desc.blueMask;
    bool done = false;
    if (bits == 16 && red == 0xF800 && green == 0x7E0 && blue == 0x1F) {
        // 5:6:5: (2 r + 2 g6 + b) / 16 in every channel.
        auto* p = reinterpret_cast<std::uint16_t*>(desc.bits);
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
        auto* p = reinterpret_cast<std::uint16_t*>(desc.bits);
        if (count != 0) {
            do {
                std::uint32_t pixel = *p;
                std::uint32_t grey = (((pixel >> 3) & 0x7C) + ((pixel >> 9) & 0x3E) + (pixel & 0x1F)) >> 4;
                *p++ = static_cast<std::uint16_t>(((grey << 5 | grey) << 5) | grey);
            } while (--count != 0);
        }
        done = true;
    } else if (bits == 32 && red == 0xFF0000 && green == 0xFF00 && blue == 0xFF) {
        auto* p = reinterpret_cast<std::uint32_t*>(desc.bits);
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
        std::uint8_t* p = desc.bits;
        const ChannelLayout converter = LayoutOf(desc);
        int bytes = static_cast<int>(bits >> 3);
        if (count != 0) {
            do {
                std::uint32_t native = converter.ToNative(Grey32(converter.FromNative(ReadPixel(p, bytes))));
                for (int i = 0; i < bytes; ++i) p[i] = static_cast<std::uint8_t>(Shr(native, i * 8));
                p += bytes;
            } while (--count != 0);
        }
    }
    gameapi::LeaveCriticalSection(TheDeviceLock());
}

namespace {

// 004E6BA0 (thiscall): drops a holder reference.
void ReleaseHolder(SurfaceHolder* holder) { holder->Release(); }

std::uint32_t& KeptBytesTotal() { return ei::GameData<std::uint32_t>(0x007C7E74); }
std::uint32_t& SurfaceBytesTotal() { return ei::GameData<std::uint32_t>(0x007C7E78); }

} // namespace

void CUiSurface::SwapContents(int restore) {
    ObservePixels(this, restore ? "SwapContents(restore)" : "SwapContents(save)", restore != 0,
                  _AddressOfReturnAddress());
    SurfacePixels desc;
    if (restore) {
        if (!holder_) {
            // A new surface of the surface's size.
            void* memory = gamecrt::operator_new(0x20);
            SurfaceHolder* made = nullptr;
            if (memory) {
                made = reinterpret_cast<SurfaceHolder*>(static_cast<CDXSurface*>(memory)->Construct(
                    reinterpret_cast<CTextureDevice*>(interface_->renderer_), width_, height_));
            }
            SurfaceHolder* previous = holder_;
            holder_ = made;
            if (made) ++made->refCount;
            if (previous) ReleaseHolder(previous);
            DirectPixels(holder_, &desc, true);   // (no memory: the description stays empty)
            SurfaceBytesTotal() += static_cast<std::uint32_t>(desc.height) * static_cast<std::uint32_t>(desc.pitch);
        }
        if (!kept_) {
            ui_native::Restored(this);
            return;
        }
        SurfaceHolder* holder = holder_;
        gameapi::EnterCriticalSection(TheDeviceLock());
        if (DirectPixels(holder, &desc)) std::memcpy(desc.bits, kept_, keptSize_);
        KeptBytesTotal() -= keptSize_;
        gameapi::LeaveCriticalSection(TheDeviceLock());
        gamecrt::operator_delete(kept_);
        kept_ = nullptr;
        keptSize_ = 0;
        ui_native::Restored(this);
        return;
    }
    SurfaceHolder* holder = holder_;
    if (!holder) return;
    ui_native::Sync(this);
    gameapi::EnterCriticalSection(TheDeviceLock());
    bool direct = DirectPixels(holder, &desc, true);   // (no memory: an empty copy)
    if (kept_) gamecrt::operator_delete(kept_);
    keptSize_ = static_cast<std::uint32_t>(desc.height) * static_cast<std::uint32_t>(desc.pitch);
    kept_ = static_cast<std::uint8_t*>(gamecrt::operator_new(keptSize_));
    if (direct) std::memcpy(kept_, desc.bits, keptSize_);
    KeptBytesTotal() += keptSize_;
    gameapi::LeaveCriticalSection(TheDeviceLock());
    SurfaceBytesTotal() -= keptSize_;
    SurfaceHolder* released = holder_;
    holder_ = nullptr;
    if (released && --released->refCount == 0) {
        using DeleteFn = void(__thiscall*)(SurfaceHolder*, int);
        ei::VirtualSlot<DeleteFn>(static_cast<void**>(released->vtable), 0)(released, 1);
    }
}

// 005FD240 (thiscall, RET 8). The pixel at (x, y) of the surface as 0x00RRGGBB (through the
// format converter); 0 outside the surface or without one.
std::uint32_t CUiSurface::PixelAt(int x, int y) {
    ObservePixels(this, "PixelAt", false, _AddressOfReturnAddress());
    SurfaceHolder* holder = HolderOf(this);
    if (!holder || x < 0 || y < 0) return 0;
    if (x >= width_ || y >= height_) return 0;
    ui_native::Sync(this);
    gameapi::EnterCriticalSection(TheDeviceLock());
    SurfacePixels desc;
    if (!DirectPixels(holder, &desc, true)) {
        gameapi::LeaveCriticalSection(TheDeviceLock());
        return 0;
    }
    int bytes = desc.bitsPerPixel >> 3;
    const std::uint8_t* at = desc.bits + static_cast<std::size_t>(desc.pitch) * static_cast<std::uint32_t>(y) +
                             static_cast<std::size_t>(bytes) * static_cast<std::uint32_t>(x);
    std::uint32_t value = bytes > 0 ? ReadPixel(at, bytes) : 0;
    std::uint32_t rgb = LayoutOf(desc).FromNative(value);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    return rgb;
}

EI_REWRITTEN(0x005FD7A0, CUiSurface::SwapContents);
EI_REWRITTEN(0x005FD440, CUiSurface::Rgb565Copy);
