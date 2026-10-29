// The interface surfaces' pixel work (CUiSurface): copying the screen into a surface, a 16-bit 5:6:5 copy of a surface at
// a given size (the save game's picture) and greying a surface in place (the escape menu's background), all three on the
// GPU (the surface's canvas, ui_native.h); keeping a surface's pixels aside and reading one pixel. See docs/notes/ui.md,
// "Surface pixel work".
#include "app.h"
#include "ui.h"
#include "ui_native.h"

#include "surface.h"
#include "texture.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace {

Surface* HolderOf(const CUiSurface* surface) { return surface->holder_; }



// Pixels of a surface's format go to and from 0xAARRGGBB through ChannelLayout (surface.h: Split,
// FromNative, ToNative).
ChannelLayout LayoutOf(const SurfacePixels& pixels) {
    ChannelLayout layout;
    layout.Split(pixels.format);
    return layout;
}

// SHL by CL (the count taken mod 32).
std::uint32_t Shl(std::uint32_t v, int count) { return v << (count & 31); }

// A pixel of `bytes` bytes, little-endian.
std::uint32_t ReadPixel(const std::uint8_t* at, int bytes) {
    std::uint32_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= Shl(at[i], i * 8);
    return value;
}

// `caller`: the observed helper's _AddressOfReturnAddress().
void ObservePixels(const CUiSurface* surface, const char* what, bool writes, void* caller) {
    if (g_uiSurfacePixelObserver) g_uiSurfacePixelObserver(surface, what, writes, static_cast<void* const*>(caller));
}

// Drops a holder reference.
void ReleaseHolder(Surface* holder) { holder->Release(); }

} // namespace

void (*g_uiSurfacePixelObserver)(const CUiSurface* surface, const char* what, bool writes, void* const* caller) = nullptr;

void CUiSurface::CaptureScreen() {
    ObservePixels(this, "CaptureScreen", true, _AddressOfReturnAddress());
    if (!HolderOf(this)) return;
    // The copy is drawn from the new renderer's frame into the surface's canvas, which keeps the surface's
    // pixels from now on (the original's Blt from the render target).
    ui_native::Capture(this, rect_);
}

// The original stretched the surface into a new surface of the wanted size, then packed it pixel by pixel;
// the GPU does both and only the picture is read back.
std::uint16_t* CUiSurface::Rgb565Copy(int width, int height) {
    ObservePixels(this, "Rgb565Copy", false, _AddressOfReturnAddress());
    if (!HolderOf(this)) return nullptr;
    int count = static_cast<int>(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height));
    auto* pixels = new std::uint16_t[static_cast<std::size_t>(count)]();
    ui_native::ReadRgb565(this, width, height, pixels);
    return pixels;
}

void CUiSurface::Greyscale() {
    ObservePixels(this, "Greyscale", true, _AddressOfReturnAddress());
    if (!HolderOf(this)) return;
    ui_native::Greyscale(this);
}

void CUiSurface::SwapContents(int restore) {
    ObservePixels(this, restore ? "SwapContents(restore)" : "SwapContents(save)", restore != 0,
                  _AddressOfReturnAddress());
    SurfacePixels desc;
    if (restore) {
        if (!holder_) {
            // A new surface of the surface's size.
            Surface* made = new Surface(interface_->renderer_, width_, height_);
            Surface* previous = holder_;
            holder_ = made;
            if (made) ++made->refs_;
            if (previous) ReleaseHolder(previous);
        }
        if (ui_native::TakeBack(this)) return;   // the pixels were kept on the GPU (PutAside)
        if (!kept_) {
            ui_native::Restored(this);
            return;
        }
        Surface* holder = holder_;
        gameapi::EnterCriticalSection(&g_deviceLock);
        if (DirectPixels(holder, &desc)) std::memcpy(desc.bits, kept_, keptSize_);
        gameapi::LeaveCriticalSection(&g_deviceLock);
        delete[] kept_;
        kept_ = nullptr;
        keptSize_ = 0;
        ui_native::Restored(this);
        return;
    }
    Surface* holder = holder_;
    if (!holder) return;
    if (ui_native::PutAside(this)) {
        // The pixels are kept on the GPU (a screen copy's canvas): only the holder goes.
        holder_ = nullptr;
        holder->Release();
        return;
    }
    ui_native::Sync(this);
    gameapi::EnterCriticalSection(&g_deviceLock);
    bool direct = DirectPixels(holder, &desc, true);   // (no memory: an empty copy)
    if (kept_) delete[] kept_;
    keptSize_ = static_cast<std::uint32_t>(desc.height) * static_cast<std::uint32_t>(desc.pitch);
    kept_ = new std::uint8_t[keptSize_];
    if (direct) std::memcpy(kept_, desc.bits, keptSize_);
    gameapi::LeaveCriticalSection(&g_deviceLock);
    Surface* released = holder_;
    holder_ = nullptr;
    if (released) released->Release();
}

// The pixel at (x, y) of the surface as 0x00RRGGBB (through the format converter); 0 outside the surface or
// without one.
std::uint32_t CUiSurface::PixelAt(int x, int y) {
    ObservePixels(this, "PixelAt", false, _AddressOfReturnAddress());
    Surface* holder = HolderOf(this);
    if (!holder || x < 0 || y < 0) return 0;
    if (x >= width_ || y >= height_) return 0;
    // A screen copy's pixel comes from the GPU (the global map's pick picture), anything else from the memory.
    std::uint32_t gpuPixel = 0;
    const bool onGpu = ui_native::PixelOnGpu(this, x, y, &gpuPixel);
    if (!onGpu) ui_native::Sync(this);
    gameapi::EnterCriticalSection(&g_deviceLock);
    SurfacePixels desc;
    if (!DirectPixels(holder, &desc, true)) {
        gameapi::LeaveCriticalSection(&g_deviceLock);
        return 0;
    }
    int bytes = desc.format.bitsPerPixel >> 3;
    const std::uint8_t* at = desc.bits + static_cast<std::size_t>(desc.pitch) * static_cast<std::uint32_t>(y) +
                             static_cast<std::size_t>(bytes) * static_cast<std::uint32_t>(x);
    std::uint32_t value = onGpu ? gpuPixel : bytes > 0 ? ReadPixel(at, bytes) : 0;
    std::uint32_t rgb = LayoutOf(desc).FromNative(value);
    gameapi::LeaveCriticalSection(&g_deviceLock);
    return rgb;
}
