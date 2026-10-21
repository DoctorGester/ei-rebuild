#pragma once

// GDI drawing through the game's statically linked MFC 4.2 (src/lib/mfc_gdi.h), as the interface uses it
// on its off-screen surfaces: CUiSurface's GetDC (005FDC00) returns a CDC, ReleaseDC (005FDC60)
// gives it back. The CDC members the original calls through the vtable go through the same slot.

#include "ei/hook.h"
#include "lib/mfc_gdi.h"
#include "originals_ui.h"
#include "game_data.h"

#include <cstdint>

#include <windows.h>

class CUiSurface;   // ui.h
namespace ui_gdi { struct GameDC; }
// 005FDC00 / 005FDC60 (ui_surface_dc.cpp; declared in ui.h too): a surface's CDC and its release.
ui_gdi::GameDC* __fastcall UiSurfaceGetDC(CUiSurface* surface);
void __fastcall UiSurfaceReleaseDC(CUiSurface* surface);

namespace ui_gdi {

// Calls virtual slot `slot` of `object` (__thiscall).
template <typename R, typename... A>
R CallSlot(void* object, int slot, A... args) {
    using Fn = R(__thiscall*)(void*, A...);
    return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot])(object, args...);
}

// MFC CDC. The virtual members the interface calls go through the vtable as in the original: slot 10
// SelectObject(CFont*), 12 SetTextColor, 26 DrawText (these two hide CDC's direct members on purpose).
struct GameDC : mfc::impl::CDC {
    void* SelectFont(void* font) { return CallSlot<void*>(this, 10, font); }
    COLORREF SetTextColor(COLORREF colour) { return CallSlot<COLORREF>(this, 12, colour); }
    int DrawText(const char* text, int length, RECT* rect, UINT format) {
        return CallSlot<int>(this, 26, text, length, rect, format);
    }
};
static_assert(sizeof(GameDC) == sizeof(mfc::impl::CDC));

// MFC CPen, as a local: Construct (0070A0E2), then Destruct (~CPen as the game inlines it).
using GamePen = mfc::impl::CPen;

// MFC CBrush (CGdiObject: vtable, m_hObject), as a local: constructed empty with CBrush's vtable,
// a brush attached (CGdiObject::Attach), destroyed as the game inlines it (CGdiObject::Destruct).
struct GameBrush : mfc::impl::CGdiObject {
    void Construct() {
        vtable = ei::GamePointer<void>(0x007463C4);
        handle = nullptr;
    }
    // operator HBRUSH (GetSafeHandle; a local is never null).
    HBRUSH Handle() const { return static_cast<HBRUSH>(handle); }
};
static_assert(sizeof(GameBrush) == 8);

// MFC CFont (CGdiObject: vtable 0073BFB4, m_hObject), allocated with new by the interface
// (CInterface3D +0x70..+0x78) and the console window (+0x08).
struct GameFont : mfc::impl::CFont {
    // Slot 1: CObject's virtual (scalar deleting) destructor; 1 frees the memory.
    void Delete() { CallSlot<void*>(this, 1, 1); }
};
static_assert(sizeof(GameFont) == 8);

// The DC of an interface surface (CUiSurface; also the console window's surface).
struct SurfaceDC {
    // 005FDC00 (null when the surface has no DirectDraw surface) and 005FDC60.
    GameDC* GetDC() { return UiSurfaceGetDC(reinterpret_cast<CUiSurface*>(this)); }
    void ReleaseDC() { UiSurfaceReleaseDC(reinterpret_cast<CUiSurface*>(this)); }
};

} // namespace ui_gdi
