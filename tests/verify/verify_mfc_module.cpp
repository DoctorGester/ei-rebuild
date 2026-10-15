// Checks for MFC's static data (src/lib/mfc_module.cpp, ticket S16). The constructors run in the
// static initialisers, before the hooks (CCmdTarget's and CWnd's also later, under MFC objects the
// game makes: its worker threads), so a start-up check drives their wrappers once and compares
// ours with what the original initialisers built:
// - AfxClassInit (image only): the original module state's class list (AfxGetModuleState 0070CE8C,
//   m_classList at +0x1C) is walked, the same classes are added in the initialisers' order (the
//   reverse of the list) to a scratch list through ours: the head, the link offset and every link
//   must come out as the original left them;
// - CCmdTarget's constructor: the original on a copy, ours on the object, every byte compared;
// - CWnd(HWND) (a call within the first five bytes): ours on scratch objects against the four
//   static CWnds;
// - the DBCS test against the original and the flag its initialiser stored;
// - the GDI terminator and its registration: ours only (counted).
#include "verify.h"

#include "../../src/lib/mfc_module.h"

#include "ei/hook.h"

#include <cstring>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

using mfc::impl::CCmdTarget;
using mfc::impl::CWnd;
using ei::verify::Check;

namespace {

// AFX_MODULE_STATE as far as the class list.
struct ModuleState {
    std::uint8_t opaque[0x1C];
    mfc::impl::SimpleList classList;   // +0x1C
};

__declspec(naked) ModuleState* __stdcall AfxGetModuleState() EI_ORIGINAL(0x0070CE8C)

void ClassListCheck() {
    using mfc::impl::CRuntimeClass;
    const mfc::impl::SimpleList& theirs = AfxGetModuleState()->classList;
    std::vector<CRuntimeClass*> classes;
    std::vector<CRuntimeClass*> links;
    for (void* p = theirs.head; p && classes.size() < 1000;
         p = *reinterpret_cast<void**>(static_cast<unsigned char*>(p) + theirs.nextOffset)) {
        classes.push_back(static_cast<CRuntimeClass*>(p));
        links.push_back(static_cast<CRuntimeClass*>(p)->nextClass);
    }
    mfc::impl::SimpleList ours{nullptr, offsetof(CRuntimeClass, nextClass)};
    for (auto it = classes.rbegin(); it != classes.rend(); ++it) ours.AddHead(*it);
    bool ok = !classes.empty() && ours.head == theirs.head && ours.nextOffset == theirs.nextOffset;
    for (std::size_t i = 0; i < classes.size(); ++i) {
        if (classes[i]->nextClass != links[i]) ok = false;
        classes[i]->nextClass = links[i];
    }
    Check("AfxClassInit (startup)", true, ok, "%d classes, offset %u", static_cast<int>(classes.size()),
          static_cast<unsigned>(theirs.nextOffset));
}

void* s_unused;

} // namespace

// 00703EAE: MOV EAX,ECX; PUSH 1; POP ECX (5).
static CCmdTarget*(__fastcall* s_cmdTarget)(CCmdTarget*);
static CCmdTarget* __fastcall CmdTargetCheck(CCmdTarget* self) {
    CCmdTarget theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    CCmdTarget* expected = s_cmdTarget(&theirs);
    CCmdTarget* actual = mfc::impl::ConstructCmdTarget(self);
    Check("CCmdTarget::CCmdTarget", true,
          expected == &theirs && actual == self && std::memcmp(&theirs, self, sizeof(theirs)) == 0, "");
    return actual;
}
EI_VERIFY(0x00703EAE, CmdTargetCheck, 5, s_cmdTarget);

// 00704546: ours only (a call within the first five bytes).
static CWnd* __fastcall WndCheck(CWnd* self, void*, HWND window) {
    CWnd* result = mfc::impl::ConstructWnd(self, window);
    const bool zero = !self->owner && !self->flags && !self->superProc && !self->modalResult && !self->dropTarget &&
                      !self->controlContainer && !self->controlSite;
    Check("CWnd::CWnd(HWND)", true, result == self && self->window == window && self->base.vtable == CWnd::Vtable() && zero,
          "");
    return result;
}
EI_VERIFY(0x00704546, WndCheck, 0, s_unused);

// 0070DC4D: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6). Pure.
static BOOL(__cdecl* s_oemDbcs)();
static BOOL __cdecl OemDbcsCheck() {
    BOOL expected = s_oemDbcs();
    BOOL actual = mfc::impl::OemCodePageIsDbcs();
    Check("OEM code page DBCS", expected, actual, "");
    return expected;
}
EI_VERIFY(0x0070DC4D, OemDbcsCheck, 6, s_oemDbcs);

static void __cdecl GdiTermCheck() {
    mfc::impl::AfxGdiTerm();
    Check("AfxGdiTerm", true, true, "");
}
EI_VERIFY(0x0070B5F0, GdiTermCheck, 0, s_unused);

static void __cdecl GdiTermInitCheck() {
    mfc::impl::AfxGdiTermInit();
    Check("AfxGdiTerm registration", true, true, "");
}
EI_VERIFY(0x0070B600, GdiTermInitCheck, 0, s_unused);

namespace {

void StaticDataStartupCheck() {
    ClassListCheck();

    CCmdTarget target;
    std::memset(&target, 0xCD, sizeof(target));
    CmdTargetCheck(&target);

    struct StaticWnd {
        std::uintptr_t address;
        HWND window;
    };
    const StaticWnd wnds[] = {{0x007CAA28, HWND_TOP}, {0x007CA9E8, HWND_BOTTOM}, {0x007CA9A8, HWND_TOPMOST},
                              {0x007CA968, HWND_NOTOPMOST}};
    for (const StaticWnd& w : wnds) {
        CWnd wnd;
        std::memset(&wnd, 0xCD, sizeof(wnd));
        WndCheck(&wnd, nullptr, w.window);
        Check("static CWnd (startup)", true, std::memcmp(&wnd, reinterpret_cast<const void*>(w.address), sizeof(wnd)) == 0,
              "%08X", static_cast<unsigned>(w.address));
    }

    BOOL dbcs = OemDbcsCheck();
    Check("OEM code page DBCS flag (startup)", *reinterpret_cast<const BOOL*>(0x007CAA98), dbcs, "");
}

} // namespace

EI_STARTUP_CHECK(StaticDataStartupCheck);
