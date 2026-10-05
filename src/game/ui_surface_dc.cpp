// A window surface's device context (CUiSurface; 005FDC00 gets it, 005FDC60 releases it): the
// device lock is held from one to the other and the HDC kept in the surface's +8. With the new
// renderer's "cpuwindows" option a surface whose pixels are in the new renderer's memory
// (dx_surface.cpp, a DIB section) is drawn through a memory DC on it instead of DirectDraw's GetDC:
// the GDI drawing lands in the same memory without DirectDraw (in the switch-over the holder has no
// DirectDraw surface at all). See docs/notes/renderer.md ("Interface windows", section 14).
#include "dx_surface.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <unordered_map>
#include "originals_engine.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

constexpr int kGetDC = 17, kReleaseDC = 26, kLock = 25, kUnlock = 32;   // IDirectDrawSurface7
constexpr unsigned long kLockWait = 1;                       // DDLOCK_WAIT
constexpr long kWasStillDrawing = static_cast<long>(0x8876021C);   // DDERR_WASSTILLDRAWING

// 004800E0 (cdecl): a DirectX error report; nonzero to go on.
// 00708D79: MFC's CDC::FromHandle (static, stdcall).
__declspec(naked) void* __stdcall CdcFromHandle(HDC) EI_ORIGINAL(0x00708D79)

// The memory DCs given out, with the bitmap each had selected before ours.
std::unordered_map<HDC, HGDIOBJ> g_memoryDcs;

} // namespace

// 005FDC00. The surface's CDC (a temporary MFC wrapper), or null without a surface or when getting
// the DC failed and the report said stop (the device lock then stays held, as in the original).
void* __fastcall UiSurfaceGetDC(char* surface) {
    char* holder = At<char*>(surface, 0x10);
    if (!holder) return nullptr;
    HDC& dc = At<HDC>(surface, 8);
    dc = nullptr;
    gameapi::EnterCriticalSection(DeviceLock());
    if (HBITMAP bitmap = ClientBitmap(holder)) {
        dc = CreateCompatibleDC(nullptr);
        if (dc) g_memoryDcs[dc] = gameapi::SelectObject(dc, bitmap);
        At<long>(holder, 8) = dc ? 0 : -1;
        if (!dc && !FatalDirectX(-1, Text(0x00793290))) return nullptr;   // "Can't get surface's DC"
        return CdcFromHandle(dc);
    }
    long hr;
    do {
        hr = ComCall(At<void*>(holder, 0x10), kGetDC, &dc);
        At<long>(holder, 8) = hr;
    } while (hr == kWasStillDrawing);
    if (hr != 0 && !FatalDirectX(hr, Text(0x00793290))) return nullptr;
    return CdcFromHandle(dc);
}
EI_REPLACE(0x005FDC00, UiSurfaceGetDC);

// 005FDC60. Leaves the device lock and releases the DC.
void __fastcall UiSurfaceReleaseDC(char* surface) {
    char* holder = At<char*>(surface, 0x10);
    if (holder) {
        HDC dc = At<HDC>(surface, 8);
        gameapi::LeaveCriticalSection(DeviceLock());
        auto it = g_memoryDcs.find(dc);
        if (it != g_memoryDcs.end()) {
            GdiFlush();
            gameapi::SelectObject(dc, it->second);
            DeleteDC(dc);
            g_memoryDcs.erase(it);
            At<long>(holder, 8) = 0;
            // DirectDraw did not see this drawing: an empty Lock / Unlock tells it the memory changed
            // (for the old path's Blt; the new renderer reads the memory itself). A holder made in
            // the switch-over has no DirectDraw surface.
            void* dd = At<void*>(holder, 0x10);
            if (render::DrawOldInterface() && dd) {
                std::uint32_t desc[0x1F] = {};
                desc[0] = 0x7C;
                gameapi::EnterCriticalSection(DeviceLock());
                if (ComCall(dd, kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), kLockWait,
                            static_cast<void*>(nullptr)) == 0)
                    ComCall(dd, kUnlock, static_cast<void*>(nullptr));
                gameapi::LeaveCriticalSection(DeviceLock());
            }
        } else {
            long hr = ComCall(At<void*>(holder, 0x10), kReleaseDC, dc);
            At<long>(holder, 8) = hr;
            if (hr != 0) FatalDirectX(hr, Text(0x007932AC));   // "Can't release surface's DC"
        }
    }
    At<HDC>(surface, 8) = nullptr;
}
EI_REPLACE(0x005FDC60, UiSurfaceReleaseDC);
