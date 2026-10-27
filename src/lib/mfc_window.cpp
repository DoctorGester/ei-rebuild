// MFC 4.2's AfxMessageBox, CWnd's constructor and destructor, registered at their library addresses
// (mfc_window.h).
#include "mfc_window.h"

#include "ei/hook.h"
#include "gameapi.h"
#include "mfc.h"
#include "mfc_runtime_class.h"
#include "mfc_string.h"
#include "game/game_data.h"

namespace mfc::impl {

constinit lib::StaticObject<CFixedAlloc> g_tempWndAlloc = /*007CA940*/ {};
constinit lib::StaticObject<CWnd> g_wndNoTopMost = /*007CA968*/ {};
constinit lib::StaticObject<CWnd> g_wndTopMost = /*007CA9A8*/ {};
constinit lib::StaticObject<CWnd> g_wndBottom = /*007CA9E8*/ {};
constinit lib::StaticObject<CWnd> g_wndTop = /*007CAA28*/ {};
constinit lib::StaticObject<CFixedAlloc> g_tempMenuAlloc = /*007CAAA8*/ {};

namespace {

// MFC's four static windows (mfc_window.h).
bool IsStaticWindow(const CWnd* wnd) {
    return wnd == &*g_wndTop || wnd == &*g_wndBottom || wnd == &*g_wndTopMost || wnd == &*g_wndNoTopMost;
}

// The fixed allocators of CTempWnd and CTempMenu (mfc_window.h).
CFixedAlloc& TempWndAlloc() { return *g_tempWndAlloc; }
CFixedAlloc& TempMenuAlloc() { return *g_tempMenuAlloc; }

// The DragList message's id (wincore.cpp's nMsgDragList): CWnd's message map entry for it ON_REGISTERED_MESSAGE,
// points at it.
constinit unsigned g_dragListMessage = /*007CAA64*/ {};

// a static initialiser (cdecl; the list holds a jump to its body).
void __cdecl RegisterDragListMessage() { g_dragListMessage = gameapi::RegisterWindowMessageA("commctrl_DragListMsg"); }

// CTempWnd (game.exe's): ~CWnd, the memory back to the fixed allocator.
class CTempWnd final : public CWnd {
public:
    const CRuntimeClass* GetRuntimeClass() const override { return &kClassCTempWnd; }
    static void* operator new(std::size_t) noexcept { return TempWndAlloc().Alloc(); }
    static void operator delete(void* block) { TempWndAlloc().Free(block); }
};

// CTempMenu.
class CTempMenu final : public CMenu {
public:
    // Unported, as ~CMenu.
#pragma warning(suppress : 4722)   // the unported body does not return
    ~CTempMenu() override { ei::UnportedSlot(0x0070DCBC); }
    const CRuntimeClass* GetRuntimeClass() const override { return &kClassCTempMenu; }
    static void* operator new(std::size_t) noexcept { return TempMenuAlloc().Alloc(); }
    static void operator delete(void* block) { TempMenuAlloc().Free(block); }
};

} // namespace

const CRuntimeClass* CWnd::GetRuntimeClass() const { return &kClassCWnd; }
const CRuntimeClass* CMenu::GetRuntimeClass() const { return &kClassCMenu; }

#pragma warning(suppress : 4722)   // the unported body does not return
CMenu::~CMenu() { ei::UnportedSlot(0x006E7150); }

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

void __fastcall DestroyStaticWindow(CWnd* window) { window->~CWnd(); }

void* __stdcall CreateCWnd() { return new CWnd(); }

void* __stdcall CreateCTempWnd() { return new CTempWnd(); }

void* __stdcall CreateCMenu() { return new CMenu(); }

void* __stdcall CreateCTempMenu() { return new CTempMenu(); }

EI_REWRITTEN_SPECIAL(0x00703EAE, CCmdTarget::CCmdTarget);
EI_REPLACE(0x0070D32B, CreateCWnd);
EI_REPLACE(0x0070D363, CreateCTempWnd);
EI_REPLACE(0x0070DC80, CreateCMenu);
EI_REPLACE(0x0070DCA0, CreateCTempMenu);
EI_REWRITTEN_SPECIAL(0x00703EC8, CCmdTarget::~CCmdTarget);
EI_REPLACE(0x007043FA, RegisterDragListMessage);
EI_REPLACE_METHOD(0x00704546, CWnd::Construct);
EI_REWRITTEN_SPECIAL(0x007068AA, CWnd::CWnd);
EI_REPLACE(0x00704FE3, DestroyStaticWindow);
EI_REWRITTEN_SPECIAL(0x0070452A, CWnd::~CWnd);   // deleting
EI_REWRITTEN(0x0070D35D, CWnd::GetRuntimeClass);
EI_REWRITTEN(0x0070DC9A, CMenu::GetRuntimeClass);

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
