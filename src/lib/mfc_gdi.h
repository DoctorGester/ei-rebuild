#pragma once

// MFC 4.2's CDC members that reach GDI without a handle map (wingdi.cpp, afxwin1.inl), as the game's
// copy has them (ticket S8, docs/notes/standalone.md section 2). Registered at their library
// addresses (mfc_gdi.cpp), checked by tests/verify/verify_mfc_gdi.cpp. The callers (ui_gdi.h's
// GameDC) keep going through the forwarders until C13. The members that look up or attach CGdiObject
// / CDC handles (FromHandle, SelectObject, CGdiObject::Attach / DeleteObject, CPen's constructor,
// CFont::CreatePointFont) need MFC's handle maps and are S8a.

#include <windows.h>

namespace mfc::impl {

// CDC (12 bytes): the output DC and the attribute DC (the same one for a plain DC).
struct CDC {
    void* vtable;
    HDC hdc;            // +0x04 m_hDC
    HDC attribDC;       // +0x08 m_hAttribDC

    int SetBkMode(int mode);                                        // 007090C2
    POINT MoveTo(int x, int y);                                     // 00709613
    BOOL LineTo(int x, int y);                                      // 0070965F
    void FillSolidRect(const RECT* rect, COLORREF colour);          // 0070BADA
    void FillSolidArea(int x, int y, int cx, int cy, COLORREF colour);  // 0070BB04 FillSolidRect(x, y, cx, cy, colour)
};
static_assert(sizeof(CDC) == 12);

} // namespace mfc::impl
