// MFC 4.2's AfxMessageBox, CWnd's constructor and destructor
// (mfc_window.h).
#include "mfc_window.h"

#include "gameapi.h"
#include "mfc.h"
#include "mfc_runtime_class.h"
#include "mfc_string.h"
#include "game/game_data.h"

namespace mfc::impl {

constinit lib::StaticObject<CWnd> g_wndNoTopMost = /*007CA968*/ {};
constinit lib::StaticObject<CWnd> g_wndTopMost = /*007CA9A8*/ {};
constinit lib::StaticObject<CWnd> g_wndBottom = /*007CA9E8*/ {};
constinit lib::StaticObject<CWnd> g_wndTop = /*007CAA28*/ {};

namespace {

// MFC's four static windows (mfc_window.h).
bool IsStaticWindow(const CWnd* wnd) {
    return wnd == &*g_wndTop || wnd == &*g_wndBottom || wnd == &*g_wndTopMost || wnd == &*g_wndNoTopMost;
}

// The DragList message's id (wincore.cpp's nMsgDragList): CWnd's message map entry for it ON_REGISTERED_MESSAGE,
// points at it.
constinit unsigned g_dragListMessage = /*007CAA64*/ {};

} // namespace

// a static initialiser (cdecl; the list holds a jump to its body).
void RegisterDragListMessage() { g_dragListMessage = gameapi::RegisterWindowMessageA("commctrl_DragListMsg"); }

const CRuntimeClass* CWnd::GetRuntimeClass() const { return &kClassCWnd; }

CCmdTarget::CCmdTarget()
    : refCount(1), outerUnknown(nullptr), innerUnknownVtable(nullptr), dispatchVtable(nullptr), resultExpected(TRUE),
      connectionPoints(nullptr) {}

CCmdTarget::~CCmdTarget() {
    if (dispatchVtable) reinterpret_cast<COleDispatchImpl*>(&dispatchVtable)->Disconnect();
}

CWnd::CWnd(HWND handle) : window(handle) {}

CWnd::~CWnd() {
    // CWnd::DestroyWindow is not here: the image makes MFC's four static windows only.
    if (window && !IsStaticWindow(this)) ei::UnportedSlot(0x007051A1);
    delete controlContainer;
    if (controlSite && controlSite->windowControl == this) controlSite->windowControl = nullptr;
}

void DestroyStaticWindow(CWnd* window) { window->~CWnd(); }

void* __stdcall CreateCWnd() { return new CWnd(); }

int MessageBoxText(const char* text, unsigned type, unsigned) {
    if (!(type & MB_ICONMASK)) {
        const unsigned buttons = type & MB_TYPEMASK;
        if (buttons == MB_OK || buttons == MB_OKCANCEL || buttons == MB_YESNOCANCEL || buttons == MB_YESNO) type |= MB_ICONEXCLAMATION;
    }
    char title[MAX_PATH];
    gameapi::GetModuleFileNameA(nullptr, title, MAX_PATH);
    return gameapi::MessageBoxA(nullptr, text, title, type);
}

int MessageBoxId(unsigned promptId, unsigned type, unsigned helpId) {
    CString prompt;
    prompt.LoadResourceString(promptId);
    if (helpId == static_cast<unsigned>(-1)) helpId = promptId;
    return MessageBoxText(prompt, type, helpId);
}

} // namespace mfc::impl
