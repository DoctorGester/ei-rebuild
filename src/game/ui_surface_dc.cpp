// A window surface's device context (CUiSurface; 005FDC00 gets it, 005FDC60 releases it): the
// device lock is held from one to the other and the HDC kept in the surface's dc_. With the new
// renderer's "cpuwindows" option a surface whose pixels are in the new renderer's memory
// (dx_surface.cpp, a DIB section) is drawn through a memory DC on it instead of DirectDraw's GetDC:
// the GDI drawing lands in the same memory without DirectDraw (in the switch-over the holder has no
// DirectDraw surface at all). See docs/notes/renderer.md ("Interface windows", section 6).
#include "app.h"
#include "dx_surface.h"
#include "game_strings.h"

#include "device.h"
#include "ui.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc_gdi.h"

#include <ddraw.h>

#include <cstdint>
#include <unordered_map>
#include "originals_engine.h"
#include "originals_ui.h"

namespace {


constexpr long kWasStillDrawing = static_cast<long>(0x8876021C);   // DDERR_WASSTILLDRAWING

// 004800E0 (cdecl): a DirectX error report; nonzero to go on.
void* CdcFromHandle(HDC dc) { return mfc::impl::CDC::FromHandle(dc); }

// The memory DCs given out, with the bitmap each had selected before ours.
std::unordered_map<HDC, HGDIOBJ> g_memoryDcs;

} // namespace

// The verify build's interface census (tests/verify/ui_census.cpp): a memory DC 005FDC00 gave out.
bool IsSurfaceMemoryDc(HDC dc) { return g_memoryDcs.count(dc) != 0; }

// 005FDC00. The surface's CDC (a temporary MFC wrapper), or null without a surface or when getting
// the DC failed and the report said stop (the device lock then stays held, as in the original).
void* __fastcall UiSurfaceGetDC(CUiSurface* surface) {
    CDXSurface* holder = surface->holder_;
    if (!holder) return nullptr;
    HDC& dc = surface->dc_;
    dc = nullptr;
    gameapi::EnterCriticalSection(GameDeviceLock());
    if (HBITMAP bitmap = ClientBitmap(holder)) {
        dc = CreateCompatibleDC(nullptr);
        if (dc) g_memoryDcs[dc] = gameapi::SelectObject(dc, bitmap);
        holder->result = dc ? 0 : -1;
        if (!dc && !app::FatalErrorCode(-1, game::Text(0x00793290))) return nullptr;   // "Can't get surface's DC"
        return CdcFromHandle(dc);
    }
    long hr;
    do {
        hr = device::Surface(holder)->GetDC(&dc);
        holder->result = hr;
    } while (hr == kWasStillDrawing);
    if (hr != 0 && !app::FatalErrorCode(hr, game::Text(0x00793290))) return nullptr;
    return CdcFromHandle(dc);
}
EI_REPLACE(0x005FDC00, UiSurfaceGetDC);

// 005FDC60. Leaves the device lock and releases the DC.
void __fastcall UiSurfaceReleaseDC(CUiSurface* surface) {
    CDXSurface* holder = surface->holder_;
    if (holder) {
        HDC dc = surface->dc_;
        gameapi::LeaveCriticalSection(GameDeviceLock());
        auto it = g_memoryDcs.find(dc);
        if (it != g_memoryDcs.end()) {
            GdiFlush();
            gameapi::SelectObject(dc, it->second);
            DeleteDC(dc);
            g_memoryDcs.erase(it);
            holder->result = 0;
        } else {
            long hr = device::Surface(holder)->ReleaseDC(dc);
            holder->result = hr;
            if (hr != 0) app::FatalErrorCode(hr, game::Text(0x007932AC));   // "Can't release surface's DC"
        }
    }
    surface->dc_ = nullptr;
}
EI_REPLACE(0x005FDC60, UiSurfaceReleaseDC);
