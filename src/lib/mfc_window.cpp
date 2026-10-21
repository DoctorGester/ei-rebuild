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

} // namespace

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
