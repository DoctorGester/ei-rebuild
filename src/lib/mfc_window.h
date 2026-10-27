#pragma once

// MFC 4.2's AfxMessageBox as the game reaches it (ticket S8, docs/notes/standalone.md section 2).
// The game constructs no CWinApp (CWinApp's constructor 0070D654 has no caller) and has no MFC
// window, so the module state's application is always null: AfxMessageBox runs
// CWinApp::DoMessageBox with no object, EnableModeless and CWnd::GetSafeOwner_ find no window, and
// the box has no owner, no help context and the module's file name as its title. That is the body
// here (registered in mfc_window.cpp; verify_mfc_window.cpp checked the assumption
// against the original's state whenever a box was shown, until S21d).
// CWnd's constructor and destructor: the exit functions run it on MFC's four static windows (wndTop,
// wndBottom, wndTopMost, wndNoTopMost: placement constants, never destroyed as windows); MFC's other
// classes reach it from their own destructors. Checked in game by verify_mfc_state.cpp until S21d.

#include <windows.h>
#include "compiled_vtable.h"   // ei::UnportedSlot
#include "mfc_object.h"
#include "mfc_string.h"
#include "static_object.h"
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

class CWnd;

// CCmdTarget (0x1C bytes): CObject's three slots, then MFC 4.2's CCmdTarget virtuals (3-19: the command routing and
// OLE automation slots, unported: only MFC's own message routing and IDispatch call them, none of which the image
// has; config/standalone_unreachable.csv). 4 and 10 are each class's own. CWnd and CWinThread derive from it.
class CCmdTarget : public CObject {
public:
    // One reference, no outer unknown or dispatch, a result expected.
    CCmdTarget();
    // An enabled dispatch disconnected (COleDispatchImpl::Disconnect, slot 7).
    ~CCmdTarget() override;

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

    long refCount;              // +0x04 m_dwRef
    void* outerUnknown;         // +0x08 m_pOuterUnknown
    void* innerUnknownVtable;   // +0x0C m_xInnerUnknown
    void* dispatchVtable;       // +0x10 m_xDispatch (a COleDispatchImpl once automation is enabled)
    BOOL resultExpected;        // +0x14 m_bResultExpected
    void* connectionPoints;     // +0x18 m_xConnPtContainer
};
static_assert(sizeof(CCmdTarget) == 0x1C);

// COleControlSite (the members used here; the image makes none).
class COleControlSite : public CCmdTarget {
public:
    void* container;            // +0x1C m_pCtrlCont
    HWND window;                // +0x20 m_hWnd
    CWnd* windowControl;        // +0x24 m_pWndCtrl
};

// CWnd (0x3C bytes). Game.exe's tables: CWnd 00746DA8, CTempWnd 00746EC4 (mfc_window.cpp): 46 slots (MFC 4.2's CWnd
// virtuals in declaration order), all unported but the destructor (the image makes MFC's four static windows only,
// never called through their table).
class CWnd : public CCmdTarget {
public:
    // CWnd(HWND) with no window.
    CWnd() : CWnd(nullptr) {}
    // 00704546 CWnd(HWND): CCmdTarget's constructor, CWnd's members cleared and the handle kept.
    explicit CWnd(HWND handle);
    // The constructor on `this` as the static initialisers reach it at its address (thiscall, RET 4), building
    // MFC's four static windows (wndTop 0, wndBottom 1, wndTopMost -1, wndNoTopMost -2). Returns this.
    CWnd* Construct(HWND handle) { return new (this) CWnd(handle); }
    // 00704FE3 (deleting 0070452A): a window still attached destroyed (CWnd::DestroyWindow, unported), except for the
    // four static windows; the control container deleted; the control site let go of this window; then CCmdTarget's
    // destructor. The original's unwind frame (CCmdTarget destroyed when DestroyWindow throws) is not kept: no game
    // window is a CWnd.
    ~CWnd() override;

    const CRuntimeClass* GetRuntimeClass() const override;
    void OnFinalRelease() override { ei::UnportedSlot(0x00705190); }                                 // 4
    const void* GetMessageMap() const override { ei::UnportedSlot(0x0070588F); }                     // 10
    virtual void PreSubclassWindow() { ei::UnportedSlot(0x007049D5); }                               // 20
    virtual BOOL Create(const char*, const char*, DWORD, const RECT*, CWnd*, UINT, void*) {          // 21
        ei::UnportedSlot(0x00704F97);
    }
    virtual BOOL DestroyWindow() { ei::UnportedSlot(0x007051A1); }                                   // 22
    virtual BOOL PreCreateWindow(CREATESTRUCTA*) { ei::UnportedSlot(0x00704F77); }                   // 23
    virtual void CalcWindowRect(RECT*, UINT) { ei::UnportedSlot(0x0070674E); }                       // 24
    virtual int OnToolHitTest(POINT, void*) const { ei::UnportedSlot(0x007052AF); }                  // 25
    virtual CWnd* GetScrollBarCtrl(int) const { ei::UnportedSlot(0x007062F0); }                      // 26
    virtual void WinHelp(DWORD, UINT) { ei::UnportedSlot(0x007057AD); }                              // 27
    virtual BOOL ContinueModal() { ei::UnportedSlot(0x007074D8); }                                   // 28
    virtual void EndModalLoop(int) { ei::UnportedSlot(0x007074DF); }                                 // 29
    virtual BOOL OnCommand(WPARAM, LPARAM) { ei::UnportedSlot(0x00705ED3); }                         // 30
    virtual BOOL OnNotify(WPARAM, LPARAM, LRESULT*) { ei::UnportedSlot(0x00705F5F); }                // 31
    virtual WNDPROC* GetSuperWndProcAddr() { ei::UnportedSlot(0x00705235); }                         // 32
    virtual void DoDataExchange(void*) { ei::UnportedSlot(0x006E7128); }                             // 33
    virtual void BeginModalState() { ei::UnportedSlot(0x006E712B); }                                 // 34
    virtual void EndModalState() { ei::UnportedSlot(0x006E7137); }                                   // 35
    virtual BOOL PreTranslateMessage(MSG*) { ei::UnportedSlot(0x00705239); }                         // 36
    virtual BOOL OnAmbientProperty(void*, long, void*) { ei::UnportedSlot(0x007037E1); }             // 37
    virtual LRESULT WindowProc(UINT, WPARAM, LPARAM) { ei::UnportedSlot(0x007058EA); }               // 38
    virtual BOOL OnWndMsg(UINT, WPARAM, LPARAM, LRESULT*) { ei::UnportedSlot(0x0070592E); }          // 39
    virtual LRESULT DefWindowProc(UINT, WPARAM, LPARAM) { ei::UnportedSlot(0x007051EE); }            // 40
    virtual void PostNcDestroy() { ei::UnportedSlot(0x0070518F); }                                   // 41
    virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*) { ei::UnportedSlot(0x0070694E); }     // 42
    virtual BOOL CheckAutoCenter() { ei::UnportedSlot(0x0070711E); }                                 // 43
    virtual BOOL IsFrameWnd() const { ei::UnportedSlot(0x00707877); }                                // 44
    virtual BOOL SetOccDialogInfo(void*) { ei::UnportedSlot(0x00707503); }                           // 45

    HWND window;                    // +0x1C m_hWnd
    HWND owner = nullptr;           // +0x20 m_hWndOwner
    unsigned flags = 0;             // +0x24 m_nFlags
    WNDPROC superProc = nullptr;    // +0x28 m_pfnSuper
    int modalResult = 0;            // +0x2C m_nModalResult
    void* dropTarget = nullptr;     // +0x30 m_pDropTarget
    CObject* controlContainer = nullptr;        // +0x34 m_pCtrlCont (a COleControlContainer)
    COleControlSite* controlSite = nullptr;     // +0x38 m_pCtrlSite
};
static_assert(sizeof(CWnd) == 0x3C);

// ~CWnd as the generated exit functions reach it at its address (00704FE3, thiscall) on MFC's four static windows.
void __fastcall DestroyStaticWindow(CWnd* window);

// CMenu (8 bytes): CObject's slots, 3 DrawItem and 4 MeasureItem (empty: 00707D0C / 00707D0F); m_hMenu. Game.exe's
// tables: CMenu 007471FC, CTempMenu 00747214 (mfc_window.cpp). Their create functions are the only makers, and nothing
// calls those.
class CMenu : public CObject {
public:
    CMenu() = default;
    // 006E7150 (deleting): the menu destroyed through MFC's menu handle map, which the image does not have; unported.
    ~CMenu() override;
    const CRuntimeClass* GetRuntimeClass() const override;
    virtual void DrawItem(DRAWITEMSTRUCT*) {}                // 3
    virtual void MeasureItem(MEASUREITEMSTRUCT*) {}          // 4

    HMENU menu = nullptr;   // +0x04 m_hMenu
};
static_assert(sizeof(CMenu) == 8);

// AfxMessageBox(LPCTSTR, UINT type, UINT helpId): MessageBoxA's answer. MB_ICONEXCLAMATION is added
// to an OK, OK / Cancel, Yes / No / Cancel or Yes / No box that names no icon.
int __stdcall MessageBoxText(const char* text, unsigned type, unsigned helpId);
// AfxMessageBox(UINT id, UINT type, UINT helpId): the box with string `id` of game.exe's table (empty
// when it is missing; the original only traces that). A help id of -1 means `id`.
int __stdcall MessageBoxId(unsigned promptId, unsigned type, unsigned helpId);

// The fixed allocators of the temporary windows (CTempWnd, 0x3C-byte blocks) and menus (CTempMenu, 8), 64 to a block,
// and MFC's four static windows wndTop, wndBottom, wndTopMost and wndNoTopMost (placement constants: their handles
// HWND_TOP 0, HWND_BOTTOM 1, HWND_TOPMOST -1, HWND_NOTOPMOST -2; mfc_window.cpp). Built and destroyed by game.exe's
// static initialisers and their guarded exit functions, which the generated static_init.cpp / static_exit.cpp run on
// these names (tools/gen_static_init.py's GENERATED_NAMES).
extern lib::StaticObject<CFixedAlloc> g_tempWndAlloc;
extern lib::StaticObject<CFixedAlloc> g_tempMenuAlloc;
extern lib::StaticObject<CWnd> g_wndTop;
extern lib::StaticObject<CWnd> g_wndBottom;
extern lib::StaticObject<CWnd> g_wndTopMost;
extern lib::StaticObject<CWnd> g_wndNoTopMost;

} // namespace mfc::impl
