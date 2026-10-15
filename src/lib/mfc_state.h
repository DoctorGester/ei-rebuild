#pragma once

// MFC 4.2's thread and process locals (afxtls.cpp) and its system metrics record AUX_DATA (afxData,
// auxdata.cpp), as far as the exe's static initialisers and exit functions reach them (ticket S3d,
// docs/notes/standalone.md). The thread and process locals are globals MFC fills on first use
// (_afxThreadState, _afxBaseModuleState and the others: their CThreadLocal / CProcessLocal holds a
// TLS slot or an object); the exit functions free them through the destructors here. afxData is
// built by a static initialiser and its menu-dot bitmap deleted at exit.
//
// Every body is registered at its library address in both builds (mfc_state.cpp). The hook build
// runs them over the original's state; in the image nothing fills the locals (S8a's handle maps are
// thread_local, mfc_gdi.cpp), so their destructors find an empty slot or object. Checked by
// tests/verify/verify_mfc_state.cpp.

#include <cstddef>
#include <cstdint>

#include <windows.h>

namespace mfc::impl {

// CNoTrackObject: what the locals hold; deleted through its virtual destructor (slot 0, with 1),
// which frees it with LocalFree. Declared only.
class __declspec(novtable) CNoTrackObject {
public:
    virtual void* DeletingDestructor(unsigned flags);   // 0
};

// CThreadData (a CNoTrackObject): one thread's slot values.
struct CThreadData {
    void* vtable;               // +0x00
    CThreadData* next;          // +0x04 pNext (CThreadSlotData's list)
    int count;                  // +0x08 nCount (values allocated for slots below it)
    CNoTrackObject** values;    // +0x0C pData (index: the slot)
};

// CSlotData: a slot's flags (bit 0: in use) and the module that allocated it.
struct CSlotData {
    DWORD flags;                // +0x00 dwFlags
    HINSTANCE instance;         // +0x04 hInst
};

// CThreadSlotData (_afxThreadData, made on first use): the TLS slots of every thread.
struct CThreadSlotData {
    enum : DWORD { kSlotUsed = 1 };

    DWORD tlsIndex;             // +0x00 m_tlsIndex
    int allocated;              // +0x04 m_nAlloc
    int rover;                  // +0x08 m_nRover
    int max;                    // +0x0C m_nMax
    CSlotData* slots;           // +0x10 m_pSlotData
    CThreadData* threads;       // +0x14 m_list.m_pHead
    std::size_t nextOffset;     // +0x18 m_list.m_nNextOffset
    CRITICAL_SECTION section;   // +0x1C m_sect

    // 0070C71F (RET 4): every thread's value of `slot` deleted and cleared, then the slot no longer in
    // use; under the critical section.
    void FreeSlot(int slot);
};
static_assert(offsetof(CThreadSlotData, section) == 0x1C);

// _afxThreadData (null until a thread local is first read).
CThreadSlotData*& AfxThreadData();

// CThreadLocalObject (CThreadLocal<T> adds no members): its TLS slot, 0 when none was allocated.
struct CThreadLocalObject {
    int slot;                   // +0x00 m_nSlot

    // 0070CA59 (and the CThreadLocal<T> destructors 0070CC2D, 0070D5F9 that jump to it): the slot freed
    // when there is one and _afxThreadData exists, then the slot cleared.
    void Destruct();
};

// CProcessLocalObject (CProcessLocal<T> adds no members): the object, made on first use.
struct CProcessLocalObject {
    CNoTrackObject* object;     // +0x00 m_pObject

    // 0070CAD2 (and the CProcessLocal<T> destructors 0070CE77, 0070D61F, 0070DC48 that jump to it): the
    // object deleted; the pointer stays.
    void Destruct();
};

// AfxDeleteObject (0070AA54, stdcall): the GDI object `*object` deleted and the handle cleared, when
// there is one.
void __stdcall AfxDeleteObject(HGDIOBJ* object);

// AUX_DATA (0x6C bytes): afxData 007C90C8.
struct AuxData {
    int vScrollWidth;           // +0x00 cxVScroll
    int hScrollHeight;          // +0x04 cyHScroll
    int iconWidth;              // +0x08 cxIcon
    int iconHeight;             // +0x0C cyIcon
    int borderWidth2;           // +0x10 cxBorder2
    int borderHeight2;          // +0x14 cyBorder2
    int pixelsPerInchX;         // +0x18 cxPixelsPerInch
    int pixelsPerInchY;         // +0x1C cyPixelsPerInch
    HBRUSH windowFrameBrush;    // +0x20 hbrWindowFrame
    HBRUSH buttonFaceBrush;     // +0x24 hbrBtnFace
    COLORREF buttonFace;        // +0x28 clrBtnFace
    COLORREF buttonShadow;      // +0x2C clrBtnShadow
    COLORREF buttonHighlight;   // +0x30 clrBtnHilite
    COLORREF buttonText;        // +0x34 clrBtnText
    COLORREF windowFrame;       // +0x38 clrWindowFrame
    HCURSOR waitCursor;         // +0x3C hcurWait
    HCURSOR arrowCursor;        // +0x40 hcurArrow
    HCURSOR helpCursor;         // +0x44 hcurHelp
    HFONT statusFont;           // +0x48 hStatusFont (never written here)
    HFONT toolTipsFont;         // +0x4C hToolTipsFont (never written here)
    HBITMAP menuDot;            // +0x50 hbmMenuDot
    unsigned version;           // +0x54 nWinVer (major << 8 | minor)
    BOOL win95;                 // +0x58 bWin95 (GetVersion's high bit)
    BOOL win4;                  // +0x5C bWin4 (major version 4 or later)
    BOOL notWin4;               // +0x60 bNotWin4
    BOOL smallCaption;          // +0x64 bSmCaption (= win4)
    BOOL marked4;               // +0x68 bMarked4 (the process is marked 4.0; picks the scroll metrics)

    // 0070D1AA: the version flags, the metrics, the colours and the two standard cursors; returns this.
    AuxData* Construct();
    // 0070E181: the menu-dot bitmap deleted (AfxDeleteObject).
    void Destruct();
    // 00703E12: the button and frame colours and their brushes.
    void UpdateSysColors();
    // 00703E56: the icon size, the scroll metrics (by marked4), the screen's pixels per inch.
    void UpdateSysMetrics();
    // 0070D158 / 0070D188 (cdecl, on afxData in the original): the scroll bar sizes. Marked 4.0: only on
    // Windows 4, one pixel larger, marked4 set; otherwise as the system has them, marked4 cleared.
    void UpdateScrollMetricsMarked4();
    void UpdateScrollMetrics();
};
static_assert(sizeof(AuxData) == 0x6C);

// afxData.
AuxData& AfxData();

} // namespace mfc::impl
