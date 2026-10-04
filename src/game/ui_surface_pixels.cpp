// The interface surfaces' pixel work (CUiSurface, ui.h): copying the screen into a surface
// (005FD3D0), a 16-bit 5:6:5 copy of a surface at a given size (005FD440, the save game's picture)
// and greying a surface in place (005FE440, the escape menu's background). The escape menu's
// drawing (005F5320, Rebirth-patched) runs all three when the menu opens. See docs/notes/ui.md,
// "Surface pixels".
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cstring>

namespace {

// What a surface's +0x10 points at: a counted holder of the DirectDraw surface.
struct SurfaceHolder {
    void* vtable;                // [0](1): deleting destructor
    int references;              // +0x04
    long result;                 // +0x08 the last DirectDraw call's result
    std::uint8_t* device;        // +0x0C the device; its +0x18 holds the screen's holder
    void** surface;              // +0x10 IDirectDrawSurface7
};

SurfaceHolder* HolderOf(const CUiSurface* surface) {
    SurfaceHolder* holder;
    std::memcpy(&holder, reinterpret_cast<const std::uint8_t*>(surface) + 0x10, 4);
    return holder;
}

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// 004800E0 (cdecl): a fatal DirectX error with its message.
__declspec(naked) void __cdecl FatalDirectX(long, const char*) EI_ORIGINAL(0x004800E0)
// 004F0B00 (cdecl): the copies' error report (an empty function in 1.06).
__declspec(naked) void __cdecl ReportBlitError(long, const char*) EI_ORIGINAL(0x004F0B00)

// IDirectDrawSurface7 slots.
long Blt(void** surface, RECT* to, void** from, RECT* fromRect, unsigned long flags, void* fx) {
    using Fn = long(__stdcall*)(void**, RECT*, void**, RECT*, unsigned long, void*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(surface))[5])(surface, to, from, fromRect, flags, fx);
}
long LockSurface(void** surface, std::uint32_t* desc) {
    using Fn = long(__stdcall*)(void**, RECT*, void*, unsigned long, void*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(surface))[25])(surface, nullptr, desc, 1, nullptr);
}
long UnlockSurface(void** surface) {
    using Fn = long(__stdcall*)(void**, RECT*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(surface))[32])(surface, nullptr);
}

constexpr unsigned long kBltWait = 0x01000000;   // DDBLT_WAIT

// DDSURFACEDESC2 as dwords.
constexpr int kDescHeight = 2;      // +0x08
constexpr int kDescWidth = 3;       // +0x0C
constexpr int kDescSurface = 9;     // +0x24 lpSurface
constexpr int kDescFormat = 0x12;   // +0x48 DDPIXELFORMAT
constexpr int kDescBits = 0x15;     // +0x54 dwRGBBitCount
constexpr int kDescRed = 0x16;      // +0x58
constexpr int kDescGreen = 0x17;    // +0x5C
constexpr int kDescBlue = 0x18;     // +0x60

// Converts pixels of a DirectDraw format to and from 0x00RRGGBB (0x34 bytes).
struct PixelConverter {
    int Init(const void* format);              // 004EDAA0 (RET 4)
    std::uint32_t FromNative(std::uint32_t);   // 004EDC70 (RET 4)
    std::uint32_t ToNative(std::uint32_t);     // 004EDC00 (RET 4)
    std::uint8_t state[0x34];
};
__declspec(naked) int PixelConverter::Init(const void*) EI_ORIGINAL(0x004EDAA0)
__declspec(naked) std::uint32_t PixelConverter::FromNative(std::uint32_t) EI_ORIGINAL(0x004EDC70)
__declspec(naked) std::uint32_t PixelConverter::ToNative(std::uint32_t) EI_ORIGINAL(0x004EDC00)

// SHL / SHR by CL (the count taken mod 32).
std::uint32_t Shl(std::uint32_t v, int count) { return v << (count & 31); }
std::uint32_t Shr(std::uint32_t v, int count) { return v >> (count & 31); }

// A pixel of `bytes` bytes, little-endian.
std::uint32_t ReadPixel(const std::uint8_t* at, int bytes) {
    std::uint32_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= Shl(at[i], i * 8);
    return value;
}

// The grey of 0x00RRGGBB: (2 r + 4 g + b) / 16 in every channel (each weight on the 8-bit value).
std::uint32_t Grey32(std::uint32_t pixel) {
    std::uint32_t grey = (((pixel >> 6) & 0x3FC) + ((pixel >> 15) & 0x1FE) + (pixel & 0xFF)) >> 4;
    return ((grey << 8 | grey) << 8) | grey;
}

} // namespace

void CUiSurface::CaptureScreen() {
    SurfaceHolder* holder = HolderOf(this);
    if (!holder) return;
    SurfaceHolder* screen = *reinterpret_cast<SurfaceHolder**>(holder->device + 0x18);
    if (screen->surface == holder->surface) return;
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = Blt(holder->surface, nullptr, screen->surface, &rect_, 0, nullptr);
    gameapi::LeaveCriticalSection(DeviceLock());
    if (hr != 0) ReportBlitError(hr, Text(0x00793224));
}

std::uint16_t* CUiSurface::Rgb565Copy(int width, int height) {
    if (!HolderOf(this)) return nullptr;
    // A surface of the wanted size in this one's format, and this one stretched into it.
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    CUiSurface* copy = nullptr;
    if (memory) {
        int format;
        std::memcpy(&format, reinterpret_cast<std::uint8_t*>(this) + 0x0C, 4);
        copy = static_cast<CUiSurface*>(memory)->Construct(interface_, 0, 0, width, height, format, 0);
    }
    SurfaceHolder* from = HolderOf(this);
    SurfaceHolder* to = HolderOf(copy);
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = Blt(to->surface, nullptr, from->surface, nullptr, kBltWait, nullptr);
    gameapi::LeaveCriticalSection(DeviceLock());
    if (hr != 0) ReportBlitError(hr, Text(0x00793248));

    int count = static_cast<int>(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height));
    auto* pixels = static_cast<std::uint16_t*>(gamecrt::operator_new(static_cast<std::size_t>(count) * 2));
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    SurfaceHolder* holder = HolderOf(copy);
    gameapi::EnterCriticalSection(DeviceLock());
    std::memset(desc, 0, sizeof(desc));
    desc[0] = 0x7C;
    hr = LockSurface(holder->surface, desc);
    holder->result = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E30C));   // "Can't lock surface"
    int bytes = static_cast<int>(desc[kDescBits] >> 3);
    PixelConverter converter;
    converter.Init(&desc[kDescFormat]);
    const std::uint8_t* source = reinterpret_cast<const std::uint8_t*>(desc[kDescSurface]);
    std::uint16_t* out = pixels;
    if (count != 0) {
        std::uint32_t left = static_cast<std::uint32_t>(count);
        do {
            std::uint32_t rgb = converter.FromNative(ReadPixel(source, bytes));
            std::uint32_t packed = ((((rgb >> 3) & 0x1F0000) | (rgb & 0xFC00)) >> 2 | (rgb & 0xF8)) >> 3;
            *out++ = static_cast<std::uint16_t>(packed);
            source += bytes;
        } while (--left != 0);
    }
    holder = HolderOf(copy);
    gameapi::LeaveCriticalSection(DeviceLock());
    hr = UnlockSurface(holder->surface);
    holder->result = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E2F4));   // "Can't unlock surface"
    if (copy) {
        copy->ReleaseContents();
        gamecrt::operator_delete(copy);
    }
    return pixels;
}

void CUiSurface::Greyscale() {
    if (!HolderOf(this)) return;
    SurfaceHolder* holder = HolderOf(this);
    gameapi::EnterCriticalSection(DeviceLock());
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    long hr = LockSurface(holder->surface, desc);
    holder->result = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E30C));   // "Can't lock surface"
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
        PixelConverter converter;
        converter.Init(&desc[kDescFormat]);
        int bytes = static_cast<int>(bits >> 3);
        if (count != 0) {
            do {
                std::uint32_t native = converter.ToNative(Grey32(converter.FromNative(ReadPixel(p, bytes))));
                for (int i = 0; i < bytes; ++i) p[i] = static_cast<std::uint8_t>(Shr(native, i * 8));
                p += bytes;
            } while (--count != 0);
        }
    }
    holder = HolderOf(this);
    gameapi::LeaveCriticalSection(DeviceLock());
    hr = UnlockSurface(holder->surface);
    holder->result = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E2F4));   // "Can't unlock surface"
}

namespace {

// 004EF7B0 (thiscall, RET 0xC): a surface holder's constructor (device, width, height).
__declspec(naked) SurfaceHolder* __fastcall ConstructHolder(void*, void*, void*, int, int) EI_ORIGINAL(0x004EF7B0)
// 004E6BA0 (thiscall): drops a holder reference.
__declspec(naked) void __fastcall ReleaseHolder(SurfaceHolder*) EI_ORIGINAL(0x004E6BA0)

long GetSurfaceDesc(void** surface, std::uint32_t* desc) {
    using Fn = long(__stdcall*)(void**, void*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(surface))[22])(surface, desc);
}

std::uint32_t& KeptBytesTotal() { return *reinterpret_cast<std::uint32_t*>(0x007C7E74); }
std::uint32_t& SurfaceBytesTotal() { return *reinterpret_cast<std::uint32_t*>(0x007C7E78); }

constexpr int kDescPitch = 4;   // +0x10

} // namespace

void CUiSurface::SwapContents(int restore) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    auto keptAt = [self] { return reinterpret_cast<std::uint8_t**>(self + 0x1C); };
    auto keptSize = [self] { return reinterpret_cast<std::uint32_t*>(self + 0x20); };
    auto holderAt = [self] { return reinterpret_cast<SurfaceHolder**>(self + 0x10); };
    std::uint32_t desc[0x1F];
    if (restore) {
        if (!*holderAt()) {
            // A new surface of the surface's size.
            void* memory = gamecrt::operator_new(0x20);
            SurfaceHolder* made = nullptr;
            if (memory) {
                void* device;
                std::memcpy(&device, reinterpret_cast<std::uint8_t*>(interface_) + 8, 4);
                int width, height;
                std::memcpy(&width, self + 0x14, 4);
                std::memcpy(&height, self + 0x18, 4);
                made = ConstructHolder(memory, nullptr, device, width, height);
            }
            SurfaceHolder* previous = *holderAt();
            *holderAt() = made;
            if (made) ++made->references;
            if (previous) ReleaseHolder(previous);
            SurfaceHolder* holder = *holderAt();
            std::memset(desc, 0, sizeof(desc));
            desc[0] = 0x7C;
            long hr = GetSurfaceDesc(holder->surface, desc);
            holder->result = hr;
            if (hr != 0) FatalDirectX(hr, Text(0x0078E6C4));
            SurfaceBytesTotal() += desc[kDescHeight] * desc[kDescPitch];
        }
        if (!*keptAt()) return;
        SurfaceHolder* holder = *holderAt();
        gameapi::EnterCriticalSection(DeviceLock());
        std::memset(desc, 0, sizeof(desc));
        desc[0] = 0x7C;
        long hr = LockSurface(holder->surface, desc);
        holder->result = hr;
        if (hr != 0) FatalDirectX(hr, Text(0x0078E30C));   // "Can't lock surface"
        std::memcpy(reinterpret_cast<void*>(desc[kDescSurface]), *keptAt(), *keptSize());
        KeptBytesTotal() -= *keptSize();
        holder = *holderAt();
        gameapi::LeaveCriticalSection(DeviceLock());
        hr = UnlockSurface(holder->surface);
        holder->result = hr;
        if (hr != 0) FatalDirectX(hr, Text(0x0078E2F4));   // "Can't unlock surface"
        gamecrt::operator_delete(*keptAt());
        *keptAt() = nullptr;
        *keptSize() = 0;
        return;
    }
    SurfaceHolder* holder = *holderAt();
    if (!holder) return;
    gameapi::EnterCriticalSection(DeviceLock());
    std::memset(desc, 0, sizeof(desc));
    desc[0] = 0x7C;
    long hr = LockSurface(holder->surface, desc);
    holder->result = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E30C));   // "Can't lock surface"
    if (*keptAt()) gamecrt::operator_delete(*keptAt());
    *keptSize() = desc[kDescHeight] * desc[kDescPitch];
    *keptAt() = static_cast<std::uint8_t*>(gamecrt::operator_new(*keptSize()));
    std::memcpy(*keptAt(), reinterpret_cast<const void*>(desc[kDescSurface]), *keptSize());
    KeptBytesTotal() += *keptSize();
    holder = *holderAt();
    gameapi::LeaveCriticalSection(DeviceLock());
    hr = UnlockSurface(holder->surface);
    holder->result = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E2F4));   // "Can't unlock surface"
    SurfaceBytesTotal() -= *keptSize();
    SurfaceHolder* released = *holderAt();
    *holderAt() = nullptr;
    if (released && --released->references == 0) {
        using DeleteFn = void(__thiscall*)(SurfaceHolder*, int);
        reinterpret_cast<DeleteFn>(*reinterpret_cast<void**>(released->vtable))(released, 1);
    }
}

namespace {
void __fastcall SurfaceSwapContents(CUiSurface* self, void*, int restore) { self->SwapContents(restore); }
void __fastcall SurfaceCaptureScreen(CUiSurface* self, void*) { self->CaptureScreen(); }
std::uint16_t* __fastcall SurfaceRgb565Copy(CUiSurface* self, void*, int width, int height) {
    return self->Rgb565Copy(width, height);
}
void __fastcall SurfaceGreyscale(CUiSurface* self, void*) { self->Greyscale(); }
} // namespace
EI_REPLACE(0x005FD7A0, SurfaceSwapContents);
EI_REPLACE(0x005FD3D0, SurfaceCaptureScreen);
EI_REPLACE(0x005FD440, SurfaceRgb565Copy);
EI_REPLACE(0x005FE440, SurfaceGreyscale);
