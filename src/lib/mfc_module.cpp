// MFC 4.2's static data the static initialisers build (mfc_module.h). Checked by
// tests/verify/verify_mfc_module.cpp (start-up checks against what the original initialisers built).
#include "mfc_module.h"

#include "crt.h"
#include "gameapi.h"

#include "ei/hook.h"

#include <cstring>

namespace mfc::impl {

namespace {
// 007C8D40: the GDI object AfxGdiTerm deletes; 007C8D44: its initialiser's atexit result.
HGDIOBJ& TermObject() { return *reinterpret_cast<HGDIOBJ*>(0x007C8D40); }
char& TermRegistered() { return *reinterpret_cast<char*>(0x007C8D44); }

// The registered thiscall forms.
CCmdTarget* __fastcall CmdTargetConstructor(CCmdTarget* target) { return ConstructCmdTarget(target); }
CWnd* __fastcall WndConstructor(CWnd* wnd, void*, HWND window) { return ConstructWnd(wnd, window); }
} // namespace

SimpleList& ImageClassList() {
    static SimpleList list{nullptr, offsetof(CRuntimeClass, nextClass)};
    return list;
}

void __stdcall AfxClassInit(CRuntimeClass* newClass) { ImageClassList().AddHead(newClass); }
EI_REPLACE_STANDALONE(0x00700674, AfxClassInit);

CCmdTarget* ConstructCmdTarget(CCmdTarget* target) {
    target->refCount = 1;
    target->outerUnknown = nullptr;
    target->innerUnknownVtable = nullptr;
    target->dispatchVtable = nullptr;
    target->resultExpected = 1;
    target->connectionPoints = nullptr;
    return target;
}
EI_REPLACE(0x00703EAE, CmdTargetConstructor);

CWnd* ConstructWnd(CWnd* wnd, HWND window) {
    ConstructCmdTarget(&wnd->base);
    wnd->base.vtable = CWnd::Vtable();
    std::memset(&wnd->window, 0, sizeof(CWnd) - offsetof(CWnd, window));
    wnd->window = window;
    return wnd;
}
EI_REPLACE(0x00704546, WndConstructor);

BOOL __cdecl OemCodePageIsDbcs() {
    CPINFO info;
    gameapi::GetCPInfo(gameapi::GetOEMCP(), &info);
    return info.MaxCharSize > 1 ? 1 : 0;
}
EI_REPLACE(0x0070DC4D, OemCodePageIsDbcs);

void __cdecl AfxGdiTerm() { AfxDeleteObject(&TermObject()); }
EI_REPLACE(0x0070B5F0, AfxGdiTerm);

void __cdecl AfxGdiTermInit() { TermRegistered() = static_cast<char>(libcrt::AtExit(&AfxGdiTerm)); }
EI_REPLACE(0x0070B600, AfxGdiTermInit);

} // namespace mfc::impl
