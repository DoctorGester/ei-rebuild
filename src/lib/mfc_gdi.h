#pragma once

// MFC 4.2's CDC and GDI objects as the game's copy has them (wingdi.cpp, wingdix.cpp, winhand.cpp,
// afxwin1.inl): tickets(the members that reach GDI without a handle map) and(FromHandle,
// SelectObject, CGdiObject's Attach / Detach / DeleteObject, CPen's constructor, CFont's point
// fonts, the CDC slots the interface calls, docs/notes/standalone.md section 2). Registered at their
// library addresses (mfc_gdi.cpp); real C++ classes over CObject since. The interface (ui_gdi.h) draws with
// them directly.
// The handle maps are MFC's module thread state: the hook build uses the original's (afxMapHDC
// afxMapHGDIOBJ, through their addresses), the image has its own per thread
// (MapHDC / MapHGDIOBJ, registered image-only at those addresses).

#include "compiled_vtable.h"   // ei::UnportedSlot
#include "mfc_handle_map.h"
#include "mfc_object.h"
#include "mfc_string.h"
#include "static_object.h"
#include "game/game_data.h"

#include <windows.h>

namespace mfc::impl {

// The maps of DCs (CTempDC objects for unknown handles, both handles set) and of GDI objects
// (CTempGdiObject), made on first use when `create`; null otherwise. Image only.
CHandleMap* __stdcall MapHDC(BOOL create);         // afxMapHDC
CHandleMap* __stdcall MapHGDIOBJ(BOOL create);     // afxMapHGDIOBJ

// CGdiObject (8 bytes): CObject's slots, m_hObject. Game.exe's tables: CGdiObject, CBrush, CPen
// CTempGdiObject, CFont (0 GetRuntimeClass, 1 the deleting destructor, 2 Serialize).
class CGdiObject : public CObject {
public:
    // CreateCGdiObject's: no handle.
    CGdiObject() = default;
    // ~CGdiObject as the game's code inlines it the fonts' destructors; deleting
    // DeleteObject. The classes add no destructor of their own.
    ~CGdiObject() override;
    const CRuntimeClass* GetRuntimeClass() const override;

    // The object of `handle` (a CTempGdiObject when it has no permanent one), null for null.
    static CGdiObject* __stdcall FromHandle(HGDIOBJ handle);
    BOOL Attach(HGDIOBJ object);        // FALSE for null; else kept and put in the permanent map
    HGDIOBJ Detach();                   // taken out of the permanent map; the old handle
    BOOL DeleteObject();                // FALSE without a handle; else detached and deleted

    HGDIOBJ handle = nullptr;   // +0x04 m_hObject
};
static_assert(sizeof(CGdiObject) == 8);

// CTempGdiObject's create function (its CRuntimeClass): one from its fixed allocator
// no handle. Null when out of memory.
void* __stdcall CreateTempGdiObject();

// CBrush: the interface's brushes (a surface's frame).
class CBrush final : public CGdiObject {
public:
    const CRuntimeClass* GetRuntimeClass() const override;
};

// CPen.
class CPen final : public CGdiObject {
public:
    // CPen(style, width, colour): CreatePen attached; a CResourceException when it fails.
    CPen(int style, int width, COLORREF colour);
    const CRuntimeClass* GetRuntimeClass() const override;
};

class CDC;

// CFont: the game's fonts (the debug panel's, the interface's and the console's), made with new.
class CFont final : public CGdiObject {
public:
    const CRuntimeClass* GetRuntimeClass() const override;
    // A font of `pointSize` tenths of a point, the face name and DEFAULT_CHARSET, through
    // CreatePointFontIndirect.
    BOOL CreatePointFont(int pointSize, const char* faceName, CDC* dc);
    // The font with its height in tenths of a point turned into logical units of `dc`'s
    // attribute DC (the screen's when null), attached.
    BOOL CreatePointFontIndirect(const LOGFONTA* font, CDC* dc);
};

// CDC (0x10 bytes): the output DC and the attribute DC (the same one for a plain DC). Game.exe's tables: CDC,
// CTempDC, 29 slots (MFC 4.2's CDC virtuals in declaration order). The ones the image reaches have bodies:
// the interface calls 10 / 12 / 26 of its surfaces' CTempDCs, a handle map deletes them through 1, Detach calls 5;
// 0, 2-4 and 6 are as short. The rest are unported (config/standalone_unreachable.csv).
class CDC : public CObject {
public:
    // No DCs.
    CDC() = default;
    // A DC still attached detached and deleted.
    ~CDC() override;
    const CRuntimeClass* GetRuntimeClass() const override;
    virtual void SetAttribDC(HDC dc);
    virtual void SetOutputDC(HDC dc);
    virtual void ReleaseAttribDC();
    virtual void ReleaseOutputDC();
    virtual int SaveDC() { ei::UnportedSlot(0x00708E98); }                                          // 7
    virtual BOOL RestoreDC(int) { ei::UnportedSlot(0x00708EC9); }                                   // 8
    virtual CGdiObject* SelectStockObject(int) { ei::UnportedSlot(0x00708F1E); }                    // 9
    // 10: SelectObject(CFont*), SelectObject's body.
    virtual CGdiObject* SelectFont(CGdiObject* font) { return SelectObject(font); }
    virtual COLORREF SetBkColor(COLORREF) { ei::UnportedSlot(0x00709093); }                         // 11
    virtual COLORREF SetTextColor(COLORREF colour);
    virtual int SetMapMode(int) { ei::UnportedSlot(0x007091A9); }                                   // 13
    virtual POINT SetViewportOrg(int, int) { ei::UnportedSlot(0x007091D7); }                        // 14
    virtual POINT OffsetViewportOrg(int, int) { ei::UnportedSlot(0x00709223); }                     // 15
    virtual SIZE SetViewportExt(int, int) { ei::UnportedSlot(0x0070926F); }                         // 16
    virtual SIZE ScaleViewportExt(int, int, int, int) { ei::UnportedSlot(0x007092BB); }             // 17
    virtual SIZE SetWindowExt(int, int) { ei::UnportedSlot(0x007093AB); }                           // 18
    virtual SIZE ScaleWindowExt(int, int, int, int) { ei::UnportedSlot(0x007093F7); }               // 19
    virtual int GetClipBox(RECT*) const { ei::UnportedSlot(0x0070944F); }                           // 20
    virtual BOOL PtVisible(int, int) const { ei::UnportedSlot(0x006E697F); }                        // 21
    virtual BOOL RectVisible(const RECT*) const { ei::UnportedSlot(0x006E6993); }                   // 22
    virtual BOOL TextOut(int, int, const char*, int) { ei::UnportedSlot(0x006E69A3); }              // 23
    virtual BOOL ExtTextOut(int, int, UINT, const RECT*, const char*, UINT, int*) {                 // 24
        ei::UnportedSlot(0x006E69BF);
    }
    virtual SIZE TabbedTextOut(int, int, const char*, int, int, int*, int) { ei::UnportedSlot(0x006E69E4); }   // 25
    // The interface's, the console's and the debug panel's text (EI_UTF8: UTF-8, drawn by the wide call
    // behind DrawTextA's import slot: system_text.h).
    virtual int DrawText(const char* text, int length, RECT* rect, UINT format);
    virtual BOOL GrayString(CBrush*, GRAYSTRINGPROC, LPARAM, int, int, int, int, int) { ei::UnportedSlot(0x006E6A38); }   // 27
    virtual int Escape(int, int, const char*, void*) { ei::UnportedSlot(0x006E6A68); }              // 28

    // The output DC taken out of the permanent map and cleared, the attribute DC released;
    // returns the old output DC.
    HDC Detach();
    // The DC object of `dc` (a CTempDC when it has no permanent one), null for null.
    static CDC* __stdcall FromHandle(HDC dc);

    // SelectObject(CPen*): selected into both DCs; the attribute DC's old object (the output DC's when
    // there is no attribute DC) through CGdiObject::FromHandle.
    CGdiObject* SelectObject(CGdiObject* object);
    int SetBkMode(int mode);
    POINT MoveTo(int x, int y);
    BOOL LineTo(int x, int y);
    void FillSolidRect(const RECT* rect, COLORREF colour);
    void FillSolidArea(int x, int y, int cx, int cy, COLORREF colour);  // FillSolidRect(x, y, cx, cy, colour)

    HDC hdc = nullptr;          // +0x04 m_hDC
    HDC attribDC = nullptr;     // +0x08 m_hAttribDC
    BOOL printing = FALSE;      // +0x0C m_bPrinting
};
static_assert(sizeof(CDC) == 0x10);

// CTempDC's create function (its CRuntimeClass): a CTempDC from its fixed allocator. Null
// when out of memory.
void* __stdcall CreateTempDC();

// The fixed allocators of the temporary DCs (0x10-byte blocks) and GDI objects (8), 64 to a block (mfc_gdi.cpp): built
// and destroyed by game.exe's static initialisers and their guarded exit functions, which the generated
// static_init.cpp / static_exit.cpp run on these names (tools/gen_static_init.py's GENERATED_NAMES).
extern lib::StaticObject<CFixedAlloc> g_tempDCAlloc;
extern lib::StaticObject<CFixedAlloc> g_tempGdiObjectAlloc;

} // namespace mfc::impl
