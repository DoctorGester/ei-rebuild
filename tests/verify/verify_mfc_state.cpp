// Checks for MFC's thread and process locals, AUX_DATA, AfxDeleteObject, the simple exceptions'
// destructors and CWnd / CCmdTarget's destructors (src/lib/mfc_state.cpp, mfc_exception.cpp,
// mfc_window.cpp; ticket S3d).
//
// Against the original: a start-up check runs each of FreeSlot, ~CProcessLocalObject,
// AfxDeleteObject, the four exception destructors, ~CCmdTarget and ~CWnd on twin objects of its own,
// one through the original's trampoline and one through ours (fake held objects record their
// deletion, a fake dispatch its Disconnect, bitmaps their deletion), and compares what each leaves.
// AUX_DATA's constructor runs through the original's trampoline and ours into storage of their own
// (the original's calls to UpdateSysMetrics / UpdateSysColors reach ours; 0070D158 / 0070D188 run
// inside ours, nothing else calls them); both are compared whole, and with afxData, which the static
// initialiser built, without the metrics: InstallHooks makes the process DPI aware afterwards, so
// the scroll and icon sizes and the pixels per inch can differ from afxData's. The four exception
// destructors' wrappers also run the original on a copy of the vtable.
//
// Ours only: the destructors the exit functions run on MFC's real state (the thread locals free a
// TLS slot of every thread, the process locals delete their object, ~CWnd runs on the four static
// windows, AUX_DATA's destructor deletes the menu-dot bitmap). Their wrappers check what is left: the
// slot cleared, its flag and every thread's value gone; the object pointer kept; CWnd's vtable, the
// control site let go; the bitmap handle cleared. They run at a normal exit, after
// harness_report.txt: their counts are in ei_hook.log. UpdateSysColors / UpdateSysMetrics are reached
// only from window messages the game never gets: their wrappers compare the fields with the API.
#include "verify.h"

#include "../../src/lib/mfc_exception.h"
#include "../../src/lib/mfc_state.h"
#include "../../src/lib/mfc_window.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace {

using ei::verify::Check;
using mfc::impl::AuxData;
using mfc::impl::CException;
using mfc::impl::CNoTrackObject;
using mfc::impl::CProcessLocalObject;
using mfc::impl::CSlotData;
using mfc::impl::CThreadData;
using mfc::impl::CThreadLocalObject;
using mfc::impl::CThreadSlotData;
using mfc::impl::CWnd;
void* s_unused;

void(__fastcall* s_freeSlot)(CThreadSlotData*, void*, int);
void(__fastcall* s_processLocalDestruct)(CProcessLocalObject*);
void(__stdcall* s_deleteObject)(HGDIOBJ*);
void(__fastcall* s_resourceDestruct)(CException*);
void(__fastcall* s_userDestruct)(CException*);
void(__fastcall* s_memoryDestruct)(CException*);
void(__fastcall* s_notSupportedDestruct)(CException*);
void(__fastcall* s_cmdTargetDestruct)(mfc::impl::CCmdTarget*);
void(__fastcall* s_wndDestruct)(CWnd*);
AuxData*(__fastcall* s_auxDataConstruct)(AuxData*);

// An address inside game.exe's image (a vtable of the exe's).
bool InImage(const void* p) {
    const auto a = reinterpret_cast<std::uintptr_t>(p);
    return a >= 0x00400000u && a < 0x007D7000u;
}

// ---- Fakes ----------------------------------------------------------------------------------------

// A CNoTrackObject (slot 0 the deleting destructor) or a CObject (slot 1) that records its deletion.
class FakeHeld {
public:
    virtual void* DeletingDestructor(unsigned flags) {
        ++deletes;
        lastFlags = flags;
        return this;
    }
    int deletes = 0;
    unsigned lastFlags = 0;
};
class FakeObject {
public:
    virtual void GetRuntimeClass() {}
    virtual void* DeletingDestructor(unsigned flags) {
        ++deletes;
        lastFlags = flags;
        return this;
    }
    int deletes = 0;
    unsigned lastFlags = 0;
};

// A COleDispatchImpl's vtable: Disconnect (slot 7) records the `this` it got (the CCmdTarget's
// m_xDispatch). The object is only the source of the vtable pointer.
int s_disconnects;
const void* s_disconnected;
class FakeDispatch {
public:
    virtual void QueryInterface() {}
    virtual void AddRef() {}
    virtual void Release() {}
    virtual void GetTypeInfoCount() {}
    virtual void GetTypeInfo() {}
    virtual void GetIDsOfNames() {}
    virtual void Invoke() {}
    virtual void Disconnect() {
        ++s_disconnects;
        s_disconnected = this;
    }
};
void* FakeDispatchVtable() {
    static FakeDispatch dispatch;
    return *reinterpret_cast<void**>(&dispatch);
}

// ---- CThreadSlotData::FreeSlot, CProcessLocalObject: twins through the original and ours ----------

// Three slots (1 and 2 in use), two threads: the first with values for slots 0-2, the second for
// slot 0 only.
struct SlotFixture {
    CThreadSlotData data;
    CSlotData slots[3];
    CThreadData threads[2];
    CNoTrackObject* first[3];
    CNoTrackObject* second[1];
    FakeHeld held[3];

    SlotFixture() {
        std::memset(&data, 0, sizeof(data));
        ::InitializeCriticalSection(&data.section);
        data.allocated = 3;
        data.max = 3;
        data.slots = slots;
        data.threads = &threads[0];
        slots[0] = {0, nullptr};
        slots[1] = {CThreadSlotData::kSlotUsed | 2, nullptr};
        slots[2] = {CThreadSlotData::kSlotUsed, nullptr};
        threads[0] = {nullptr, &threads[1], 3, first};
        threads[1] = {nullptr, nullptr, 1, second};
        first[0] = reinterpret_cast<CNoTrackObject*>(&held[0]);
        first[1] = nullptr;
        first[2] = reinterpret_cast<CNoTrackObject*>(&held[1]);
        second[0] = reinterpret_cast<CNoTrackObject*>(&held[2]);
    }
    ~SlotFixture() { ::DeleteCriticalSection(&data.section); }

    // What a FreeSlot leaves: the flags, which values are set, every deletion and its flags.
    bool Same(const SlotFixture& other) const {
        for (int i = 0; i < 3; ++i) {
            if (slots[i].flags != other.slots[i].flags || (first[i] == nullptr) != (other.first[i] == nullptr)) return false;
            if (held[i].deletes != other.held[i].deletes || held[i].lastFlags != other.held[i].lastFlags) return false;
        }
        return (second[0] == nullptr) == (other.second[0] == nullptr);
    }
};

void SlotCheck() {
    for (int slot : {2, 1, 0}) {
        SlotFixture original, ours;
        s_freeSlot(&original.data, nullptr, slot);
        ours.data.FreeSlot(slot);
        Check("CThreadSlotData::FreeSlot (against the original)", true, original.Same(ours), "slot %d", slot);
    }

    FakeHeld originalHeld, oursHeld;
    CProcessLocalObject original{reinterpret_cast<CNoTrackObject*>(&originalHeld)};
    CProcessLocalObject ours{reinterpret_cast<CNoTrackObject*>(&oursHeld)};
    s_processLocalDestruct(&original);
    ours.Destruct();
    CProcessLocalObject originalEmpty{nullptr}, oursEmpty{nullptr};
    s_processLocalDestruct(&originalEmpty);
    oursEmpty.Destruct();
    const bool same = originalHeld.deletes == oursHeld.deletes && originalHeld.lastFlags == oursHeld.lastFlags &&
                      original.object == reinterpret_cast<CNoTrackObject*>(&originalHeld) &&
                      ours.object == reinterpret_cast<CNoTrackObject*>(&oursHeld) && !originalEmpty.object && !oursEmpty.object;
    Check("CProcessLocalObject::~CProcessLocalObject (against the original)", true, same, "deletes %d / %d", originalHeld.deletes,
          oursHeld.deletes);
}

// ---- AfxDeleteObject, the exception destructors ----------------------------------------------------

// The process's GDI object count (GetObjectType on a deleted handle proved unreliable here: it still
// reported a bitmap after either version had deleted it).
DWORD GdiObjects() { return ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS); }

void DeleteObjectCheck() {
    HGDIOBJ original = ::CreateBitmap(8, 8, 1, 1, nullptr);
    HGDIOBJ ours = ::CreateBitmap(8, 8, 1, 1, nullptr);
    const DWORD before = GdiObjects();
    s_deleteObject(&original);
    const DWORD afterOriginal = GdiObjects();
    mfc::impl::AfxDeleteObject(&ours);
    const DWORD afterOurs = GdiObjects();
    HGDIOBJ originalNull = nullptr, oursNull = nullptr;
    s_deleteObject(&originalNull);
    mfc::impl::AfxDeleteObject(&oursNull);
    const bool same = !original && !ours && !originalNull && !oursNull && before - afterOriginal == afterOriginal - afterOurs;
    Check("AfxDeleteObject (against the original)", true, same, "%p %p %p %p, GDI objects %lu, %lu, %lu", original, ours, originalNull,
          oursNull, before, afterOriginal, afterOurs);
}

using ExceptionDestruct = void(__fastcall*)(CException*);

void ExceptionCheck(const char* name, ExceptionDestruct original, ExceptionDestruct ours) {
    CException a{reinterpret_cast<void*>(std::uintptr_t(0xCDCDCDCD)), 7};
    CException b = a;
    original(&a);
    ours(&b);
    Check(name, true, std::memcmp(&a, &b, sizeof(a)) == 0 && InImage(b.vtable), "vtable %p / %p", a.vtable, b.vtable);
}

template <ExceptionDestruct* Original, ExceptionDestruct Ours>
struct ExceptionDestructCheck {
    static void __fastcall Verify(CException* self) {
        CException copy = *self;
        (*Original)(&copy);
        Ours(self);
        Check("simple exception destructor (against the original)", true, copy.vtable == self->vtable, "%p", static_cast<void*>(self));
    }
};
using ResourceCheck = ExceptionDestructCheck<&s_resourceDestruct, mfc::impl::ResourceExceptionDestruct>;
using UserCheck = ExceptionDestructCheck<&s_userDestruct, mfc::impl::UserExceptionDestruct>;
using MemoryCheck = ExceptionDestructCheck<&s_memoryDestruct, mfc::impl::MemoryExceptionDestruct>;
using NotSupportedCheck = ExceptionDestructCheck<&s_notSupportedDestruct, mfc::impl::NotSupportedExceptionDestruct>;

// ---- CCmdTarget / CWnd destructors: twins with no window ------------------------------------------

struct WindowFixture {
    CWnd wnd;
    mfc::impl::COleControlSite site;
    FakeObject container;

    explicit WindowFixture(bool siteHoldsThis) {
        std::memset(&wnd, 0, sizeof(wnd));
        std::memset(&site, 0, sizeof(site));
        wnd.base.vtable = reinterpret_cast<void*>(std::uintptr_t(0xCDCDCDCD));
        wnd.base.dispatchVtable = FakeDispatchVtable();
        wnd.controlContainer = &container;
        wnd.controlSite = &site;
        site.windowControl = siteHoldsThis ? &wnd : reinterpret_cast<CWnd*>(&site);
    }
};

void WindowCheck() {
    for (bool siteHoldsThis : {true, false}) {
        WindowFixture original(siteHoldsThis), ours(siteHoldsThis);
        s_disconnects = 0;
        s_wndDestruct(&original.wnd);
        const bool originalDisconnected = s_disconnects == 1 && s_disconnected == &original.wnd.base.dispatchVtable;
        s_disconnects = 0;
        ours.wnd.Destruct();
        const bool oursDisconnected = s_disconnects == 1 && s_disconnected == &ours.wnd.base.dispatchVtable;
        const bool siteSame = siteHoldsThis ? !original.site.windowControl && !ours.site.windowControl
                                            : original.site.windowControl == reinterpret_cast<CWnd*>(&original.site) &&
                                                  ours.site.windowControl == reinterpret_cast<CWnd*>(&ours.site);
        const bool same = original.wnd.base.vtable == ours.wnd.base.vtable && ours.wnd.base.vtable == CWnd::Vtable() &&
                          original.container.deletes == 1 && ours.container.deletes == 1 && original.container.lastFlags == 1 &&
                          ours.container.lastFlags == 1 && originalDisconnected == oursDisconnected && oursDisconnected && siteSame;
        Check("CWnd::~CWnd (against the original)", true, same, "site holds the window %d", siteHoldsThis);
    }

    mfc::impl::CCmdTarget original{}, ours{}, originalNone{}, oursNone{};
    original.dispatchVtable = ours.dispatchVtable = FakeDispatchVtable();
    s_disconnects = 0;
    s_cmdTargetDestruct(&original);
    s_cmdTargetDestruct(&originalNone);
    const int originalCount = s_disconnects;
    s_disconnects = 0;
    ours.Destruct();
    oursNone.Destruct();
    Check("CCmdTarget::~CCmdTarget (against the original)", originalCount, s_disconnects, "disconnects %d / %d", originalCount,
          s_disconnects);
}

// ---- AUX_DATA against afxData -----------------------------------------------------------------------

// The first byte where two records differ (-1: none).
int FirstDifference(const AuxData& a, const AuxData& b) {
    for (unsigned i = 0; i < sizeof(AuxData); ++i)
        if (reinterpret_cast<const std::uint8_t*>(&a)[i] != reinterpret_cast<const std::uint8_t*>(&b)[i]) return static_cast<int>(i);
    return -1;
}

void AuxDataCheck() {
    AuxData original{}, ours{};
    s_auxDataConstruct(&original);
    ours.Construct();
    int first = FirstDifference(original, ours);
    Check("AUX_DATA::AUX_DATA (against the original)", -1, first, "first difference at +0x%X", first);

    // afxData without the DPI-dependent metrics.
    AuxData global = mfc::impl::AfxData();
    global.vScrollWidth = ours.vScrollWidth;
    global.hScrollHeight = ours.hScrollHeight;
    global.iconWidth = ours.iconWidth;
    global.iconHeight = ours.iconHeight;
    global.pixelsPerInchX = ours.pixelsPerInchX;
    global.pixelsPerInchY = ours.pixelsPerInchY;
    first = FirstDifference(global, ours);
    Check("AUX_DATA::AUX_DATA (against afxData, no metrics)", -1, first, "first difference at +0x%X", first);
}

void StateStartupCheck() {
    SlotCheck();
    DeleteObjectCheck();
    ExceptionCheck("CResourceException::~CResourceException (against the original)", s_resourceDestruct, mfc::impl::ResourceExceptionDestruct);
    ExceptionCheck("CUserException::~CUserException (against the original)", s_userDestruct, mfc::impl::UserExceptionDestruct);
    ExceptionCheck("CMemoryException::~CMemoryException (against the original)", s_memoryDestruct, mfc::impl::MemoryExceptionDestruct);
    ExceptionCheck("CNotSupportedException::~CNotSupportedException (against the original)", s_notSupportedDestruct,
                   mfc::impl::NotSupportedExceptionDestruct);
    WindowCheck();
    AuxDataCheck();
}
EI_STARTUP_CHECK(StateStartupCheck);

// ---- Exit wrappers (ours only) ----------------------------------------------------------------------

void __fastcall ThreadLocalVerify(CThreadLocalObject* self) {
    const int slot = self->slot;
    CThreadSlotData* data = mfc::impl::AfxThreadData();
    bool ok = slot >= 0 && (!data || slot < data->allocated);
    self->Destruct();
    ok = ok && self->slot == 0;
    if (ok && slot != 0 && data) {
        ok = !(data->slots[slot].flags & CThreadSlotData::kSlotUsed);
        for (const CThreadData* thread = data->threads; ok && thread; thread = thread->next)
            ok = slot >= thread->count || !thread->values[slot];
    }
    Check("CThreadLocalObject::~CThreadLocalObject", true, ok, "slot %d, slot data %p", slot, static_cast<void*>(data));
}

void __fastcall ProcessLocalVerify(CProcessLocalObject* self) {
    CNoTrackObject* object = self->object;
    const bool shape = !object || InImage(*reinterpret_cast<void**>(object));
    self->Destruct();
    Check("CProcessLocalObject::~CProcessLocalObject", true, shape && self->object == object, "object %p", static_cast<void*>(object));
}

void __stdcall DeleteObjectVerify(HGDIOBJ* object) {
    const HGDIOBJ handle = *object;
    mfc::impl::AfxDeleteObject(object);
    Check("AfxDeleteObject", true, !*object, "handle %p", handle);
}

void __fastcall AuxDataDestructVerify(AuxData* self) {
    self->Destruct();
    Check("AUX_DATA::~AUX_DATA", true, !self->menuDot, "afxData %p", static_cast<void*>(self));
}

void __fastcall UpdateSysColorsVerify(AuxData* self) {
    self->UpdateSysColors();
    const bool ok = self->buttonFace == ::GetSysColor(COLOR_BTNFACE) && self->windowFrame == ::GetSysColor(COLOR_WINDOWFRAME) &&
                    self->buttonFaceBrush == ::GetSysColorBrush(COLOR_BTNFACE) &&
                    self->windowFrameBrush == ::GetSysColorBrush(COLOR_WINDOWFRAME);
    Check("AUX_DATA::UpdateSysColors", true, ok, "");
}

void __fastcall UpdateSysMetricsVerify(AuxData* self) {
    self->UpdateSysMetrics();
    const bool ok = self->iconWidth == ::GetSystemMetrics(SM_CXICON) && self->iconHeight == ::GetSystemMetrics(SM_CYICON) &&
                    self->pixelsPerInchX > 0 && self->pixelsPerInchY > 0;
    Check("AUX_DATA::UpdateSysMetrics", true, ok, "");
}

void __fastcall WndDestructVerify(CWnd* self) {
    const HWND window = self->window;
    mfc::impl::COleControlSite* site = self->controlSite;
    self->Destruct();
    const bool ok = self->base.vtable == CWnd::Vtable() && self->window == window && (!site || site->windowControl != self);
    Check("CWnd::~CWnd", true, ok, "window object %p", static_cast<void*>(self));
}

void __fastcall CmdTargetDestructVerify(mfc::impl::CCmdTarget* self) {
    const void* vtable = self->vtable;
    self->Destruct();
    Check("CCmdTarget::~CCmdTarget", true, self->vtable == vtable, "object %p", static_cast<void*>(self));
}

// Only the static initialiser calls it, before the hooks: the start-up check compares it.
AuxData* __fastcall AuxDataConstructVerify(AuxData* self) { return self->Construct(); }

void __fastcall FreeSlotVerify(CThreadSlotData* self, void*, int slot) {
    self->FreeSlot(slot);
    Check("CThreadSlotData::FreeSlot", true, !(self->slots[slot].flags & CThreadSlotData::kSlotUsed), "slot %d", slot);
}

} // namespace

EI_VERIFY(0x0070C71F, FreeSlotVerify, 9, s_freeSlot);
EI_VERIFY(0x0070CA59, ThreadLocalVerify, 0, s_unused);
EI_VERIFY(0x0070CC2D, ThreadLocalVerify, 0, s_unused);
EI_VERIFY(0x0070D5F9, ThreadLocalVerify, 0, s_unused);
EI_VERIFY(0x0070CAD2, ProcessLocalVerify, 7, s_processLocalDestruct);
EI_VERIFY(0x0070CE77, ProcessLocalVerify, 0, s_unused);
EI_VERIFY(0x0070D61F, ProcessLocalVerify, 0, s_unused);
EI_VERIFY(0x0070DC48, ProcessLocalVerify, 0, s_unused);
EI_VERIFY(0x0070AA54, DeleteObjectVerify, 5, s_deleteObject);
EI_VERIFY(0x0070D1AA, AuxDataConstructVerify, 5, s_auxDataConstruct);
EI_VERIFY(0x0070E181, AuxDataDestructVerify, 0, s_unused);
EI_VERIFY(0x00703E12, UpdateSysColorsVerify, 0, s_unused);
EI_VERIFY(0x00703E56, UpdateSysMetricsVerify, 0, s_unused);
EI_VERIFY(0x006E690F, ResourceCheck::Verify, 6, s_resourceDestruct);
EI_VERIFY(0x006E695C, UserCheck::Verify, 6, s_userDestruct);
EI_VERIFY(0x006E6B30, MemoryCheck::Verify, 6, s_memoryDestruct);
EI_VERIFY(0x006E6B7D, NotSupportedCheck::Verify, 6, s_notSupportedDestruct);
EI_VERIFY(0x00703EC8, CmdTargetDestructVerify, 5, s_cmdTargetDestruct);
EI_VERIFY(0x00704FE3, WndDestructVerify, 5, s_wndDestruct);
