// The software mouse cursor's blit (005E5C40; the cursor object at 007C7BD8, see docs/notes/ui.md,
// "Cursor"). Used by the cursor thread (005E5690) and 005E54A0 when the display option's software
// cursor bit is set. Also the cursor images' constructor (005E4EB0), which builds each image's mask
// from its picture: in the switch-over with "cpuwindows" the picture's holder has no DirectDraw
// surface (ticket R7), and the mask is built from the new renderer's memory (DirectPixels).
#include "ui_cursor.h"
#include "game_strings.h"
#include "dx_surface.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_render.h"

namespace {

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// 004800E0 (cdecl): a fatal DirectX error; its result says whether the caller goes on.

void** SurfaceOf(CDXSurface* holder) { return static_cast<void**>(holder->surface); }

// Enters the device lock (left by Unlock) and locks `rect` of the holder's surface (DDLOCK_WAIT).
// Nonzero when locked, or what the error report returned.
int Lock(CDXSurface* holder, RECT* rect, std::uint32_t* desc) {
    gameapi::EnterCriticalSection(DeviceLock());
    std::memset(desc, 0, 0x7C);
    desc[0] = 0x7C;
    void** surface = SurfaceOf(holder);
    using LockFn = long(__stdcall*)(void**, RECT*, void*, unsigned long, void*);
    long hr = reinterpret_cast<LockFn>((*reinterpret_cast<void***>(surface))[25])(surface, rect, desc, 1, nullptr);
    holder->result = hr;
    if (hr != 0) return FatalDirectX(hr, game::Text(0x0078E30C));   // "Can't lock surface"
    return 1;
}

void Unlock(CDXSurface* holder) {
    gameapi::LeaveCriticalSection(DeviceLock());
    void** surface = SurfaceOf(holder);
    using UnlockFn = long(__stdcall*)(void**, RECT*);
    long hr = reinterpret_cast<UnlockFn>((*reinterpret_cast<void***>(surface))[32])(surface, nullptr);
    holder->result = hr;
    if (hr != 0) FatalDirectX(hr, game::Text(0x0078E2F4));   // "Can't unlock surface"
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
        int bits = image->holder->bitsPerPixel;
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

EI_REPLACE_METHOD(0x005E5C40, CCursor::Blit);

// --- 005E4EB0: a cursor image ------------------------------------------------------------------------

namespace {

CTextureManager* TextureManager() { return *reinterpret_cast<CTextureManager**>(0x007AF744); }
CTextureDevice* DeviceObject() { return *reinterpret_cast<CTextureDevice**>(0x007AF080); }
// The cursor's size (the hot spots are given for a 32-pixel cursor).
LONG CursorWidth() { return *reinterpret_cast<LONG*>(0x007C7C90); }
LONG CursorHeight() { return *reinterpret_cast<LONG*>(0x007C7C94); }

// DDSURFACEDESC2 as dwords (with kDescWidth, kDescPitch, kDescSurface above).
constexpr int kDescHeight = 2;    // +0x08
constexpr int kGetSurfaceDesc = 22, kLockSlot = 25, kUnlockSlot = 32;   // IDirectDrawSurface7
constexpr unsigned long kLockWait = 1;                                  // DDLOCK_WAIT

template <typename... Args>
long SurfaceCall(void* surface, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(surface))[slot])(surface, args...);
}

// The mask of `count` pixels of a row: all ones where the pixel's colour is 0 (16 bits: the whole
// pixel; 32 bits: its low 24 bits), else 0.
template <typename T>
void MaskRow(const std::uint8_t* row, T* mask, std::uint32_t count, T colourBits) {
    for (std::uint32_t x = 0; x < count; ++x) {
        T pixel;
        std::memcpy(&pixel, row + x * sizeof(T), sizeof(T));
        mask[x] = (pixel & colourBits) == 0 ? static_cast<T>(~T{0}) : T{0};
    }
}

} // namespace

CursorImage* CursorImage::Construct(const char* name, int hotSpotX, int hotSpotY, int flag) {
    holder = nullptr;
    frame = 0;
    CTextureManager* manager = TextureManager();
    void* picture = manager->GetFile(name, nullptr);
    // (The original frees the holder's memory when 004EFC60 throws and passes the exception on; that
    // ends the game.)
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    holder = memory ? static_cast<CDXSurface*>(DXSurfaceFromPictureOriginal(memory, nullptr, DeviceObject(), picture, flag))
                    : nullptr;
    if (holder) ++holder->refCount;
    // (The original releases a holder kept before here; there is none: it was cleared above.)
    manager->CloseFile(picture);

    // The picture's size: from the new renderer's memory when the pixel code works there (always for
    // a holder without a DirectDraw surface), else GetSurfaceDesc (a failure is reported, then
    // ignored).
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    bool direct = DirectPixels(holder, desc);
    if (!direct) {
        long hr = SurfaceCall(holder->surface, kGetSurfaceDesc, static_cast<void*>(desc));
        holder->result = hr;
        if (hr != 0) FatalDirectX(hr, game::Text(0x0078E6C4));   // "Can't get surface description"
    }
    std::uint32_t pictureWidth = desc[kDescWidth], pictureHeight = desc[kDescHeight];
    std::uint32_t count = pictureWidth / pictureHeight;
    height = static_cast<LONG>(pictureHeight);
    frames = static_cast<int>(count);
    width = static_cast<LONG>(pictureWidth / count);
    hotX = static_cast<int>(CursorWidth()) * hotSpotX / 32;
    hotY = static_cast<int>(CursorHeight()) * hotSpotY / 32;
    std::uint32_t maskBytes = (static_cast<std::uint32_t>(holder->bitsPerPixel) * pictureHeight * pictureWidth) >> 3;
    saved = static_cast<std::uint8_t*>(gamecrt::operator_new(maskBytes));

    // The mask, under the device lock; the pixels locked (DDLOCK_WAIT; a failure is reported, then
    // ignored) unless they are in the new renderer's memory.
    gameapi::EnterCriticalSection(DeviceLock());
    if (!direct) {
        std::memset(desc, 0, sizeof(desc));
        desc[0] = 0x7C;
        long hr = SurfaceCall(holder->surface, kLockSlot, static_cast<RECT*>(nullptr), static_cast<void*>(desc), kLockWait,
                              static_cast<void*>(nullptr));
        holder->result = hr;
        if (hr != 0) FatalDirectX(hr, game::Text(0x0078E30C));   // "Can't lock surface"
    }
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(desc[kDescSurface]));
    std::uint32_t rows = desc[kDescHeight], columns = desc[kDescWidth], pitch = desc[kDescPitch];
    if (holder->bitsPerPixel == 16) {
        auto* mask = reinterpret_cast<std::uint16_t*>(saved);
        for (std::uint32_t y = 0; y < rows; ++y)
            MaskRow<std::uint16_t>(pixels + y * ((pitch >> 1) * 2), mask + y * columns, columns, 0xFFFF);
    } else if (holder->bitsPerPixel == 32) {
        auto* mask = reinterpret_cast<std::uint32_t*>(saved);
        for (std::uint32_t y = 0; y < rows; ++y)
            MaskRow<std::uint32_t>(pixels + y * ((pitch >> 2) * 4), mask + y * columns, columns, 0xFFFFFF);
    }
    gameapi::LeaveCriticalSection(DeviceLock());
    if (!direct) {
        long hr = SurfaceCall(holder->surface, kUnlockSlot, static_cast<RECT*>(nullptr));
        holder->result = hr;
        if (hr != 0) FatalDirectX(hr, game::Text(0x0078E2F4));   // "Can't unlock surface"
    }
    return this;
}
EI_REPLACE_METHOD(0x005E4EB0, CursorImage::Construct);
