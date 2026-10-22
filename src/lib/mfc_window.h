#pragma once

// MFC 4.2's AfxMessageBox as the game reaches it (ticket S8, docs/notes/standalone.md section 2).
// The game constructs no CWinApp (CWinApp's constructor 0070D654 has no caller) and has no MFC
// window, so the module state's application is always null: AfxMessageBox runs
// CWinApp::DoMessageBox with no object, EnableModeless and CWnd::GetSafeOwner_ find no window, and
// the box has no owner, no help context and the module's file name as its title. That is the body
// here (registered in mfc_window.cpp; verify_mfc_window.cpp checked the assumption
// against the original's state whenever a box was shown, until S21d).
//
// CWnd's constructor (S3f) and destructor (S3d): the exit functions run it on MFC's four static windows (wndTop,
// wndBottom, wndTopMost, wndNoTopMost: placement constants, never destroyed as windows); MFC's other
// classes reach it from their own destructors. Checked in game by verify_mfc_state.cpp until S21d.

#include <windows.h>
#include "compiled_vtable.h"
#include "game/game_data.h"

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

    // 00703EAE: one reference, no outer unknown or dispatch, a result expected; no vtable written
    // (returns this).
    CCmdTarget* Construct();
    // 00703EC8: an enabled dispatch disconnected (COleDispatchImpl::Disconnect, slot 7).
    void Destruct();
};
static_assert(sizeof(CCmdTarget) == 0x1C);

struct CRuntimeClass;
struct CWnd;

// CCmdTarget's 20 slots as CWnd's and CWinThread's compiled tables have them (C19; MFC 4.2's CCmdTarget virtuals):
// 0 GetRuntimeClass, 1 the scalar deleting destructor, 2 Serialize (CObject's, empty), 3-19 the command routing
// and OLE automation slots, unported: only MFC's own message routing and IDispatch call them, none of which the
// image has (config/standalone_unreachable.csv). 4 and 10 are the class's own.
template <typename Self, const CRuntimeClass* kClass>
struct CmdTargetTable : ei::CompiledTag<Self> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return kClass; }                 // 0
    virtual void* DeleteScalar(unsigned flags) = 0;                                         // 1
    virtual void Serialize(void*) {}                                                        // 2: 00444B00
    virtual BOOL OnCmdMsg(UINT, int, void*, void*) { ei::UnportedSlot(0x00703FFA); }       // 3
    virtual void OnFinalRelease() = 0;                                                      // 4
    virtual BOOL IsInvokeAllowed(long) { ei::UnportedSlot(0x00704109); }                   // 5
    virtual BOOL GetDispatchIID(IID*) { ei::UnportedSlot(0x0070410F); }                    // 6
    virtual UINT GetTypeInfoCount() { ei::UnportedSlot(0x00704114); }                      // 7
    virtual void* GetTypeLibCache() { ei::UnportedSlot(0x00704117); }                      // 8
    virtual HRESULT GetTypeLib(LCID, void**) { ei::UnportedSlot(0x0070411A); }             // 9
    virtual const void* GetMessageMap() const = 0;                                          // 10
    virtual const void* GetCommandMap() const { ei::UnportedSlot(0x007041C4); }            // 11
    virtual const void* GetDispatchMap() const { ei::UnportedSlot(0x00704167); }           // 12
    virtual const void* GetConnectionMap() const { ei::UnportedSlot(0x007041B4); }         // 13
    virtual const void* GetInterfaceMap() const { ei::UnportedSlot(0x00704173); }          // 14
    virtual const void* GetEventSinkMap() const { ei::UnportedSlot(0x0070416D); }          // 15
    virtual BOOL OnCreateAggregates() { ei::UnportedSlot(0x007041AB); }                    // 16
    virtual void* GetInterfaceHook(const void*) { ei::UnportedSlot(0x007041AF); }          // 17
    virtual BOOL GetExtraConnectionPoints(void*) { ei::UnportedSlot(0x007041BF); }         // 18
    virtual void* GetConnectionHook(const IID&) { ei::UnportedSlot(0x007041BA); }          // 19
};

// COleControlSite (the members used here).
struct COleControlSite {
    CCmdTarget base;            // +0x00
    void* container;            // +0x1C m_pCtrlCont
    HWND window;                // +0x20 m_hWnd
    CWnd* windowControl;        // +0x24 m_pWndCtrl
};

// CWnd (0x3C bytes). Its table and CTempWnd's are compiled (C19, mfc_window.cpp's WndTable) in place of game.exe's
// 00746DA8 / 00746EC4: 46 slots, the destructors ported, the rest unported (MFC's four static windows are never
// called through their table).
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

    static void* Vtable();
    static void* TempVtable();   // CTempWnd's

    // 00704546 CWnd(HWND): CCmdTarget's constructor, CWnd's vtable, CWnd's members cleared and the
    // handle kept (returns this). The static initialisers build MFC's four static windows with it
    // (wndTop 0, wndBottom 1, wndTopMost -1, wndNoTopMost -2); CWnd's default constructor (007068AA)
    // calls it too.
    CWnd* Construct(HWND handle);
    // 00704FE3: a window still attached destroyed (CWnd::DestroyWindow, at its address), except for the
    // four static windows; the control container deleted; the control site let go of this window; then
    // CCmdTarget's destructor. The original's unwind frame (CCmdTarget destroyed when DestroyWindow
    // throws) is not kept: no game window is a CWnd.
    void Destruct();
    // 0070452A (slot 1): Destruct, then the memory freed when bit 0 of `deleteFlags` is set.
    CWnd* DeletingDestructor(unsigned deleteFlags);
};
static_assert(sizeof(CWnd) == 0x3C);

// CMenu (8 bytes): vtable, m_hMenu. Its table and CTempMenu's are compiled (C19, mfc_window.cpp's MenuTable) in
// place of game.exe's 007471FC / 00747214; their create functions are the only makers, and nothing calls those.
struct CMenu {
    void* vtable;
    HMENU menu;         // +0x04 m_hMenu

    static void* Vtable();
    static void* TempVtable();   // CTempMenu's
};
static_assert(sizeof(CMenu) == 8);

// AfxMessageBox(LPCTSTR, UINT type, UINT helpId): MessageBoxA's answer. MB_ICONEXCLAMATION is added
// to an OK, OK / Cancel, Yes / No / Cancel or Yes / No box that names no icon.
int __stdcall MessageBoxText(const char* text, unsigned type, unsigned helpId);   // 0070AC94
// AfxMessageBox(UINT id, UINT type, UINT helpId): the box with string `id` of game.exe's table (empty
// when it is missing; the original only traces that). A help id of -1 means `id`.
int __stdcall MessageBoxId(unsigned promptId, unsigned type, unsigned helpId);    // 0070ACCC

} // namespace mfc::impl
