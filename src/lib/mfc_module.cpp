// MFC 4.2's static data the static initialisers build (mfc_module.h). Checked by
// tests/verify/verify_mfc_module.cpp (start-up checks against what the original initialisers built).
#include "mfc_module.h"

#include "crt.h"
#include "gameapi.h"

#include "ei/hook.h"

#include <cstring>

namespace mfc::impl {

namespace {
// GetVersion through the game's import slot (gameapi.h leaves it out: the SDK marks it deprecated).
DWORD GameGetVersion() {
    using Fn = DWORD(WINAPI*)();
    return (*reinterpret_cast<Fn*>(0x0073B2D0))();
}
// 007C8D40: the GDI object AfxGdiTerm deletes; 007C8D44: its initialiser's atexit result.
HGDIOBJ& TermObject() { return *reinterpret_cast<HGDIOBJ*>(0x007C8D40); }
char& TermRegistered() { return *reinterpret_cast<char*>(0x007C8D44); }
} // namespace

SimpleList& ImageClassList() {
    static SimpleList list{nullptr, offsetof(CRuntimeClass, nextClass)};
    return list;
}

void __stdcall AfxClassInit(CRuntimeClass* newClass) { ImageClassList().AddHead(newClass); }
EI_REPLACE_STANDALONE(0x00700674, AfxClassInit);

// ---- AUX_DATA --------------------------------------------------------------------------------------

AuxData* AuxData::Construct() {
    DWORD version = GameGetVersion();
    BYTE major = LOBYTE(version);
    nWinVer = (major << 8) + HIBYTE(LOWORD(version));
    bWin95 = version >> 31;
    bWin4 = major >= 4 ? 1 : 0;
    bNotWin4 = 1 - bWin4;
    bSmCaption = bWin4;
    bMarked4 = 0;
    if (bWin4) bMarked4 = gameapi::GetProcessVersion(0) >= 0x00040000 ? 1 : 0;
    UpdateSysMetrics();
    hbrBtnFace = nullptr;
    UpdateSysColors();
    hcurWait = gameapi::LoadCursorA(nullptr, IDC_WAIT);
    hcurArrow = gameapi::LoadCursorA(nullptr, IDC_ARROW);
    hbmMenuDot = nullptr;
    hcurHelp = nullptr;
    cxBorder2 = cyBorder2 = (bWin4 != 0) + 1;
    return this;
}
EI_REPLACE_METHOD(0x0070D1AA, AuxData::Construct);

void AuxData::UpdateSysColors() {
    clrBtnFace = gameapi::GetSysColor(COLOR_BTNFACE);
    clrBtnShadow = gameapi::GetSysColor(COLOR_BTNSHADOW);
    clrBtnHilite = gameapi::GetSysColor(COLOR_BTNHIGHLIGHT);
    clrBtnText = gameapi::GetSysColor(COLOR_BTNTEXT);
    clrWindowFrame = gameapi::GetSysColor(COLOR_WINDOWFRAME);
    hbrBtnFace = gameapi::GetSysColorBrush(COLOR_BTNFACE);
    hbrWindowFrame = gameapi::GetSysColorBrush(COLOR_WINDOWFRAME);
}
EI_REPLACE_METHOD(0x00703E12, AuxData::UpdateSysColors);

void AuxData::UpdateSysMetrics() {
    cxIcon = gameapi::GetSystemMetrics(SM_CXICON);
    cyIcon = gameapi::GetSystemMetrics(SM_CYICON);
    if (bMarked4) SetScrollMetrics4();
    else SetScrollMetrics3();
    HDC screen = gameapi::GetDC(nullptr);
    cxPixelsPerInch = gameapi::GetDeviceCaps(screen, LOGPIXELSX);
    cyPixelsPerInch = gameapi::GetDeviceCaps(screen, LOGPIXELSY);
    gameapi::ReleaseDC(nullptr, screen);
}
EI_REPLACE_METHOD(0x00703E56, AuxData::UpdateSysMetrics);

void __cdecl SetScrollMetrics4() {
    AuxData& data = AfxData();
    if (!data.bWin4) return;
    data.cxVScroll = gameapi::GetSystemMetrics(SM_CXVSCROLL) + 1;
    data.cyHScroll = gameapi::GetSystemMetrics(SM_CYHSCROLL) + 1;
    data.bMarked4 = 1;
}
EI_REPLACE(0x0070D158, SetScrollMetrics4);

void __cdecl SetScrollMetrics3() {
    AuxData& data = AfxData();
    data.cxVScroll = gameapi::GetSystemMetrics(SM_CXVSCROLL);
    data.cyHScroll = gameapi::GetSystemMetrics(SM_CYHSCROLL);
    data.bMarked4 = 0;
}
EI_REPLACE(0x0070D188, SetScrollMetrics3);

// ---- CCmdTarget / CWnd -----------------------------------------------------------------------------

CCmdTarget* CCmdTarget::Construct() {
    dwRef = 1;
    outerUnknown = nullptr;
    innerUnknown = 0;
    dispatchVtable = nullptr;
    resultExpected = 1;
    connPtContainerVtable = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x00703EAE, CCmdTarget::Construct);

CWnd* CWnd::Construct(HWND window) {
    base.Construct();
    base.vtable = Vtable();
    hWnd = nullptr;
    std::memset(members, 0, sizeof(members));
    hWnd = window;
    return this;
}
EI_REPLACE_METHOD(0x00704546, CWnd::Construct);

// ---- The rest ------------------------------------------------------------------------------------

BOOL __cdecl OemCodePageIsDbcs() {
    CPINFO info;
    gameapi::GetCPInfo(gameapi::GetOEMCP(), &info);
    return info.MaxCharSize > 1 ? 1 : 0;
}
EI_REPLACE(0x0070DC4D, OemCodePageIsDbcs);

void __stdcall AfxDeleteObject(HGDIOBJ* object) {
    if (!*object) return;
    gameapi::DeleteObject(*object);
    *object = nullptr;
}
EI_REPLACE(0x0070AA54, AfxDeleteObject);

void __cdecl AfxGdiTerm() { AfxDeleteObject(&TermObject()); }
EI_REPLACE(0x0070B5F0, AfxGdiTerm);

void __cdecl AfxGdiTermInit() { TermRegistered() = static_cast<char>(libcrt::AtExit(&AfxGdiTerm)); }
EI_REPLACE(0x0070B600, AfxGdiTermInit);

} // namespace mfc::impl
