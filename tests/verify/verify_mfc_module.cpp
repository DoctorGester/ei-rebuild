// Checks for MFC's static data (src/lib/mfc_module.cpp, ticket S16). The constructors run only in the
// static initialisers, before the hooks, so start-up checks drive their wrappers once and compare
// ours with what the original initialisers built:
// - AfxClassInit (image only): the original module state's class list (AfxGetModuleState 0070CE8C,
//   m_classList at +0x1C) is walked, the same classes are added in the initialisers' order (the
//   reverse of the list) to a scratch list through ours: the head, the link offset and every link
//   must come out as the original left them;
// - AUX_DATA's constructor, UpdateSysColors, UpdateSysMetrics and CCmdTarget's constructor: the
//   original on a copy, ours on the object, every byte compared; the scroll-metric helpers (they
//   write afxData itself) original then ours, the three fields compared; afxData against the one
//   the original built (SameAuxData: not the metrics that follow the DPI, nor the on-demand fonts);
// - CWnd(HWND) (no room for a trampoline): ours on scratch objects against the four static CWnds;
// - the DBCS test against the original and the flag its initialiser stored;
// - AfxDeleteObject, the GDI terminator and its registration: ours only (counted).
#include "verify.h"

#include "../../src/lib/mfc_module.h"

#include "ei/hook.h"

#include <cstring>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

using mfc::impl::AuxData;
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

// 0070D1AA: PUSH EBX; PUSH ESI; PUSH EDI; MOV ESI,ECX (5).
static AuxData*(__fastcall* s_auxConstruct)(AuxData*);
static AuxData* __fastcall AuxConstructCheck(AuxData* self) {
    AuxData theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    AuxData* expected = s_auxConstruct(&theirs);
    AuxData* actual = self->Construct();
    Check("AUX_DATA::AUX_DATA", true, expected == &theirs && actual == self && std::memcmp(&theirs, self, sizeof(theirs)) == 0,
          "version %04X", self->nWinVer);
    return actual;
}
EI_VERIFY(0x0070D1AA, AuxConstructCheck, 5, s_auxConstruct);

// 00703E12: PUSH ESI; PUSH EDI; MOV EDI,[GetSysColor] (8).
static void(__fastcall* s_updateColors)(AuxData*);
static void __fastcall UpdateColorsCheck(AuxData* self) {
    AuxData theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    s_updateColors(&theirs);
    self->UpdateSysColors();
    Check("AUX_DATA::UpdateSysColors", true, std::memcmp(&theirs, self, sizeof(theirs)) == 0, "");
}
EI_VERIFY(0x00703E12, UpdateColorsCheck, 8, s_updateColors);

// 00703E56: PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,[GetSystemMetrics] (9).
static void(__fastcall* s_updateMetrics)(AuxData*);
static void __fastcall UpdateMetricsCheck(AuxData* self) {
    AuxData theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    s_updateMetrics(&theirs);
    self->UpdateSysMetrics();
    Check("AUX_DATA::UpdateSysMetrics", true, std::memcmp(&theirs, self, sizeof(theirs)) == 0, "");
}
EI_VERIFY(0x00703E56, UpdateMetricsCheck, 9, s_updateMetrics);

namespace {
// The three afxData fields the scroll-metric helpers write, after the original and after ours.
template <void(__cdecl* Ours)()>
struct ScrollMetrics {
    static inline void(__cdecl* original)();
    static inline const char* name;
    static void __cdecl Run() {
        AuxData& data = mfc::impl::AfxData();
        original();
        int x = data.cxVScroll, y = data.cyHScroll;
        BOOL marked = data.bMarked4;
        Ours();
        Check(name, true, x == data.cxVScroll && y == data.cyHScroll && marked == data.bMarked4, "%d %d %d", x, y, marked);
    }
};
using Scroll4 = ScrollMetrics<&mfc::impl::SetScrollMetrics4>;
using Scroll3 = ScrollMetrics<&mfc::impl::SetScrollMetrics3>;
struct Names {
    Names() {
        Scroll4::name = "AUX_DATA scroll metrics (version 4)";
        Scroll3::name = "AUX_DATA scroll metrics";
    }
} s_names;
} // namespace
// 0070D158: CMP [afxData.bWin4],0 (7); 0070D188: PUSH ESI; MOV ESI,[GetSystemMetrics] (7).
EI_VERIFY(0x0070D158, Scroll4::Run, 7, Scroll4::original);
EI_VERIFY(0x0070D188, Scroll3::Run, 7, Scroll3::original);

// 00703EAE: MOV EAX,ECX; PUSH 1; POP ECX (5).
static CCmdTarget*(__fastcall* s_cmdTarget)(CCmdTarget*);
static CCmdTarget* __fastcall CmdTargetCheck(CCmdTarget* self) {
    CCmdTarget theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    CCmdTarget* expected = s_cmdTarget(&theirs);
    CCmdTarget* actual = self->Construct();
    Check("CCmdTarget::CCmdTarget", true,
          expected == &theirs && actual == self && std::memcmp(&theirs, self, sizeof(theirs)) == 0, "");
    return actual;
}
EI_VERIFY(0x00703EAE, CmdTargetCheck, 5, s_cmdTarget);

// 00704546: ours only (a call within the first five bytes).
static CWnd* __fastcall WndCheck(CWnd* self, void*, HWND window) {
    CWnd* result = self->Construct(window);
    bool zero = true;
    for (std::uint32_t m : self->members) zero = zero && m == 0;
    Check("CWnd::CWnd(HWND)", true, result == self && self->hWnd == window && self->base.vtable == CWnd::Vtable() && zero, "");
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

static void __stdcall DeleteObjectCheck(HGDIOBJ* object) {
    mfc::impl::AfxDeleteObject(object);
    Check("AfxDeleteObject", true, *object == nullptr, "");
}
EI_VERIFY(0x0070AA54, DeleteObjectCheck, 0, s_unused);

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

// The fields that cannot have changed since the static initialisers: the borders, the colours,
// brushes and cursors, then hbmMenuDot to the end. Not the metrics that follow the screen's DPI (the
// icon size, the pixels per inch; the scroll sizes): the process is made DPI-aware after the static
// initialisers, so a construction now sees other values (an icon of 64 where it was 32).
bool SameAuxData(const AuxData& a, const AuxData& b) {
    return a.cxBorder2 == b.cxBorder2 && a.cyBorder2 == b.cyBorder2 &&
           std::memcmp(&a.hbrWindowFrame, &b.hbrWindowFrame,
                       offsetof(AuxData, hStatusFont) - offsetof(AuxData, hbrWindowFrame)) == 0 &&
           std::memcmp(&a.hbmMenuDot, &b.hbmMenuDot, sizeof(AuxData) - offsetof(AuxData, hbmMenuDot)) == 0;
}

void StaticDataStartupCheck() {
    ClassListCheck();

    AuxData scratch;
    std::memset(&scratch, 0xCD, sizeof(scratch));
    AuxConstructCheck(&scratch);
    Check("afxData (startup)", true, SameAuxData(scratch, mfc::impl::AfxData()), "version %04X", scratch.nWinVer);
    UpdateColorsCheck(&scratch);
    UpdateMetricsCheck(&scratch);
    AuxData& data = mfc::impl::AfxData();
    const int cx = data.cxVScroll, cy = data.cyHScroll;
    const BOOL marked = data.bMarked4;
    Scroll4::Run();
    Scroll3::Run();
    data.cxVScroll = cx;   // the state the game started with
    data.cyHScroll = cy;
    data.bMarked4 = marked;

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
