// Checks for the MFC functions the exe's static initialisers call (src/lib/mfc_exception.cpp,
// mfc_window.cpp, mfc_state.cpp; ticket S3f): the simple exceptions' constructors and CException(BOOL),
// CCmdTarget's and CWnd(HWND)'s constructors, AfxClassInit with CSimpleList::AddHead and MFC's global
// locks, _AfxInitDBCS and wingdix.cpp's halftone brush term.
//
// The static initialisers run before the hooks, so a start-up check compares each with the original:
// - against the original's trampoline on twin objects of its own: CException(BOOL), the four exception
//   constructors (whole objects, filled with a pattern first), CCmdTarget's constructor, AddHead (two
//   lists of fake elements), AfxCriticalInit's answer, _AfxInitDBCS's answer (and _afxDBCS 007CAA98,
//   which the original's initialiser stored);
// - against the original's globals: the four static exceptions (vtable, auto-delete, resource id) and
//   the four static windows (wndTop ... wndNoTopMost, whole: ours rebuilds each from a scrambled copy),
//   the five classes AfxClassInit put in the module state's list, in reverse order of the
//   initialisers, and _afxWingdixTerm (007C8D44: ours registers AfxWingdixTerm once more, so it runs
//   twice at exit, on a null brush the second time);
// - AfxLockGlobals / AfxUnlockGlobals: a lock type no one has used yet through the original's
//   trampolines and another through ours, the lock's state (initialised, owned by this thread once,
//   then free) compared; AfxClassInit on two fake classes (the list's head put back afterwards).
//
// Per call, in the game: CException(BOOL), CCmdTarget's constructor and _AfxInitDBCS against the
// original on a copy; the others ours only, their results checked (the element at the list's head, the
// lock owned, the window's members). AfxWingdixTerm runs at exit, after harness_report.txt (its count is
// in ei_hook.log).
#include "verify.h"

#include "../../src/lib/mfc_exception.h"
#include "../../src/lib/mfc_state.h"
#include "../../src/lib/mfc_window.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace {

using ei::verify::Check;
using mfc::impl::CCmdTarget;
using mfc::impl::CException;
using mfc::impl::CSimpleException;
using mfc::impl::CSimpleList;
using mfc::impl::CWnd;
void* s_unused;

using SimpleConstruct = CSimpleException*(__fastcall*)(CSimpleException*, void*, BOOL, unsigned);
CException*(__fastcall* s_exceptionConstruct)(CException*, void*, BOOL);
SimpleConstruct s_resourceConstruct;
SimpleConstruct s_userConstruct;
SimpleConstruct s_memoryConstruct;
SimpleConstruct s_notSupportedConstruct;
CCmdTarget*(__fastcall* s_cmdTargetConstruct)(CCmdTarget*);
void(__fastcall* s_addHead)(CSimpleList*, void*, void*);
BOOL(__cdecl* s_criticalInit)();
void(__stdcall* s_lockGlobals)(int);
void(__stdcall* s_unlockGlobals)(int);
BOOL(__cdecl* s_initDbcs)();

// _afxDBCS, as the original's static initialiser stored _AfxInitDBCS's answer.
BOOL OriginalDbcs() { return *reinterpret_cast<const BOOL*>(0x007CAA98); }

// ---- Exceptions -------------------------------------------------------------------------------------

using OurSimpleConstruct = CSimpleException* (CSimpleException::*)(BOOL, unsigned);

struct SimpleClass {
    const char* name;
    SimpleConstruct* original;
    OurSimpleConstruct ours;
    std::uintptr_t object;  // the static object the initialiser built
    unsigned id;            // its resource id
};
const SimpleClass kSimpleClasses[] = {
    {"CResourceException(BOOL, UINT) (against the original)", &s_resourceConstruct, &CSimpleException::ConstructResource, 0x007C8C58, 0xF022},
    {"CUserException(BOOL, UINT) (against the original)", &s_userConstruct, &CSimpleException::ConstructUser, 0x007C8BC0, 0xF024},
    {"CMemoryException(BOOL, UINT) (against the original)", &s_memoryConstruct, &CSimpleException::ConstructMemory, 0x007C8DE8, 0xF023},
    {"CNotSupportedException(BOOL, UINT) (against the original)", &s_notSupportedConstruct, &CSimpleException::ConstructNotSupported,
     0x007C8D50, 0xF021},
};

void ExceptionCheck() {
    for (BOOL autoDelete : {FALSE, TRUE}) {
        CException original, ours;
        std::memset(&original, 0xCD, sizeof(original));
        std::memset(&ours, 0xCD, sizeof(ours));
        CException* originalResult = s_exceptionConstruct(&original, nullptr, autoDelete);
        CException* oursResult = ours.ConstructAutoDelete(autoDelete);
        Check("CException(BOOL) (against the original)", true,
              std::memcmp(&original, &ours, sizeof(ours)) == 0 && originalResult == &original && oursResult == &ours, "auto-delete %d",
              autoDelete);
    }

    for (const SimpleClass& c : kSimpleClasses) {
        for (BOOL autoDelete : {FALSE, TRUE}) {
            CSimpleException original, ours;
            std::memset(&original, 0xCD, sizeof(original));
            std::memset(&ours, 0xCD, sizeof(ours));
            CSimpleException* originalResult = (*c.original)(&original, nullptr, autoDelete, c.id + autoDelete);
            CSimpleException* oursResult = (ours.*c.ours)(autoDelete, c.id + autoDelete);
            Check(c.name, true, std::memcmp(&original, &ours, sizeof(ours)) == 0 && originalResult == &original && oursResult == &ours,
                  "auto-delete %d, vtable %p / %p", autoDelete, original.vtable, ours.vtable);
        }
        // The static object: the members the constructor wrote (GetErrorMessage may have filled the
        // message since).
        const auto& global = *reinterpret_cast<const CSimpleException*>(c.object);
        CSimpleException ours = global;
        ours.vtable = nullptr;
        ours.autoDelete = 7;
        ours.resourceId = 0;
        (ours.*c.ours)(FALSE, c.id);
        Check("simple exception's static object (against the original's initialiser)", true,
              ours.vtable == global.vtable && ours.autoDelete == global.autoDelete && ours.resourceId == global.resourceId,
              "%p: vtable %p / %p, auto-delete %d / %d, id %X / %X", static_cast<const void*>(&global), global.vtable, ours.vtable,
              global.autoDelete, ours.autoDelete, global.resourceId, ours.resourceId);
    }
}

// ---- CCmdTarget, CWnd ------------------------------------------------------------------------------

void WindowCheck() {
    CCmdTarget original, ours;
    std::memset(&original, 0xCD, sizeof(original));
    std::memset(&ours, 0xCD, sizeof(ours));
    CCmdTarget* originalResult = s_cmdTargetConstruct(&original);
    CCmdTarget* oursResult = ours.Construct();
    Check("CCmdTarget::CCmdTarget (against the original)", true,
          std::memcmp(&original, &ours, sizeof(ours)) == 0 && originalResult == &original && oursResult == &ours, "");

    // MFC's four static windows and the handles their initialisers pass.
    const struct {
        std::uintptr_t object;
        HWND handle;
    } windows[] = {{0x007CAA28, HWND_TOP}, {0x007CA9E8, HWND_BOTTOM}, {0x007CA9A8, HWND_TOPMOST}, {0x007CA968, HWND_NOTOPMOST}};
    for (const auto& w : windows) {
        const auto& global = *reinterpret_cast<const CWnd*>(w.object);
        CWnd wnd;
        std::memset(&wnd, 0xCD, sizeof(wnd));
        CWnd* result = wnd.Construct(w.handle);
        Check("CWnd(HWND) (against the original's static window)", true, std::memcmp(&global, &wnd, sizeof(wnd)) == 0 && result == &wnd,
              "%p, handle %p", static_cast<const void*>(&global), w.handle);
    }
}

// ---- AddHead, AfxClassInit ---------------------------------------------------------------------------

// Fake list elements: a word before the link (nextOffset 4).
struct Element {
    std::uint32_t value;
    void* next;
};

void ListCheck() {
    Element originalElements[3] = {}, oursElements[3] = {};
    CSimpleList original{nullptr, offsetof(Element, next)}, ours{nullptr, offsetof(Element, next)};
    bool same = true;
    for (int i = 0; i < 3; ++i) {
        originalElements[i].value = oursElements[i].value = 0xCDCDCDCD;
        originalElements[i].next = oursElements[i].next = reinterpret_cast<void*>(std::uintptr_t(0xABABABAB));
        s_addHead(&original, nullptr, &originalElements[i]);
        ours.AddHead(&oursElements[i]);
        same = same && original.head == &originalElements[i] && ours.head == &oursElements[i] &&
               originalElements[i].value == oursElements[i].value &&
               (i == 0 ? !originalElements[i].next && !oursElements[i].next
                       : originalElements[i].next == &originalElements[i - 1] && oursElements[i].next == &oursElements[i - 1]);
    }
    Check("CSimpleList::AddHead (against the original)", true, same, "");

    // The module state's class list as the original's initialisers left it: the five classes,
    // the last registered first.
    mfc::impl::ModuleState* state = mfc::impl::AfxGetModuleState();
    CSimpleList& classes = state->classList;
    const std::uintptr_t registered[] = {0x00797198, 0x00797140, 0x00796F20, 0x00796EE8, 0x00796CD0};
    int found = 0;
    int count = 0;
    for (void* c = classes.head; c && count < 10000; c = *reinterpret_cast<void**>(static_cast<char*>(c) + classes.nextOffset), ++count)
        if (found < 5 && reinterpret_cast<std::uintptr_t>(c) == registered[found]) ++found;
    Check("AfxClassInit (the original's class list)", true, classes.nextOffset == mfc::impl::kNextClassOffset && found == 5,
          "next offset %u, %d of 5 classes in order, %d in the list", static_cast<unsigned>(classes.nextOffset), found, count);

    // Ours on two fake classes, then the head put back.
    struct FakeClass {
        std::uint8_t fields[mfc::impl::kNextClassOffset];
        void* next;
    } first{}, second{};
    mfc::impl::AfxLockGlobals(mfc::impl::kRuntimeClassListLock);
    void* const head = classes.head;
    mfc::impl::AfxClassInit(reinterpret_cast<mfc::impl::CRuntimeClass*>(&first));
    mfc::impl::AfxClassInit(reinterpret_cast<mfc::impl::CRuntimeClass*>(&second));
    const bool linked = classes.head == &second && second.next == &first && first.next == head;
    classes.head = head;
    mfc::impl::AfxUnlockGlobals(mfc::impl::kRuntimeClassListLock);
    Check("AfxClassInit (ours on fake classes)", true, linked, "head %p", head);
}

// ---- Locks ---------------------------------------------------------------------------------------------

// A lock's state: initialised, and owned by this thread `owned` times.
bool LockState(int type, LONG owned) {
    const CRITICAL_SECTION& section = mfc::impl::AfxResourceLocks()[type];
    const bool owner = section.OwningThread == reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(::GetCurrentThreadId()));
    return mfc::impl::AfxLockInit()[type] == 1 && (owned ? owner && section.RecursionCount == owned : !owner);
}

void LockCheck() {
#pragma warning(suppress : 4996) // GetVersion, as AfxCriticalInit calls it
    const DWORD version = ::GetVersion();
    const BOOL win32s = (version & 0xFF) < 4 && (version & 0x80000000u);
    const BOOL originalInit = s_criticalInit();
    const BOOL oursInit = mfc::impl::AfxCriticalInit();
    Check("AfxCriticalInit (against the original)", true,
          originalInit == oursInit && oursInit == TRUE && mfc::impl::AfxCriticalWin32s() == win32s, "%d / %d, Win32s %d", originalInit,
          oursInit, mfc::impl::AfxCriticalWin32s());
    if (win32s) return;

    // Two lock types nothing has used yet, from the last.
    int unused[2] = {-1, -1};
    for (int type = mfc::impl::kLockTypes - 1, n = 0; type > 0 && n < 2; --type)
        if (!mfc::impl::AfxLockInit()[type]) unused[n++] = type;
    if (unused[1] < 0) {
        Check("AfxLockGlobals (two unused lock types)", true, false, "%d, %d", unused[0], unused[1]);
        return;
    }
    const int originalType = unused[0], oursType = unused[1];
    s_lockGlobals(originalType);
    s_lockGlobals(originalType);
    mfc::impl::AfxLockGlobals(oursType);
    mfc::impl::AfxLockGlobals(oursType);
    const bool originalLocked = LockState(originalType, 2), oursLocked = LockState(oursType, 2);
    s_unlockGlobals(originalType);
    mfc::impl::AfxUnlockGlobals(oursType);
    const bool originalOnce = LockState(originalType, 1), oursOnce = LockState(oursType, 1);
    s_unlockGlobals(originalType);
    mfc::impl::AfxUnlockGlobals(oursType);
    const bool originalFree = LockState(originalType, 0), oursFree = LockState(oursType, 0);
    Check("AfxLockGlobals / AfxUnlockGlobals (against the original)", true,
          originalLocked && oursLocked && originalOnce && oursOnce && originalFree && oursFree, "types %d / %d: %d%d%d / %d%d%d",
          originalType, oursType, originalLocked, originalOnce, originalFree, oursLocked, oursOnce, oursFree);
}

// ---- _AfxInitDBCS, wingdix ------------------------------------------------------------------------------

void DbcsAndWingdixCheck() {
    const BOOL original = s_initDbcs();
    const BOOL ours = mfc::impl::AfxInitDbcs();
    Check("_AfxInitDBCS (against the original and _afxDBCS)", true, original == ours && ours == OriginalDbcs(), "%d / %d, _afxDBCS %d",
          original, ours, OriginalDbcs());

    const char before = mfc::impl::AfxWingdixTermResult();
    mfc::impl::AfxWingdixTermInit();
    const char after = mfc::impl::AfxWingdixTermResult();
    Check("_afxWingdixTerm's initialiser (against the original's)", true, before == after && after == 0, "%d / %d", before, after);
}

void StaticInitStartupCheck() {
    ExceptionCheck();
    WindowCheck();
    ListCheck();
    LockCheck();
    DbcsAndWingdixCheck();
}
EI_STARTUP_CHECK(StaticInitStartupCheck);

// ---- Per-call wrappers ----------------------------------------------------------------------------

CException* __fastcall ExceptionConstructVerify(CException* self, void*, BOOL autoDelete) {
    CException copy = *self;
    s_exceptionConstruct(&copy, nullptr, autoDelete);
    CException* result = self->ConstructAutoDelete(autoDelete);
    Check("CException(BOOL) (against the original)", true, std::memcmp(&copy, self, sizeof(copy)) == 0, "%p", static_cast<void*>(self));
    return result;
}

template <SimpleConstruct* Original, OurSimpleConstruct Ours>
struct SimpleConstructCheck {
    static CSimpleException* __fastcall Verify(CSimpleException* self, void*, BOOL autoDelete, unsigned id) {
        CSimpleException copy = *self;
        (*Original)(&copy, nullptr, autoDelete, id);
        CSimpleException* result = (self->*Ours)(autoDelete, id);
        Check("simple exception constructor (against the original)", true, std::memcmp(&copy, self, sizeof(copy)) == 0, "%p",
              static_cast<void*>(self));
        return result;
    }
};
using ResourceCheck = SimpleConstructCheck<&s_resourceConstruct, &CSimpleException::ConstructResource>;
using UserCheck = SimpleConstructCheck<&s_userConstruct, &CSimpleException::ConstructUser>;
using MemoryCheck = SimpleConstructCheck<&s_memoryConstruct, &CSimpleException::ConstructMemory>;
using NotSupportedCheck = SimpleConstructCheck<&s_notSupportedConstruct, &CSimpleException::ConstructNotSupported>;

CCmdTarget* __fastcall CmdTargetConstructVerify(CCmdTarget* self) {
    CCmdTarget copy = *self;
    s_cmdTargetConstruct(&copy);
    CCmdTarget* result = self->Construct();
    Check("CCmdTarget::CCmdTarget (against the original)", true, std::memcmp(&copy, self, sizeof(copy)) == 0, "%p",
          static_cast<void*>(self));
    return result;
}

CWnd* __fastcall WndConstructVerify(CWnd* self, void*, HWND handle) {
    CWnd* result = self->Construct(handle);
    const bool ok = self->base.vtable == CWnd::Vtable() && self->base.refCount == 1 && self->window == handle && !self->owner &&
                    !self->flags && !self->controlContainer && !self->controlSite;
    Check("CWnd(HWND)", true, ok && result == self, "%p", static_cast<void*>(self));
    return result;
}

void __fastcall AddHeadVerify(CSimpleList* self, void*, void* element) {
    void* const head = self->head;
    self->AddHead(element);
    const bool ok = self->head == element && *reinterpret_cast<void**>(static_cast<char*>(element) + self->nextOffset) == head;
    Check("CSimpleList::AddHead", true, ok, "list %p", static_cast<void*>(self));
}

void __stdcall ClassInitVerify(mfc::impl::CRuntimeClass* newClass) {
    mfc::impl::AfxClassInit(newClass);
    Check("AfxClassInit", true, mfc::impl::AfxGetModuleState()->classList.head == newClass, "class %p", static_cast<void*>(newClass));
}

BOOL __cdecl CriticalInitVerify() {
    const BOOL result = mfc::impl::AfxCriticalInit();
    Check("AfxCriticalInit", TRUE, result, "");
    return result;
}

void __stdcall LockGlobalsVerify(int type) {
    const bool inRange = type >= 0 && type < mfc::impl::kLockTypes;
    mfc::impl::AfxLockGlobals(type);
    const CRITICAL_SECTION& section = mfc::impl::AfxResourceLocks()[type];
    const bool owned = mfc::impl::AfxCriticalWin32s() ||
                       (mfc::impl::AfxLockInit()[type] == 1 &&
                        section.OwningThread == reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(::GetCurrentThreadId())));
    Check("AfxLockGlobals", true, inRange && owned, "type %d", type);
}

void __stdcall UnlockGlobalsVerify(int type) {
    const bool ok = type >= 0 && type < mfc::impl::kLockTypes && (mfc::impl::AfxCriticalWin32s() || mfc::impl::AfxLockInit()[type] == 1);
    mfc::impl::AfxUnlockGlobals(type);
    Check("AfxUnlockGlobals", true, ok, "type %d", type);
}

BOOL __cdecl InitDbcsVerify() {
    const BOOL original = s_initDbcs();
    const BOOL ours = mfc::impl::AfxInitDbcs();
    Check("_AfxInitDBCS (against the original)", original, ours, "");
    return ours;
}

void __cdecl WingdixTermVerify() {
    mfc::impl::AfxWingdixTerm();
    Check("AfxWingdixTerm", true, !mfc::impl::AfxHalftoneBrush(), "");
}

// Only the static initialiser calls it, before the hooks: the start-up check runs ours.
void __cdecl WingdixTermInitVerify() { mfc::impl::AfxWingdixTermInit(); }

} // namespace

EI_VERIFY(0x0070205D, ExceptionConstructVerify, 6, s_exceptionConstruct);
EI_VERIFY(0x006E68C9, ResourceCheck::Verify, 7, s_resourceConstruct);
EI_VERIFY(0x006E6916, UserCheck::Verify, 7, s_userConstruct);
EI_VERIFY(0x006E6AEA, MemoryCheck::Verify, 7, s_memoryConstruct);
EI_VERIFY(0x006E6B37, NotSupportedCheck::Verify, 7, s_notSupportedConstruct);
EI_VERIFY(0x00703EAE, CmdTargetConstructVerify, 5, s_cmdTargetConstruct);
EI_VERIFY(0x00704546, WndConstructVerify, 0, s_unused);
EI_VERIFY(0x0070C4E9, AddHeadVerify, 7, s_addHead);
EI_VERIFY(0x00700674, ClassInitVerify, 0, s_unused);
EI_VERIFY(0x0070C3C5, CriticalInitVerify, 7, s_criticalInit);
EI_VERIFY(0x0070C458, LockGlobalsVerify, 9, s_lockGlobals);
EI_VERIFY(0x0070C4C8, UnlockGlobalsVerify, 7, s_unlockGlobals);
EI_VERIFY(0x0070DC4D, InitDbcsVerify, 6, s_initDbcs);
EI_VERIFY(0x0070B5F0, WingdixTermVerify, 0, s_unused);
EI_VERIFY(0x0070B600, WingdixTermInitVerify, 0, s_unused);
