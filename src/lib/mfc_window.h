#pragma once

// MFC 4.2's AfxMessageBox as the game reaches it (docs/notes/standalone.md section 2). The game constructs no
// CWinApp and has no MFC window, so the module state's application is always null: AfxMessageBox runs
// CWinApp::DoMessageBox with no object, and the box has no owner, no help context and the module's file name
// as its title (mfc_window.cpp). CWnd's constructor and destructor: the exit functions run it on
// MFC's four static windows (wndTop, wndBottom, wndTopMost, wndNoTopMost: placement constants, never destroyed
// as windows); MFC's other classes reach it from their own destructors.

#include <windows.h>
#include "compiled_vtable.h"
#include "mfc.h"
#include "mfc_object.h"
#include "static_object.h"
#include "game/game_data.h"

namespace mfc::impl {

// COleDispatchImpl's virtual slots (IDispatch's seven, then Disconnect); declared only.
class __declspec(novtable) COleDispatchImpl {
public:
    virtual void QueryInterface();
    virtual void AddRef();
    virtual void Release();
    virtual void GetTypeInfoCount();
    virtual void GetTypeInfo();
    virtual void GetIDsOfNames();
    virtual void Invoke();
    virtual void Disconnect();
};

class CWnd;

// CCmdTarget (0x1C bytes): CObject's three slots, then MFC 4.2's CCmdTarget virtuals (3-19: the command
// routing and OLE automation slots, unported: only MFC's own message routing and IDispatch call them, none of
// which the image has: its CWnds are MFC's four static windows, its CWinThreads run their procedure only). CWnd and
// CWinThread derive from it.
class CCmdTarget : public CObject {
public:
    // One reference, no outer unknown or dispatch, a result expected.
    CCmdTarget();
    // An enabled dispatch disconnected (COleDispatchImpl::Disconnect).
    ~CCmdTarget() override;

    virtual BOOL OnCmdMsg(UINT, int, void*, void*) { ei::UnportedSlot(0x00703FFA); }
    virtual void OnFinalRelease() = 0;
    virtual BOOL IsInvokeAllowed(long) { ei::UnportedSlot(0x00704109); }
    virtual BOOL GetDispatchIID(IID*) { ei::UnportedSlot(0x0070410F); }
    virtual UINT GetTypeInfoCount() { ei::UnportedSlot(0x00704114); }
    virtual void* GetTypeLibCache() { ei::UnportedSlot(0x00704117); }
    virtual HRESULT GetTypeLib(LCID, void**) { ei::UnportedSlot(0x0070411A); }
    virtual const void* GetMessageMap() const = 0;
    virtual const void* GetCommandMap() const { ei::UnportedSlot(0x007041C4); }
    virtual const void* GetDispatchMap() const { ei::UnportedSlot(0x00704167); }
    virtual const void* GetConnectionMap() const { ei::UnportedSlot(0x007041B4); }
    virtual const void* GetInterfaceMap() const { ei::UnportedSlot(0x00704173); }
    virtual const void* GetEventSinkMap() const { ei::UnportedSlot(0x0070416D); }
    virtual BOOL OnCreateAggregates() { ei::UnportedSlot(0x007041AB); }
    virtual void* GetInterfaceHook(const void*) { ei::UnportedSlot(0x007041AF); }
    virtual BOOL GetExtraConnectionPoints(void*) { ei::UnportedSlot(0x007041BF); }
    virtual void* GetConnectionHook(const IID&) { ei::UnportedSlot(0x007041BA); }

    long refCount;
    void* outerUnknown;
    void* innerUnknownVtable;
    void* dispatchVtable;
    BOOL resultExpected;
    void* connectionPoints;
};
static_assert(sizeof(CCmdTarget) == 0x1C);

// COleControlSite (the members used here; the image makes none).
class COleControlSite : public CCmdTarget {
public:
    void* container;
    HWND window;
    CWnd* windowControl;
};

// CWnd (0x3C bytes). Game.exe's table CWnd has 46 slots (MFC 4.2's CWnd virtuals in declaration order), all
// unported but the destructor: only MFC's four static windows are made, with no handle, and destroyed without a
// virtual call.
class CWnd : public CCmdTarget {
public:
    // CWnd(HWND) with no window.
    CWnd() : CWnd(nullptr) {}
    // CWnd(HWND): CCmdTarget's constructor, CWnd's members cleared and the handle kept.
    explicit CWnd(HWND handle);
    // The constructor on `this` as the static initialisers reach it at its address, building
    // MFC's four static windows (wndTop 0, wndBottom 1, wndTopMost -1, wndNoTopMost -2). Returns this.
    CWnd* Construct(HWND handle) { return new (this) CWnd(handle); }
    // A window still attached destroyed (CWnd::DestroyWindow, unported), except for the
    // four static windows; the control container deleted; the control site let go of this window; then CCmdTarget's
    // destructor. The unwind frame (CCmdTarget destroyed when DestroyWindow throws) is not kept: no game
    // window is a CWnd.
    ~CWnd() override;

    const CRuntimeClass* GetRuntimeClass() const override;
    void OnFinalRelease() override { ei::UnportedSlot(0x00705190); }
    const void* GetMessageMap() const override { ei::UnportedSlot(0x0070588F); }
    virtual void PreSubclassWindow() { ei::UnportedSlot(0x007049D5); }
    virtual BOOL Create(const char*, const char*, DWORD, const RECT*, CWnd*, UINT, void*) {
        ei::UnportedSlot(0x00704F97);
    }
    virtual BOOL DestroyWindow() { ei::UnportedSlot(0x007051A1); }
    virtual BOOL PreCreateWindow(CREATESTRUCTA*) { ei::UnportedSlot(0x00704F77); }
    virtual void CalcWindowRect(RECT*, UINT) { ei::UnportedSlot(0x0070674E); }
    virtual int OnToolHitTest(POINT, void*) const { ei::UnportedSlot(0x007052AF); }
    virtual CWnd* GetScrollBarCtrl(int) const { ei::UnportedSlot(0x007062F0); }
    virtual void WinHelp(DWORD, UINT) { ei::UnportedSlot(0x007057AD); }
    virtual BOOL ContinueModal() { ei::UnportedSlot(0x007074D8); }
    virtual void EndModalLoop(int) { ei::UnportedSlot(0x007074DF); }
    virtual BOOL OnCommand(WPARAM, LPARAM) { ei::UnportedSlot(0x00705ED3); }
    virtual BOOL OnNotify(WPARAM, LPARAM, LRESULT*) { ei::UnportedSlot(0x00705F5F); }
    virtual WNDPROC* GetSuperWndProcAddr() { ei::UnportedSlot(0x00705235); }
    virtual void DoDataExchange(void*) { ei::UnportedSlot(0x006E7128); }
    virtual void BeginModalState() { ei::UnportedSlot(0x006E712B); }
    virtual void EndModalState() { ei::UnportedSlot(0x006E7137); }
    virtual BOOL PreTranslateMessage(MSG*) { ei::UnportedSlot(0x00705239); }
    virtual BOOL OnAmbientProperty(void*, long, void*) { ei::UnportedSlot(0x007037E1); }
    virtual LRESULT WindowProc(UINT, WPARAM, LPARAM) { ei::UnportedSlot(0x007058EA); }
    virtual BOOL OnWndMsg(UINT, WPARAM, LPARAM, LRESULT*) { ei::UnportedSlot(0x0070592E); }
    virtual LRESULT DefWindowProc(UINT, WPARAM, LPARAM) { ei::UnportedSlot(0x007051EE); }
    virtual void PostNcDestroy() { ei::UnportedSlot(0x0070518F); }
    virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*) { ei::UnportedSlot(0x0070694E); }
    virtual BOOL CheckAutoCenter() { ei::UnportedSlot(0x0070711E); }
    virtual BOOL IsFrameWnd() const { ei::UnportedSlot(0x00707877); }
    virtual BOOL SetOccDialogInfo(void*) { ei::UnportedSlot(0x00707503); }

    HWND window;
    HWND owner = nullptr;
    unsigned flags = 0;
    WNDPROC superProc = nullptr;
    int modalResult = 0;
    void* dropTarget = nullptr;
    CObject* controlContainer = nullptr;
    COleControlSite* controlSite = nullptr;
};
static_assert(sizeof(CWnd) == 0x3C);

// ~CWnd as the generated exit functions reach it at its address on MFC's four static windows.
void DestroyStaticWindow(CWnd* window);

// AfxMessageBox(LPCTSTR, UINT type, UINT helpId): MessageBoxA's answer. MB_ICONEXCLAMATION is added
// to an OK, OK / Cancel, Yes / No / Cancel or Yes / No box that names no icon.
int MessageBoxText(const char* text, unsigned type, unsigned helpId);
// AfxMessageBox(UINT id, UINT type, UINT helpId): the box with string `id` of game.exe's table (empty
// when it is missing; MFC only traces that). A help id of -1 means `id`.
int MessageBoxId(unsigned promptId, unsigned type, unsigned helpId);

// MFC's four static windows (placement constants: HWND_TOP 0, HWND_BOTTOM 1, HWND_TOPMOST -1, HWND_NOTOPMOST -2),
// built and destroyed by game.exe's static initialisers and their guarded exit functions (the names are
// tools/gen_static_init.py's GENERATED_NAMES).
extern lib::StaticObject<CWnd> g_wndTop;
extern lib::StaticObject<CWnd> g_wndBottom;
extern lib::StaticObject<CWnd> g_wndTopMost;
extern lib::StaticObject<CWnd> g_wndNoTopMost;

} // namespace mfc::impl
