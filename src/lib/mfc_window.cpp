// MFC 4.2's AfxMessageBox, CWnd's constructor and destructor, registered at their library addresses
// (mfc_window.h).
#include "mfc_window.h"

#include "ei/hook.h"
#include "game/originals_library.h"   // CWndDestroyWindow
#include "gameapi.h"
#include "mfc.h"
#include "mfc_runtime_class.h"
#include "mfc_string.h"
#include "game/game_data.h"

namespace mfc::impl {

namespace {

// MFC's four static windows (wndTop, wndBottom, wndTopMost, wndNoTopMost).
bool IsStaticWindow(const CWnd* wnd) {
    return wnd == ei::GamePointer<const CWnd>(0x007CAA28) || wnd == ei::GamePointer<const CWnd>(0x007CA9E8) ||
           wnd == ei::GamePointer<const CWnd>(0x007CA9A8) || wnd == ei::GamePointer<const CWnd>(0x007CA968);
}

// The fixed allocators of CTempWnd (0x3C bytes) and CTempMenu (8).
CFixedAlloc& TempWndAlloc() { return ei::GameData<CFixedAlloc>(0x007CA940); }
CFixedAlloc& TempMenuAlloc() { return ei::GameData<CFixedAlloc>(0x007CAAA8); }

// CWnd's 46 slots (C19; MFC 4.2's CWnd virtuals in declaration order): CCmdTarget's 20, then the window's, all
// unported but the destructor (config/standalone_unreachable.csv: the image makes MFC's four static windows only).
template <typename Self, const CRuntimeClass* kClass>
struct WndTable : CmdTargetTable<Self, kClass> {
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
};
// game.exe's 00746DA8 (0: 0070D35D, 1: 0070452A).
struct PlainWndTable final : WndTable<PlainWndTable, &kClassCWnd> {
    void* DeleteScalar(unsigned flags) override { return reinterpret_cast<CWnd*>(this)->DeletingDestructor(flags); }
};
// CTempWnd (game.exe's 00746EC4; 0: 0070D3C8, 1: 0070D3A3): ~CWnd, the memory back to the fixed allocator.
struct TempWndTable final : WndTable<TempWndTable, &kClassCTempWnd> {
    void* DeleteScalar(unsigned flags) override {
        auto* self = reinterpret_cast<CWnd*>(this);
        self->Destruct();
        if (flags & 1) TempWndAlloc().Free(self);
        return self;
    }
};

// CMenu's 5 slots: 0 GetRuntimeClass, 1 the scalar deleting destructor (unported: ~CMenu destroys the menu through
// MFC's menu handle map, which the image does not have), 2 Serialize (CObject's, empty), 3 DrawItem and 4
// MeasureItem (empty: 00707D0C / 00707D0F).
template <typename Self, const CRuntimeClass* kClass>
struct MenuTable : ei::CompiledTag<Self> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return kClass; }   // 0
    virtual void* DeleteScalar(unsigned flags) = 0;                           // 1
    virtual void Serialize(void*) {}                                          // 2: 00444B00
    virtual void DrawItem(DRAWITEMSTRUCT*) {}                                 // 3
    virtual void MeasureItem(MEASUREITEMSTRUCT*) {}                           // 4
};
// game.exe's 007471FC (0: 0070DC9A) and CTempMenu's 00747214 (0: 0070DD05).
struct PlainMenuTable final : MenuTable<PlainMenuTable, &kClassCMenu> {
    void* DeleteScalar(unsigned) override { ei::UnportedSlot(0x006E7150); }
};
struct TempMenuTable final : MenuTable<TempMenuTable, &kClassCTempMenu> {
    void* DeleteScalar(unsigned) override { ei::UnportedSlot(0x0070DCBC); }
};

} // namespace

void* CWnd::Vtable() { return PlainWndTable::Vtable(); }
void* CWnd::TempVtable() { return TempWndTable::Vtable(); }
void* CMenu::Vtable() { return PlainMenuTable::Vtable(); }
void* CMenu::TempVtable() { return TempMenuTable::Vtable(); }

CCmdTarget* CCmdTarget::Construct() {
    refCount = 1;
    outerUnknown = nullptr;
    innerUnknownVtable = nullptr;
    dispatchVtable = nullptr;
    resultExpected = TRUE;
    connectionPoints = nullptr;
    return this;
}

void CCmdTarget::Destruct() {
    if (dispatchVtable) reinterpret_cast<COleDispatchImpl*>(&dispatchVtable)->Disconnect();
}

CWnd* CWnd::Construct(HWND handle) {
    base.Construct();
    base.vtable = Vtable();
    owner = nullptr;
    flags = 0;
    superProc = nullptr;
    modalResult = 0;
    dropTarget = nullptr;
    controlContainer = nullptr;
    controlSite = nullptr;
    window = handle;
    return this;
}

void CWnd::Destruct() {
    base.vtable = Vtable();
    if (window && !IsStaticWindow(this)) CWndDestroyWindow(this);
    if (controlContainer) static_cast<mfc::CObjectVirtuals*>(controlContainer)->DeletingDestructor(1);
    if (controlSite && controlSite->windowControl == this) controlSite->windowControl = nullptr;
    base.Destruct();
}

CWnd* CWnd::DeletingDestructor(unsigned deleteFlags) {
    Destruct();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}

void* __stdcall CreateCWnd() {
    auto* wnd = static_cast<CWnd*>(gamecrt::operator_new(sizeof(CWnd)));
    return wnd ? wnd->Construct(nullptr) : nullptr;
}

void* __stdcall CreateCTempWnd() {
    auto* wnd = static_cast<CWnd*>(TempWndAlloc().Alloc());
    if (!wnd) return nullptr;
    wnd->Construct(nullptr);
    wnd->base.vtable = CWnd::TempVtable();
    return wnd;
}

void* __stdcall CreateCMenu() {
    auto* menu = static_cast<CMenu*>(gamecrt::operator_new(sizeof(CMenu)));
    if (!menu) return nullptr;
    menu->vtable = CMenu::Vtable();
    menu->menu = nullptr;
    return menu;
}

void* __stdcall CreateCTempMenu() {
    auto* menu = static_cast<CMenu*>(TempMenuAlloc().Alloc());
    if (!menu) return nullptr;
    menu->menu = nullptr;
    menu->vtable = CMenu::TempVtable();
    return menu;
}

EI_REWRITTEN(0x00703EAE, CCmdTarget::Construct);
EI_REPLACE(0x0070D32B, CreateCWnd);
EI_REPLACE(0x0070D363, CreateCTempWnd);
EI_REPLACE(0x0070DC80, CreateCMenu);
EI_REPLACE(0x0070DCA0, CreateCTempMenu);
EI_REWRITTEN(0x00703EC8, CCmdTarget::Destruct);
EI_REPLACE_METHOD(0x00704546, CWnd::Construct);
EI_REPLACE_METHOD(0x00704FE3, CWnd::Destruct);
EI_REWRITTEN(0x0070452A, CWnd::DeletingDestructor);

int __stdcall MessageBoxText(const char* text, unsigned type, unsigned) {
    if (!(type & MB_ICONMASK)) {
        const unsigned buttons = type & MB_TYPEMASK;
        if (buttons == MB_OK || buttons == MB_OKCANCEL || buttons == MB_YESNOCANCEL || buttons == MB_YESNO) type |= MB_ICONEXCLAMATION;
    }
    char title[MAX_PATH];
    gameapi::GetModuleFileNameA(nullptr, title, MAX_PATH);
    return gameapi::MessageBoxA(nullptr, text, title, type);
}

int __stdcall MessageBoxId(unsigned promptId, unsigned type, unsigned helpId) {
    CString prompt;
    prompt.LoadResourceString(promptId);
    if (helpId == static_cast<unsigned>(-1)) helpId = promptId;
    return MessageBoxText(prompt, type, helpId);
}

EI_REPLACE(0x0070AC94, MessageBoxText);
EI_REWRITTEN(0x0070ACCC, MessageBoxId);

} // namespace mfc::impl
