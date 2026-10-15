// MFC 4.2's AfxMessageBox and CWnd's destructor, registered at their library addresses
// (mfc_window.h).
#include "mfc_window.h"

#include "ei/hook.h"
#include "game/originals_library.h"   // CWndDestroyWindow
#include "gameapi.h"
#include "mfc.h"

namespace mfc::impl {

namespace {

// MFC's four static windows (wndTop, wndBottom, wndTopMost, wndNoTopMost).
bool IsStaticWindow(const CWnd* wnd) {
    return wnd == reinterpret_cast<const CWnd*>(0x007CAA28) || wnd == reinterpret_cast<const CWnd*>(0x007CA9E8) ||
           wnd == reinterpret_cast<const CWnd*>(0x007CA9A8) || wnd == reinterpret_cast<const CWnd*>(0x007CA968);
}

} // namespace

void CCmdTarget::Destruct() {
    if (dispatchVtable) reinterpret_cast<COleDispatchImpl*>(&dispatchVtable)->Disconnect();
}

void CWnd::Destruct() {
    base.vtable = Vtable();
    if (window && !IsStaticWindow(this)) CWndDestroyWindow(this);
    if (controlContainer) static_cast<mfc::CObjectVirtuals*>(controlContainer)->DeletingDestructor(1);
    if (controlSite && controlSite->windowControl == this) controlSite->windowControl = nullptr;
    base.Destruct();
}

EI_REPLACE_METHOD(0x00703EC8, CCmdTarget::Destruct);
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

EI_REPLACE(0x0070AC94, MessageBoxText);

} // namespace mfc::impl
