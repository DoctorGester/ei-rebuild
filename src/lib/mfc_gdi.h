#pragma once

// MFC 4.2's CDC and GDI objects as the game's copy has them (wingdi.cpp, wingdix.cpp, winhand.cpp,
// afxwin1.inl): tickets S8 (the members that reach GDI without a handle map) and S8a (FromHandle,
// SelectObject, CGdiObject's Attach / Detach / DeleteObject, CPen's constructor, CFont's point
// fonts, the CDC slots the interface calls, docs/notes/standalone.md section 2). Registered at their
// library addresses (mfc_gdi.cpp), checked in game by verify_mfc_gdi.cpp until S21d. The interface calls them
// directly (ui_gdi.h's GameDC, GamePen, GameBrush and GameFont are these classes).
//
// The handle maps are MFC's module thread state: the hook build uses the original's (afxMapHDC
// 00708D07, afxMapHGDIOBJ 00709FEE, through their addresses), the image has its own per thread
// (MapHDC / MapHGDIOBJ, registered image-only at those addresses).

#include "mfc_handle_map.h"
#include "game/game_data.h"

#include <windows.h>

namespace mfc::impl {

// The maps of DCs (CTempDC objects for unknown handles, both handles set) and of GDI objects
// (CTempGdiObject), made on first use when `create`; null otherwise. Image only.
CHandleMap* __stdcall MapHDC(BOOL create);         // 00708D07 afxMapHDC
CHandleMap* __stdcall MapHGDIOBJ(BOOL create);     // 00709FEE afxMapHGDIOBJ

// CGdiObject (8 bytes, vtable 007463D4): vtable, m_hObject.
struct CGdiObject {
    void* vtable;
    HGDIOBJ handle;     // +0x04 m_hObject

    static void* Vtable() { return ei::GamePointer<void>(0x007463D4); }

    // 0070A060: the object of `handle` (a CTempGdiObject when it has no permanent one), null for null.
    static CGdiObject* __stdcall FromHandle(HGDIOBJ handle);
    BOOL Attach(HGDIOBJ object);        // 0070A075: FALSE for null; else kept and put in the permanent map
    HGDIOBJ Detach();                   // 0070A0A2: taken out of the permanent map; the old handle
    BOOL DeleteObject();                // 0070A0CC: FALSE without a handle; else detached and deleted
    // ~CGdiObject as the game's code inlines it (S6c: 00444B30, 00444B90, 005FDDE0, the fonts'
    // destructors): the CGdiObject vtable, then DeleteObject. Their scalar deleting destructors
    // (00444B10, 00444B70, 005FDDC0): it, then the memory freed when bit 0 of `flags` is set.
    void Destruct();
    CGdiObject* DeletingDestruct(unsigned flags);
};
static_assert(sizeof(CGdiObject) == 8);

// CTempGdiObject's create function (its CRuntimeClass at 00745F88): one from its fixed allocator
// (007C8CF0), no handle. Null when out of memory.
void* __stdcall CreateTempGdiObject();          // 0070C028

// CPen (vtable 007463B4).
struct CPen : CGdiObject {
    static void* Vtable() { return ei::GamePointer<void>(0x007463B4); }
    // 0070A0E2 CPen(style, width, colour): CreatePen attached; a CResourceException when it fails.
    CPen* Construct(int style, int width, COLORREF colour);
};
static_assert(sizeof(CPen) == 8);

struct CDC;

// CFont (vtable 0073BFB4).
struct CFont : CGdiObject {
    // 0070BBE5: a font of `pointSize` tenths of a point, the face name and DEFAULT_CHARSET, through
    // CreatePointFontIndirect.
    BOOL CreatePointFont(int pointSize, const char* faceName, CDC* dc);
    // 0070BC2A: the font with its height in tenths of a point turned into logical units of `dc`'s
    // attribute DC (the screen's when null), attached.
    BOOL CreatePointFontIndirect(const LOGFONTA* font, CDC* dc);
};
static_assert(sizeof(CFont) == 8);

// CDC (0x10 bytes, vtable 007461B0): the output DC and the attribute DC (the same one for a plain DC).
struct CDC {
    void* vtable;
    HDC hdc;            // +0x04 m_hDC
    HDC attribDC;       // +0x08 m_hAttribDC
    BOOL printing;      // +0x0C m_bPrinting

    static void* Vtable() { return ei::GamePointer<void>(0x007461B0); }

    CDC* Construct();                                               // 00708CD7 (returns this)
    // 00708D79: the DC object of `dc` (a CTempDC when it has no permanent one), null for null.
    static CDC* __stdcall FromHandle(HDC dc);

    // SelectObject(CPen*) 00708F5A and SelectObject(CFont*) 00708FE6 (virtual slot 10), one body:
    // selected into both DCs; the attribute DC's old object (the output DC's when there is no
    // attribute DC) through CGdiObject::FromHandle.
    CGdiObject* SelectObject(CGdiObject* object);
    COLORREF SetTextColor(COLORREF colour);                         // 0070917A (virtual slot 12)
    int DrawText(const char* text, int length, RECT* rect, UINT format);   // 006E6A1C (virtual slot 26)
    int SetBkMode(int mode);                                        // 007090C2
    POINT MoveTo(int x, int y);                                     // 00709613
    BOOL LineTo(int x, int y);                                      // 0070965F
    void FillSolidRect(const RECT* rect, COLORREF colour);          // 0070BADA
    void FillSolidArea(int x, int y, int cx, int cy, COLORREF colour);  // 0070BB04 FillSolidRect(x, y, cx, cy, colour)
};
static_assert(sizeof(CDC) == 0x10);

// CTempDC's create function (its CRuntimeClass at 00745FB0): a CDC from its fixed allocator
// (007C8D18) with CTempDC's vtable (007463E4). Null when out of memory.
void* __stdcall CreateTempDC();                 // 0070BFD7

} // namespace mfc::impl
