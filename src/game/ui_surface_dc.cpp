// A window surface's device context (CUiSurface; 005FDC00 gets it, 005FDC60 releases it): the
// device lock is held from one to the other and the HDC kept in the surface's dc_. The surface's
// pixels are in the new renderer's memory (dx_surface.cpp, a DIB section; the holder has no
// DirectDraw surface), drawn through a memory DC on it instead of DirectDraw's GetDC. GDI keeps the
// bounds of what is drawn through the DC; its release marks them written for the new renderer
// (render::SurfaceWritten, R16a). See docs/notes/renderer.md ("Interface windows", section 6).
#include "app.h"
#include "dx_surface.h"
#include "game_strings.h"

#include "texture.h"
#include "ui.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc_gdi.h"
#include "render/render.h"

#include <cstdint>
#include <unordered_map>
#include "originals_engine.h"
#include "originals_ui.h"

namespace {

void* CdcFromHandle(HDC dc) { return mfc::impl::CDC::FromHandle(dc); }

// The memory DCs given out, with the bitmap each had selected before ours.
std::unordered_map<HDC, HGDIOBJ> g_memoryDcs;

// Marks what GDI drew through `dc` into the memory of `holder` written: the DC's bounds, a few pixels
// wider (text's overhang); all of it when the bounds cannot be read.
void MarkDrawn(const CDXSurface* holder, HDC dc) {
    RECT bounds{};
    UINT state = GetBoundsRect(dc, &bounds, 0);
    if (state & DCB_SET) {
        constexpr int kMargin = 2;
        render::Rect drawn{bounds.left - kMargin, bounds.top - kMargin, bounds.right + kMargin, bounds.bottom + kMargin};
        render::SurfaceWritten(holder, &drawn);
    } else if (!(state & DCB_RESET)) {
        render::SurfaceWritten(holder, nullptr);
    }
}

} // namespace

// The verify build's interface census (tests/verify/ui_census.cpp): a memory DC 005FDC00 gave out.
bool IsSurfaceMemoryDc(HDC dc) { return g_memoryDcs.count(dc) != 0; }

// 005FDC00. The surface's CDC (a temporary MFC wrapper), or null without a surface or when getting
// the DC failed (also: no memory) and the report said stop (the device lock then stays held, as in
// the original).
ui_gdi::GameDC* __fastcall UiSurfaceGetDC(CUiSurface* surface) {
    CDXSurface* holder = surface->holder_;
    if (!holder) return nullptr;
    HDC& dc = surface->dc_;
    dc = nullptr;
    gameapi::EnterCriticalSection(GameDeviceLock());
    HBITMAP bitmap = ClientBitmap(holder);
    if (bitmap) dc = CreateCompatibleDC(nullptr);
    if (dc) {
        g_memoryDcs[dc] = gameapi::SelectObject(dc, bitmap);
        SetBoundsRect(dc, nullptr, DCB_ENABLE | DCB_RESET);   // the bounds of what is drawn from now on
    }
    holder->result = dc ? 0 : -1;
    if (!dc && !app::FatalErrorCode(-1, game::Text(0x00793290))) return nullptr;   // "Can't get surface's DC"
    return static_cast<ui_gdi::GameDC*>(CdcFromHandle(dc));
}
EI_REPLACE(0x005FDC00, UiSurfaceGetDC);

// 005FDC60. Leaves the device lock and releases the DC (nothing for a DC 005FDC00 did not make).
void __fastcall UiSurfaceReleaseDC(CUiSurface* surface) {
    CDXSurface* holder = surface->holder_;
    if (holder) {
        HDC dc = surface->dc_;
        gameapi::LeaveCriticalSection(GameDeviceLock());
        auto it = g_memoryDcs.find(dc);
        if (it != g_memoryDcs.end()) {
            GdiFlush();
            MarkDrawn(holder, dc);
            gameapi::SelectObject(dc, it->second);
            DeleteDC(dc);
            g_memoryDcs.erase(it);
            holder->result = 0;
        }
    }
    surface->dc_ = nullptr;
}
EI_REPLACE(0x005FDC60, UiSurfaceReleaseDC);
