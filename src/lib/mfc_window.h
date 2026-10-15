#pragma once

// MFC 4.2's AfxMessageBox as the game reaches it (ticket S8, docs/notes/standalone.md section 2).
// The game constructs no CWinApp (CWinApp's constructor 0070D654 has no caller) and has no MFC
// window, so the module state's application is always null: AfxMessageBox runs
// CWinApp::DoMessageBox with no object, EnableModeless and CWnd::GetSafeOwner_ find no window, and
// the box has no owner, no help context and the module's file name as its title. That is the body
// here (registered in mfc_window.cpp; tests/verify/verify_mfc_window.cpp checks the assumption
// against the original's state whenever a box is shown).
//
// CWnd's destructor (S3d): the exit functions run it on MFC's four static windows (wndTop,
// wndBottom, wndTopMost, wndNoTopMost: placement constants, never destroyed as windows); MFC's other
// classes reach it from their own destructors. Checked by tests/verify/verify_mfc_state.cpp.

#include <windows.h>

namespace mfc::impl {

// COleDispatchImpl's virtual slots (IDispatch's seven, then Disconnect); declared only.
class __declspec(novtable) COleDispatchImpl {
public:
    virtual void QueryInterface();      // 0
    virtual void AddRef();              // 1
    virtual void Release();             // 2
    virtual void GetTypeInfoCount();    // 3
    virtual void GetTypeInfo();         // 4
    virtual void GetIDsOfNames();       // 5
    virtual void Invoke();              // 6
    virtual void Disconnect();          // 7
};

// CCmdTarget (0x1C bytes).
struct CCmdTarget {
    void* vtable;               // +0x00
    long refCount;              // +0x04 m_dwRef
    void* outerUnknown;         // +0x08 m_pOuterUnknown
    void* innerUnknownVtable;   // +0x0C m_xInnerUnknown
    void* dispatchVtable;       // +0x10 m_xDispatch (a COleDispatchImpl once automation is enabled)
    BOOL resultExpected;        // +0x14 m_bResultExpected
    void* connectionPoints;     // +0x18 m_xConnPtContainer

    // 00703EC8: an enabled dispatch disconnected (COleDispatchImpl::Disconnect, slot 7).
    void Destruct();
};
static_assert(sizeof(CCmdTarget) == 0x1C);

struct CWnd;

// COleControlSite (the members used here).
struct COleControlSite {
    CCmdTarget base;            // +0x00
    void* container;            // +0x1C m_pCtrlCont
    HWND window;                // +0x20 m_hWnd
    CWnd* windowControl;        // +0x24 m_pWndCtrl
};

// CWnd (0x3C bytes, vtable 00746DA8).
struct CWnd {
    CCmdTarget base;            // +0x00
    HWND window;                // +0x1C m_hWnd
    HWND owner;                 // +0x20 m_hWndOwner
    unsigned flags;             // +0x24 m_nFlags
    WNDPROC superProc;          // +0x28 m_pfnSuper
    int modalResult;            // +0x2C m_nModalResult
    void* dropTarget;           // +0x30 m_pDropTarget
    void* controlContainer;     // +0x34 m_pCtrlCont (a COleControlContainer, a CObject)
    COleControlSite* controlSite;   // +0x38 m_pCtrlSite

    static void* Vtable() { return reinterpret_cast<void*>(0x00746DA8); }

    // 00704FE3: a window still attached destroyed (CWnd::DestroyWindow, at its address), except for the
    // four static windows; the control container deleted; the control site let go of this window; then
    // CCmdTarget's destructor. The original's unwind frame (CCmdTarget destroyed when DestroyWindow
    // throws) is not kept: no game window is a CWnd.
    void Destruct();
};
static_assert(sizeof(CWnd) == 0x3C);

// AfxMessageBox(LPCTSTR, UINT type, UINT helpId): MessageBoxA's answer. MB_ICONEXCLAMATION is added
// to an OK, OK / Cancel, Yes / No / Cancel or Yes / No box that names no icon.
int __stdcall MessageBoxText(const char* text, unsigned type, unsigned helpId);   // 0070AC94

} // namespace mfc::impl
