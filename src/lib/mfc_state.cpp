// MFC 4.2's thread and process locals and AUX_DATA, registered at their library addresses
// (mfc_state.h).
#include "mfc_state.h"

#include "ei/hook.h"
#include "game/originals_library.h"   // GameAtExit
#include "gameapi.h"
#include "game/game_data.h"

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

namespace {

// GetVersion through the exe's import table (gameapi.h leaves it out: its SDK prototype differs).
using GetVersionFn = DWORD(WINAPI*)();
GetVersionFn& GetVersionImport() { return ei::GameData<GetVersionFn>(0x0073B2D0); }

// 0070D158 / 0070D188 as the original calls them: on afxData, no arguments.
void __cdecl UpdateAfxScrollMetricsMarked4() { AfxData().UpdateScrollMetricsMarked4(); }
void __cdecl UpdateAfxScrollMetrics() { AfxData().UpdateScrollMetrics(); }

// AfxWingdixTerm as the exe's atexit list holds it: its address (ours, through the registration).
using ExitFunction = void(__cdecl*)();
ExitFunction WingdixTermAt() { return reinterpret_cast<ExitFunction>(0x0070B5F0); }

} // namespace

CThreadSlotData*& AfxThreadData() { return ei::GameData<CThreadSlotData*>(0x007C9084); }
AuxData& AfxData() { return ei::GameData<AuxData>(0x007C90C8); }

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

EI_REPLACE_METHOD(0x0070CC2D, CThreadLocalObject::Destruct);   // ~CThreadLocal<_AFX_THREAD_STATE> (007C90C0)
EI_REPLACE_METHOD(0x0070D5F9, CThreadLocalObject::Destruct);   // ~CThreadLocal<T> (007CAA84)
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

EI_REPLACE_METHOD(0x0070D1AA, AuxData::Construct);
EI_REPLACE_METHOD(0x0070E181, AuxData::Destruct);

} // namespace mfc::impl

namespace mfc::impl {

// ---- Module state, runtime class list, global locks (S3f) ----------------------------------------

void CSimpleList::AddHead(void* element) {
    *reinterpret_cast<void**>(static_cast<char*>(element) + nextOffset) = head;
    head = element;
}

ModuleState* __cdecl AfxGetModuleState() { return mfc::impl::ImageModuleState(); }

ModuleState* __cdecl ImageModuleState() {
    static ModuleState state = {nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, {0, 0}, 0, {nullptr, kNextClassOffset}};
    return &state;
}

BOOL& AfxCriticalInitDone() { return ei::GameData<BOOL>(0x007C907C); }
BOOL& AfxCriticalWin32s() { return ei::GameData<BOOL>(0x007C9078); }
int* AfxLockInit() { return ei::GamePointer<int>(0x007C8E80); }
CRITICAL_SECTION* AfxResourceLocks() { return ei::GamePointer<CRITICAL_SECTION>(0x007C8EE0); }
CRITICAL_SECTION* AfxLockInitLock() { return ei::GamePointer<CRITICAL_SECTION>(0x007C8EC8); }

BOOL __cdecl AfxCriticalInit() {
    if (!AfxCriticalInitDone()) {
        AfxCriticalInitDone() = TRUE;
        const DWORD versionWord = GetVersionImport()();
        if ((versionWord & 0xFF) < 4 && (versionWord & 0x80000000u)) {
            AfxCriticalWin32s() = TRUE;
        } else {
            AfxCriticalWin32s() = FALSE;
            gameapi::InitializeCriticalSection(AfxLockInitLock());
        }
    }
    return AfxCriticalInitDone();
}

void __stdcall AfxLockGlobals(int lockType) {
    if (!AfxCriticalInitDone()) AfxCriticalInit();
    if (AfxCriticalWin32s()) return;
    if (!AfxLockInit()[lockType]) {
        gameapi::EnterCriticalSection(AfxLockInitLock());
        if (!AfxLockInit()[lockType]) {
            gameapi::InitializeCriticalSection(&AfxResourceLocks()[lockType]);
            ++AfxLockInit()[lockType];
        }
        gameapi::LeaveCriticalSection(AfxLockInitLock());
    }
    gameapi::EnterCriticalSection(&AfxResourceLocks()[lockType]);
}

void __stdcall AfxUnlockGlobals(int lockType) {
    if (AfxCriticalWin32s()) return;
    gameapi::LeaveCriticalSection(&AfxResourceLocks()[lockType]);
}

void __stdcall AfxClassInit(CRuntimeClass* newClass) {
    ModuleState* state = AfxGetModuleState();
    AfxLockGlobals(kRuntimeClassListLock);
    state->classList.AddHead(newClass);
    AfxUnlockGlobals(kRuntimeClassListLock);
}

BOOL __cdecl AfxInitDbcs() {
    CPINFO info;
    gameapi::GetCPInfo(gameapi::GetOEMCP(), &info);
    return info.MaxCharSize > 1 ? TRUE : FALSE;
}

HGDIOBJ& AfxHalftoneBrush() { return ei::GameData<HGDIOBJ>(0x007C8D40); }
char& AfxWingdixTermResult() { return ei::GameData<char>(0x007C8D44); }

void __cdecl AfxWingdixTerm() { AfxDeleteObject(&AfxHalftoneBrush()); }

void __cdecl AfxWingdixTermInit() { AfxWingdixTermResult() = static_cast<char>(GameAtExit(WingdixTermAt())); }

EI_REPLACE(0x00700674, AfxClassInit);
EI_REPLACE(0x0070DC4D, AfxInitDbcs);
EI_REPLACE(0x0070B5F0, AfxWingdixTerm);
EI_REPLACE(0x0070B600, AfxWingdixTermInit);

} // namespace mfc::impl
