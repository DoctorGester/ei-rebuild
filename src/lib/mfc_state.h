#pragma once

// MFC 4.2's thread and process locals (afxtls.cpp), its AUX_DATA (afxData) and what the other static
// initialisers call. See docs/notes/standalone.md, "The image's source files".

#include <cstddef>
#include <cstdint>

#include <windows.h>

namespace mfc::impl {

// CNoTrackObject: what the locals hold; deleted through its virtual destructor (with 1),
// which frees it with LocalFree. Declared only.
class __declspec(novtable) CNoTrackObject {
public:
    virtual void* DeletingDestructor(unsigned flags);
};

// CThreadData (a CNoTrackObject): one thread's slot values.
struct CThreadData {
    void* vtable;
    CThreadData* next;
    int count;
    CNoTrackObject** values;
};

// CSlotData: a slot's flags (bit 0: in use) and the module that allocated it.
struct CSlotData {
    DWORD flags;
    HINSTANCE instance;
};

// CThreadSlotData (_afxThreadData, made on first use): the TLS slots of every thread.
struct CThreadSlotData {
    enum : DWORD { kSlotUsed = 1 };

    DWORD tlsIndex;
    int allocated;
    int rover;
    int max;
    CSlotData* slots;
    CThreadData* threads;
    std::size_t nextOffset;
    CRITICAL_SECTION section;

    // Every thread's value of `slot` deleted and cleared, then the slot no longer in
    // use; under the critical section.
    void FreeSlot(int slot);
};
static_assert(offsetof(CThreadSlotData, section) == 0x1C);

// CThreadLocalObject (CThreadLocal<T> adds no members): its TLS slot, 0 when none was allocated.
struct CThreadLocalObject {
    int slot;

    // The slot freed
    // when there is one and _afxThreadData exists, then the slot cleared.
    void Destruct();
};

// CProcessLocalObject (CProcessLocal<T> adds no members): the object, made on first use.
struct CProcessLocalObject {
    CNoTrackObject* object;

    // The object is deleted; the pointer stays.
    void Destruct();
};

// AfxDeleteObject: the GDI object `*object` deleted and the handle cleared, when
// there is one.
void __stdcall AfxDeleteObject(HGDIOBJ* object);

// AUX_DATA (0x6C bytes): afxData (mfc_state.cpp).
struct AuxData {
    int vScrollWidth;
    int hScrollHeight;
    int iconWidth;
    int iconHeight;
    int borderWidth2;
    int borderHeight2;
    int pixelsPerInchX;
    int pixelsPerInchY;
    HBRUSH windowFrameBrush;
    HBRUSH buttonFaceBrush;
    COLORREF buttonFace;
    COLORREF buttonShadow;
    COLORREF buttonHighlight;
    COLORREF buttonText;
    COLORREF windowFrame;
    HCURSOR waitCursor;
    HCURSOR arrowCursor;
    HCURSOR helpCursor;
    HFONT statusFont;
    HFONT toolTipsFont;
    HBITMAP menuDot;
    unsigned version;           // major << 8 | minor
    BOOL win95;                 // GetVersion's high bit
    BOOL win4;                  // major version 4 or later
    BOOL notWin4;
    BOOL smallCaption;
    BOOL marked4;               // the process is marked 4.0; picks the scroll metrics

    // The version flags, the metrics, the colours and the two standard cursors; returns this.
    AuxData* Construct();
    // The menu-dot bitmap deleted (AfxDeleteObject).
    void Destruct();
    // The button and frame colours and their brushes.
    void UpdateSysColors();
    // The icon size, the scroll metrics (by marked4), the screen's pixels per inch.
    void UpdateSysMetrics();
    // (cdecl, on afxData): the scroll bar sizes. Marked 4.0: only on
    // Windows 4, one pixel larger, marked4 set; otherwise as the system has them, marked4 cleared.
    void UpdateScrollMetricsMarked4();
    void UpdateScrollMetrics();
};
static_assert(sizeof(AuxData) == 0x6C);

// afxData.
AuxData& AfxData();

// ---- Module state, runtime class list, global locks ----------------------------------------

// CSimpleList: an intrusive singly linked list; each element keeps its next pointer `nextOffset`
// bytes in.
struct CSimpleList {
    void* head;
    std::size_t nextOffset;

    void AddHead(void* element);
};

struct CRuntimeClass;

// AFX_MODULE_STATE (a CNoTrackObject; the members up to the runtime class list).
struct ModuleState {
    void* vtable;
    void* currentApp;           // m_pCurrentWinApp (always null: the game has no CWinApp)
    HINSTANCE instance;
    HINSTANCE resources;
    const char* appName;
    BYTE dll;
    BYTE system;
    BYTE reserved[2];
    DWORD registeredClasses;
    CSimpleList classList;      // m_classList (CRuntimeClass, linked through m_pNextClass +0x14)
};
static_assert(offsetof(ModuleState, classList) == 0x1C);

// The runtime classes' link offset (CRuntimeClass::m_pNextClass).
constexpr std::size_t kNextClassOffset = 0x14;

// AfxGetModuleState: the calling thread's module state. The hook build calls the
// original's; the image registers ImageModuleState at that address: one module state for the
// process, nothing in it but the class list. Its instance and resource handles stay null, as the
// original's do (the game never runs AfxWinInit); AfxLoadString (mfc_resource.h) answers from a table in code.
ModuleState* __cdecl AfxGetModuleState();
ModuleState* __cdecl ImageModuleState();

// MFC's global locks (mtcore.cpp): the lock types index _afxResourceLock (CRIT_RUNTIMECLASSLIST 0,
// CRIT_PROCESSLOCAL 1, ...; 17 of them: _afxResourceLock ends where _afxCriticalWin32s starts).
// Win32s (GetVersion below 4 with the high bit) uses none.
constexpr int kLockTypes = 17;
constexpr int kRuntimeClassListLock = 0;

// Once, the Win32s flag from GetVersion and the lock that guards the locks' set-up; returns
// _afxCriticalInit.
BOOL __cdecl AfxCriticalInit();
// lock type `lockType` entered (its critical section made on first use) / left.
void __stdcall AfxLockGlobals(int lockType);
void __stdcall AfxUnlockGlobals(int lockType);

// AfxClassInit: `newClass` put at the head of the module state's class list, under
// CRIT_RUNTIMECLASSLIST. The static initialisers register five classes with it.
void __stdcall AfxClassInit(CRuntimeClass* newClass);

// _AfxInitDBCS: whether the OEM code page has double-byte characters (its MaxCharSize above
// 1); the static initialiser stores it in _afxDBCS (a BOOL).
BOOL __cdecl AfxInitDbcs();

// wingdix.cpp's halftone brush (_afxHalftoneBrush, made on first use by code the game never reaches) deleted at
// exit by AfxWingdixTerm, which the static initialiser registers with atexit.
void __cdecl AfxWingdixTerm();
void __cdecl AfxWingdixTermInit();

} // namespace mfc::impl
