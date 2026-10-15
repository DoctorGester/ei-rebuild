#pragma once

// MFC 4.2's static data that the static initialisers build (mfc_module.cpp, ticket S16): the
// runtime-class registration (AFX_CLASSINIT), afxData (AUX_DATA), the four static CWnds (wndTop,
// wndBottom, wndTopMost, wndNoTopMost), the DBCS flag and the GDI terminator's registration. The
// hook build keeps the original's module state; the image keeps a class list of its own.

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace mfc::impl {

// CRuntimeClass (statically linked MFC: the base class by pointer, 0x18 bytes).
struct CRuntimeClass {
    const char* className;        // +0x00
    int objectSize;               // +0x04
    unsigned schema;              // +0x08
    void* createObject;           // +0x0C
    CRuntimeClass* baseClass;     // +0x10
    CRuntimeClass* nextClass;     // +0x14 the module's class list
};
static_assert(sizeof(CRuntimeClass) == 0x18 && offsetof(CRuntimeClass, nextClass) == 0x14);

// CSimpleList: the head and the offset of each element's next link.
struct SimpleList {
    void* head;
    std::size_t nextOffset;

    // 0070C4E9 (thiscall, RET 4): `p`'s link to the old head, `p` the new head.
    void AddHead(void* p) {
        *reinterpret_cast<void**>(static_cast<unsigned char*>(p) + nextOffset) = head;
        head = p;
    }
};

// The image's module class list (AFX_MODULE_STATE::m_classList; empty until the initialisers run).
SimpleList& ImageClassList();

// 00700674 AfxClassInit (stdcall, RET 4): the class at the head of the module's class list (under
// the runtime-class lock, which the image's single-threaded start does not need). Image only.
void __stdcall AfxClassInit(CRuntimeClass* newClass);

// AUX_DATA (afxData, 007C90C8): system metrics, colours and cursors MFC caches.
struct AuxData {
    int cxVScroll, cyHScroll;               // +0x00
    int cxIcon, cyIcon;                     // +0x08
    int cxBorder2, cyBorder2;               // +0x10
    int cxPixelsPerInch, cyPixelsPerInch;   // +0x18
    HBRUSH hbrWindowFrame;                  // +0x20
    HBRUSH hbrBtnFace;                      // +0x24
    COLORREF clrBtnFace, clrBtnShadow, clrBtnHilite;   // +0x28
    COLORREF clrBtnText, clrWindowFrame;    // +0x34
    HCURSOR hcurWait;                       // +0x3C
    HCURSOR hcurArrow;                      // +0x40
    HCURSOR hcurHelp;                       // +0x44
    HFONT hStatusFont;                      // +0x48 made on demand (the constructor leaves it)
    HFONT hToolTipsFont;                    // +0x4C made on demand (the constructor leaves it)
    HBITMAP hbmMenuDot;                     // +0x50
    int nWinVer;                            // +0x54 major << 8 | minor
    BOOL bWin95;                            // +0x58 the version's top bit
    BOOL bWin4;                             // +0x5C major version 4 or later
    BOOL bNotWin4;                          // +0x60
    BOOL bSmCaption;                        // +0x64
    BOOL bMarked4;                          // +0x68 the process marked for version 4

    // 0070D1AA (thiscall, returns this).
    AuxData* Construct();
    // 00703E12: the colours and the two brushes.
    void UpdateSysColors();
    // 00703E56: the icon size, the scroll bar sizes (through 0070D158 / 0070D188) and the screen's
    // pixels per inch.
    void UpdateSysMetrics();
};
static_assert(sizeof(AuxData) == 0x6C && offsetof(AuxData, nWinVer) == 0x54);
inline AuxData& AfxData() { return *reinterpret_cast<AuxData*>(0x007C90C8); }

// 0070D158 / 0070D188 (no arguments; they write afxData itself): the scroll bar sizes with a border
// (version 4, only when afxData.bWin4 is set; bMarked4 set) or without (bMarked4 cleared).
void __cdecl SetScrollMetrics4();
void __cdecl SetScrollMetrics3();

// CCmdTarget's fields after its vtable (0x1C bytes in all).
struct CCmdTarget {
    void* vtable;
    DWORD dwRef;                  // +0x04 m_dwRef
    void* outerUnknown;           // +0x08 m_pOuterUnknown
    DWORD innerUnknown;           // +0x0C m_xInnerUnknown
    void* dispatchVtable;         // +0x10 m_xDispatch.m_vtbl
    BOOL resultExpected;          // +0x14 m_bResultExpected
    void* connPtContainerVtable;  // +0x18 m_xConnPtContainer.m_vtbl

    // 00703EAE (thiscall, returns this): no vtable written.
    CCmdTarget* Construct();
};
static_assert(sizeof(CCmdTarget) == 0x1C);

// CWnd as far as its constructors write it (0x3C bytes).
struct CWnd {
    CCmdTarget base;
    HWND hWnd;                    // +0x1C m_hWnd
    std::uint32_t members[7];     // +0x20 the rest the constructor zeroes (m_bEnableActiveAccessibility ...)

    static void* Vtable() { return reinterpret_cast<void*>(0x00746DA8); }
    // 00704546 (thiscall, RET 4, returns this): CWnd(HWND).
    CWnd* Construct(HWND window);
};
static_assert(sizeof(CWnd) == 0x3C && offsetof(CWnd, hWnd) == 0x1C);

// 0070DC4D (cdecl): whether the OEM code page has multibyte characters (MaxCharSize above 1);
// stored by its initialiser in 007CAA98.
BOOL __cdecl OemCodePageIsDbcs();

// 0070AA54 AfxDeleteObject (stdcall, RET 4): the object deleted and the handle cleared, if set.
void __stdcall AfxDeleteObject(HGDIOBJ* object);
// 0070B5F0 (cdecl, the exit function): AfxDeleteObject on the GDI handle at 007C8D40.
void __cdecl AfxGdiTerm();
// 0070B600 (cdecl, a static initialiser): AfxGdiTerm registered with atexit, its result kept in
// the byte at 007C8D44.
void __cdecl AfxGdiTermInit();

} // namespace mfc::impl
