// MFC 4.2's thread and process locals and AUX_DATA, registered at their library addresses
// (mfc_state.h).
#include "mfc_state.h"

#include "ei/hook.h"
#include "gameapi.h"

namespace mfc::impl {

namespace {

// GetVersion through the exe's import table (gameapi.h leaves it out: its SDK prototype differs).
using GetVersionFn = DWORD(WINAPI*)();
GetVersionFn& GetVersionImport() { return *reinterpret_cast<GetVersionFn*>(0x0073B2D0); }

// 0070D158 / 0070D188 as the original calls them: on afxData, no arguments.
void __cdecl UpdateAfxScrollMetricsMarked4() { AfxData().UpdateScrollMetricsMarked4(); }
void __cdecl UpdateAfxScrollMetrics() { AfxData().UpdateScrollMetrics(); }

} // namespace

CThreadSlotData*& AfxThreadData() { return *reinterpret_cast<CThreadSlotData**>(0x007C9084); }
AuxData& AfxData() { return *reinterpret_cast<AuxData*>(0x007C90C8); }

// ---- Thread and process locals -------------------------------------------------------------------

void CThreadSlotData::FreeSlot(int slot) {
    CRITICAL_SECTION* lock = &section;
    gameapi::EnterCriticalSection(lock);
    for (CThreadData* thread = threads; thread; thread = thread->next) {
        if (slot >= thread->count) continue;
        if (CNoTrackObject* value = thread->values[slot]) value->DeletingDestructor(1);
        thread->values[slot] = nullptr;
    }
    slots[slot].flags &= ~kSlotUsed;
    gameapi::LeaveCriticalSection(lock);
}

void CThreadLocalObject::Destruct() {
    if (slot != 0 && AfxThreadData()) AfxThreadData()->FreeSlot(slot);
    slot = 0;
}

void CProcessLocalObject::Destruct() {
    if (object) object->DeletingDestructor(1);
}

EI_REPLACE_METHOD(0x0070C71F, CThreadSlotData::FreeSlot);
EI_REPLACE_METHOD(0x0070CA59, CThreadLocalObject::Destruct);
EI_REPLACE_METHOD(0x0070CC2D, CThreadLocalObject::Destruct);   // ~CThreadLocal<_AFX_THREAD_STATE> (007C90C0)
EI_REPLACE_METHOD(0x0070D5F9, CThreadLocalObject::Destruct);   // ~CThreadLocal<T> (007CAA84)
EI_REPLACE_METHOD(0x0070CAD2, CProcessLocalObject::Destruct);
EI_REPLACE_METHOD(0x0070CE77, CProcessLocalObject::Destruct);  // ~CProcessLocal<_AFX_BASE_MODULE_STATE> (007C90BC)
EI_REPLACE_METHOD(0x0070D61F, CProcessLocalObject::Destruct);  // ~CProcessLocal<T> (007CAA88)
EI_REPLACE_METHOD(0x0070DC48, CProcessLocalObject::Destruct);  // ~CProcessLocal<T> (007CAA8C)

// ---- GDI objects and AUX_DATA ----------------------------------------------------------------------

void __stdcall AfxDeleteObject(HGDIOBJ* object) {
    if (!*object) return;
    gameapi::DeleteObject(*object);
    *object = nullptr;
}

AuxData* AuxData::Construct() {
    const DWORD versionWord = GetVersionImport()();
    const unsigned major = versionWord & 0xFF;
    const unsigned minor = (versionWord >> 8) & 0xFF;
    version = (major << 8) + minor;
    win95 = static_cast<BOOL>(versionWord >> 31);
    win4 = major >= 4 ? 1 : 0;
    notWin4 = 1 - win4;
    smallCaption = win4;
    marked4 = FALSE;
    if (win4) marked4 = gameapi::GetProcessVersion(0) >= 0x40000 ? 1 : 0;
    UpdateSysMetrics();
    buttonFaceBrush = nullptr;
    UpdateSysColors();
    waitCursor = gameapi::LoadCursorA(nullptr, MAKEINTRESOURCEA(32514));   // IDC_WAIT
    arrowCursor = gameapi::LoadCursorA(nullptr, MAKEINTRESOURCEA(32512));   // IDC_ARROW
    menuDot = nullptr;
    helpCursor = nullptr;
    borderWidth2 = win4 ? 2 : 1;
    borderHeight2 = borderWidth2;
    return this;
}

void AuxData::Destruct() {
    AfxDeleteObject(reinterpret_cast<HGDIOBJ*>(&menuDot));
}

void AuxData::UpdateSysColors() {
    buttonFace = gameapi::GetSysColor(COLOR_BTNFACE);
    buttonShadow = gameapi::GetSysColor(COLOR_BTNSHADOW);
    buttonHighlight = gameapi::GetSysColor(COLOR_BTNHIGHLIGHT);
    buttonText = gameapi::GetSysColor(COLOR_BTNTEXT);
    windowFrame = gameapi::GetSysColor(COLOR_WINDOWFRAME);
    buttonFaceBrush = gameapi::GetSysColorBrush(COLOR_BTNFACE);
    windowFrameBrush = gameapi::GetSysColorBrush(COLOR_WINDOWFRAME);
}

void AuxData::UpdateSysMetrics() {
    iconWidth = gameapi::GetSystemMetrics(SM_CXICON);
    iconHeight = gameapi::GetSystemMetrics(SM_CYICON);
    if (marked4)
        UpdateScrollMetricsMarked4();
    else
        UpdateScrollMetrics();
    HDC screen = gameapi::GetDC(nullptr);
    pixelsPerInchX = gameapi::GetDeviceCaps(screen, LOGPIXELSX);
    pixelsPerInchY = gameapi::GetDeviceCaps(screen, LOGPIXELSY);
    gameapi::ReleaseDC(nullptr, screen);
}

void AuxData::UpdateScrollMetricsMarked4() {
    if (!win4) return;
    vScrollWidth = gameapi::GetSystemMetrics(SM_CXVSCROLL) + 1;
    marked4 = TRUE;
    hScrollHeight = gameapi::GetSystemMetrics(SM_CYHSCROLL) + 1;
}

void AuxData::UpdateScrollMetrics() {
    vScrollWidth = gameapi::GetSystemMetrics(SM_CXVSCROLL);
    hScrollHeight = gameapi::GetSystemMetrics(SM_CYHSCROLL);
    marked4 = FALSE;
}

EI_REPLACE(0x0070AA54, AfxDeleteObject);
EI_REPLACE_METHOD(0x0070D1AA, AuxData::Construct);
EI_REPLACE_METHOD(0x0070E181, AuxData::Destruct);
EI_REPLACE_METHOD(0x00703E12, AuxData::UpdateSysColors);
EI_REPLACE_METHOD(0x00703E56, AuxData::UpdateSysMetrics);
EI_REPLACE(0x0070D158, UpdateAfxScrollMetricsMarked4);
EI_REPLACE(0x0070D188, UpdateAfxScrollMetrics);

} // namespace mfc::impl
