// The software mouse cursor's blit (005E5C40; the cursor object at 007C7BD8, see docs/notes/ui.md,
// "Cursor"). Used by the cursor thread (005E5690) and 005E54A0 when the display option's software
// cursor bit is set.
#include "ui_cursor.h"

#include "ei/hook.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cstring>
#include "originals_engine.h"

namespace {

template <typename T>
T Get(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}
template <typename T>
void Set(void* p, int offset, T v) {
    std::memcpy(static_cast<std::uint8_t*>(p) + offset, &v, sizeof(T));
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// 004800E0 (cdecl): a fatal DirectX error; its result says whether the caller goes on.

// A surface holder: +0x08 the last result, +0x10 the IDirectDrawSurface7, +0x1C bits per pixel.
void** SurfaceOf(std::uint8_t* holder) { return Get<void**>(holder, 0x10); }

// Enters the device lock (left by Unlock) and locks `rect` of the holder's surface (DDLOCK_WAIT).
// Nonzero when locked, or what the error report returned.
int Lock(std::uint8_t* holder, RECT* rect, std::uint32_t* desc) {
    gameapi::EnterCriticalSection(DeviceLock());
    std::memset(desc, 0, 0x7C);
    desc[0] = 0x7C;
    void** surface = SurfaceOf(holder);
    using LockFn = long(__stdcall*)(void**, RECT*, void*, unsigned long, void*);
    long hr = reinterpret_cast<LockFn>((*reinterpret_cast<void***>(surface))[25])(surface, rect, desc, 1, nullptr);
    Set(holder, 8, hr);
    if (hr != 0) return FatalDirectX(hr, Text(0x0078E30C));   // "Can't lock surface"
    return 1;
}

void Unlock(std::uint8_t* holder) {
    gameapi::LeaveCriticalSection(DeviceLock());
    void** surface = SurfaceOf(holder);
    using UnlockFn = long(__stdcall*)(void**, RECT*);
    long hr = reinterpret_cast<UnlockFn>((*reinterpret_cast<void***>(surface))[32])(surface, nullptr);
    Set(holder, 8, hr);
    if (hr != 0) FatalDirectX(hr, Text(0x0078E2F4));   // "Can't unlock surface"
}

// The blend for pixels of type T (16 or 32 bits): to = (under & saved) | image, row by row over
// `to`; each pointer steps to its next row by its own pitch (the saved copy by the image's width).
template <typename T>
void Blend(const RECT* to, std::uintptr_t image, std::uintptr_t under, std::uintptr_t target, std::uintptr_t saved,
           std::uint32_t imagePitch, std::uint32_t underPitch, std::uint32_t targetPitch, int imageWidth) {
    constexpr int kShift = sizeof(T) == 2 ? 1 : 2;
    int right = to->right;
    int left = to->left;
    int imageStep = static_cast<int>(imagePitch >> kShift) - right + left;
    int underStep = static_cast<int>(underPitch >> kShift) - right + left;
    int targetStep = static_cast<int>(targetPitch >> kShift) - right + left;
    int savedStep = left - right + imageWidth;
    for (int y = to->top; y < to->bottom; ++y) {
        for (int x = to->left; x < to->right; ++x) {
            T value = static_cast<T>((*reinterpret_cast<const T*>(under) & *reinterpret_cast<const T*>(saved)) |
                                     *reinterpret_cast<const T*>(image));
            *reinterpret_cast<T*>(target) = value;
            image += sizeof(T);
            under += sizeof(T);
            target += sizeof(T);
            saved += sizeof(T);
        }
        image += static_cast<std::uintptr_t>(imageStep) * sizeof(T);
        target += static_cast<std::uintptr_t>(targetStep) * sizeof(T);
        under += static_cast<std::uintptr_t>(underStep) * sizeof(T);
        saved += static_cast<std::uintptr_t>(savedStep) * sizeof(T);
    }
}

// DDSURFACEDESC2 as dwords.
constexpr int kDescWidth = 3;     // +0x0C
constexpr int kDescPitch = 4;     // +0x10
constexpr int kDescSurface = 9;   // +0x24

} // namespace

void CCursor::Blit(CursorImage* image, RECT* to, RECT* from) {
    std::uint32_t imageDesc[0x1F], targetDesc[0x1F], underDesc[0x1F];
    int imageLocked = Lock(image->holder, from, imageDesc);
    int targetLocked = Lock(target_, to, targetDesc);
    RECT whole = {0, 0, from->right - from->left, from->bottom - from->top};
    int underLocked = Lock(under_, &whole, underDesc);
    if (imageLocked && targetLocked && underLocked) {
        int bits = Get<int>(image->holder, 0x1C);
        int imageWidth = static_cast<int>(imageDesc[kDescWidth]);
        if (bits == 16) {
            std::uintptr_t saved = reinterpret_cast<std::uintptr_t>(image->saved) +
                                   (static_cast<std::uint32_t>(from->top) * static_cast<std::uint32_t>(imageWidth) + static_cast<std::uint32_t>(from->left)) * 2;
            Blend<std::uint16_t>(to, imageDesc[kDescSurface], underDesc[kDescSurface], targetDesc[kDescSurface], saved,
                                 imageDesc[kDescPitch], underDesc[kDescPitch], targetDesc[kDescPitch], imageWidth);
        } else if (bits == 32) {
            std::uintptr_t saved = reinterpret_cast<std::uintptr_t>(image->saved) +
                                   (static_cast<std::uint32_t>(from->top) * static_cast<std::uint32_t>(imageWidth) + static_cast<std::uint32_t>(from->left)) * 4;
            Blend<std::uint32_t>(to, imageDesc[kDescSurface], underDesc[kDescSurface], targetDesc[kDescSurface], saved,
                                 imageDesc[kDescPitch], underDesc[kDescPitch], targetDesc[kDescPitch], imageWidth);
        }
    }
    if (underLocked) Unlock(under_);
    if (targetLocked) Unlock(target_);
    if (imageLocked) Unlock(image->holder);
}

namespace {
void __fastcall CursorBlit(CCursor* self, void*, CursorImage* image, RECT* to, RECT* from) {
    self->Blit(image, to, from);
}
} // namespace
EI_REPLACE(0x005E5C40, CursorBlit);
