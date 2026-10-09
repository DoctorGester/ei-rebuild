#pragma once

// GDI drawing through the game's statically linked MFC 4.2 (CDC, CPen), as the interface uses it
// on its off-screen surfaces: CUiSurface's GetDC (005FDC00) returns a CDC, ReleaseDC (005FDC60)
// gives it back. Virtual CDC members are called through the vtable slot the original uses; the
// non-virtual ones through their addresses (not rewritten).

#include "ei/hook.h"
#include "originals_ui.h"

#include <cstdint>

#include <windows.h>

namespace ui_gdi {

// Calls virtual slot `slot` of `object` (__thiscall).
template <typename R, typename... A>
R CallSlot(void* object, int slot, A... args) {
    using Fn = R(__thiscall*)(void*, A...);
    return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot])(object, args...);
}

// MFC CDC (vtable, m_hDC, m_hAttribDC, ...).
struct GameDC {
    // Virtual slots used by the interface: 10 SelectObject(CFont*), 12 SetTextColor, 26 DrawText.
    void* SelectFont(void* font) { return CallSlot<void*>(this, 10, font); }
    COLORREF SetTextColor(COLORREF colour) { return CallSlot<COLORREF>(this, 12, colour); }
    int DrawText(const char* text, int length, RECT* rect, UINT format) {
        return CallSlot<int>(this, 26, text, length, rect, format);
    }

    void FillSolidRect(int x, int y, int cx, int cy, COLORREF colour);   // 0070BB04
    int SetBkMode(int mode);                                             // 007090C2
    void* SelectPen(void* pen);                                          // 00708F5A SelectObject(CPen*)
    POINT MoveTo(int x, int y);                                          // 00709613
    BOOL LineTo(int x, int y);                                           // 0070965F

    void** vtable_;
    HDC hdc_;                        // +0x04 m_hDC
    HDC attribDC_;                   // +0x08 m_hAttribDC
};

// MFC CPen (CGdiObject: vtable, m_hObject).
struct GamePen {
    GamePen* Construct(int style, int width, COLORREF colour);   // 0070A0E2 CPen(int, int, COLORREF)
    BOOL DeleteObject();                                         // 0070A0CC CGdiObject::DeleteObject
    // ~CPen as the game inlines it: back to CGdiObject's vtable, then DeleteObject.
    void Destruct() {
        vtable_ = reinterpret_cast<void**>(0x007463D4);
        DeleteObject();
    }

    void** vtable_;
    HGDIOBJ handle_;
};
static_assert(sizeof(GamePen) == 8);

// MFC CBrush (CGdiObject: vtable, m_hObject), as a local: constructed empty with CBrush's vtable,
// a brush attached, destroyed as the game inlines it.
struct GameBrush {
    BOOL Attach(HGDIOBJ handle);                                 // 0070A075 CGdiObject::Attach
    BOOL DeleteObject();                                         // 0070A0CC CGdiObject::DeleteObject
    void Construct() {
        vtable_ = reinterpret_cast<void**>(0x007463C4);
        handle_ = nullptr;
    }
    void Destruct() {
        vtable_ = reinterpret_cast<void**>(0x007463D4);
        DeleteObject();
    }
    // operator HBRUSH (GetSafeHandle; a local is never null).
    HBRUSH Handle() const { return static_cast<HBRUSH>(handle_); }

    void** vtable_;
    HGDIOBJ handle_;
};
static_assert(sizeof(GameBrush) == 8);

// MFC CFont (CGdiObject: vtable 0073BFB4, m_hObject), allocated with new by the interface
// (CInterface3D +0x70..+0x78) and the console window (+0x08).
struct GameFont {
    // Slot 1: CObject's virtual (scalar deleting) destructor; 1 frees the memory.
    void Delete() { CallSlot<void*>(this, 1, 1); }

    void** vtable_;
    HGDIOBJ handle_;
};
static_assert(sizeof(GameFont) == 8);

// The DC of an interface surface (CUiSurface; also the console window's surface).
struct SurfaceDC {
    GameDC* GetDC() { return SurfaceGetDC(this); }      // 005FDC00 (null when the surface has no DirectDraw surface)
    void ReleaseDC() { SurfaceReleaseDC(this); }        // 005FDC60
};

} // namespace ui_gdi
