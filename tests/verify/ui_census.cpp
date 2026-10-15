// Interface drawing census (verify build only; ticket N0): see ui_census.h, docs/notes/ui-native.md
// (section 1, the inventory it checks) and docs/notes/ui-census.md (the merged result).
//
// What is hooked:
// - the gdi32 / user32 import slots of game.exe (at the harness's start) and of the other modules of
//   the game's folder, addon.dll among them (at the first frame, once they are loaded): text, metric,
//   fill, line, pixel, blit and object-selection calls, window DCs, and SendMessageA for
//   EM_FORMATRANGE. Our rewrites call the API through game.exe's slots (lib/gameapi.h), so they are
//   seen too. Each hook calls on through the slot's previous value of the module that called it;
//   nothing else changes.
// - Blt, BltFast, Lock and Unlock in the vtables of the window surfaces' DirectDraw surfaces (with
//   "cpuwindows" outside the switch-over every holder has one, on the new renderer's memory): who
//   copies or locks a surface and whether the lock wrote it.
// - the owner of each surface: CUiWindow's AddSurface / RemoveSurfaceAt / ClearSurfaces tell
//   g_uiSurfaceOwnerObserver (ui_window.cpp); the interface's own surfaces (backdrops, frame rate,
//   status text) and the console's are read from 007C7D50.
//
// How a call is placed: a DC with a surface's DIB section selected (005FDC00's memory DC, seen as the
// bitmap goes in through SelectObject) belongs to that surface; window DCs (GetDC) and other DCs are
// counted under their own names. The call site is the hook's return address plus the code addresses
// found further up the stack that follow a CALL (the report tool names them; the first ones may be
// MFC's CDC members or the surface helpers).
//
// Drawing we do not intercept: every DC session (bitmap in .. bitmap out) checksums the DIB at both
// ends; a change with no hooked drawing call in between is reported. At each frame every known DIB is
// checksummed again; a change with no hooked drawing, write lock, blit into it or session since the
// last frame is reported too.
#include "ui_census.h"
#include "slot_recorder.h"

#include "../../src/game/console.h"
#include "../../src/game/dx_surface.h"
#include "../../src/game/texture.h"
#include "../../src/game/ui.h"
#include "../../src/hook/log.h"
#include "../../src/render/render.h"

#include <windows.h>
#include <ddraw.h>
#include <intrin.h>
#include <psapi.h>
#include <richedit.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ei::verify::census {

namespace {

constexpr std::uintptr_t kInterface = 0x007C7D50;   // CInterface3D
constexpr unsigned kMaxDetails = 8;                  // distinct details kept per row

// --- code modules and call chains ----------------------------------------------------------------

struct CodeRange {
    std::uintptr_t begin, end;   // an executable section
    std::uintptr_t base;         // its module
    char tag;                    // G game.exe, E ei_hook.dll, A addon.dll, O other
    std::string name;
};

// --- the hooked API -------------------------------------------------------------------------------

enum Api {
    kDrawTextA, kDrawTextExA, kTabbedTextOutA, kGetTabbedTextExtentA, kFillRect, kFrameRect, kInvertRect,
    kDrawFocusRect, kDrawEdge, kDrawIconEx, kGetDC, kGetWindowDC, kReleaseDC, kSendMessageA,
    kExtTextOutA, kTextOutA, kGetTextExtentPoint32A, kGetTextExtentPointA, kGetTextExtentExPointA,
    kGetTextMetricsA, kGetCharWidthA, kGetCharWidth32A, kGetCharABCWidthsA, kGetTextFaceA, kGetDeviceCaps,
    kMoveToEx, kLineTo, kPolyline, kRectangle, kEllipse, kSetPixel, kSetPixelV, kGetPixel, kBitBlt,
    kStretchBlt, kPatBlt, kStretchDIBits, kSetDIBitsToDevice, kSelectObject, kCreateFontA,
    kCreateFontIndirectA, kCreatePen, kCreateSolidBrush, kSetTextColor, kSetBkColor, kSetBkMode,
    kSetTextAlign,
    kApiCount
};

struct PatchedModule {
    std::uintptr_t begin, end;          // the whole image
    void* original[kApiCount];          // its slots' values before ours (null: not imported)
};

using BltFn = HRESULT(WINAPI*)(IDirectDrawSurface7*, RECT*, IDirectDrawSurface7*, RECT*, DWORD, DDBLTFX*);
using BltFastFn = HRESULT(WINAPI*)(IDirectDrawSurface7*, DWORD, DWORD, IDirectDrawSurface7*, RECT*, DWORD);
using LockFn = HRESULT(WINAPI*)(IDirectDrawSurface7*, RECT*, DDSURFACEDESC2*, DWORD, HANDLE);
using UnlockFn = HRESULT(WINAPI*)(IDirectDrawSurface7*, RECT*);
constexpr int kBltSlot = 5, kBltFastSlot = 7, kLockSlot = 25, kUnlockSlot = 32;

struct PatchedVtable {
    void** vtable;
    BltFn blt;
    BltFastFn bltFast;
    LockFn lock;
    UnlockFn unlock;
};

// --- what is counted ------------------------------------------------------------------------------

// A DIB section a surface draws into.
struct Dib {
    const void* bits = nullptr;
    std::size_t bytes = 0;
    int width = 0, height = 0;
    std::string owner;            // window vtable and index, interface field, "unowned" (a surface's) or "dib"
    std::uint64_t hash = 0;       // at the last frame
    bool touched = false;         // drawn, written or copied into since the last frame
    std::string lastSession;      // the chain that opened its last DC session
};

// 005FDC00 .. 005FDC60 as seen through SelectObject: the DIB in a memory DC.
struct Session {
    HBITMAP bitmap;
    std::uint64_t hash;
    unsigned draws;
    std::string chain;
};

struct Row {
    const char* kind;
    unsigned count = 0, frames = 0, lastFrame = ~0u;
    std::vector<std::string> details;
    bool moreDetails = false;
};

struct SurfaceStats {
    unsigned alive = 0, drawn = 0, changed = 0, run = 0, maxRun = 0, sessions = 0;
    unsigned uncaughtSession = 0, uncaughtFrame = 0, lastDrawn = ~0u;
    bool pending = false;         // drawn or changed since the last frame (by any of its DIBs)
};

struct PendingLock {
    std::uint64_t hash;
    std::string chain;
};

struct State {
    std::mutex lock;
    bool enabled = false, installed = false, modulesHooked = false;
    unsigned frame = 0;
    std::vector<CodeRange> code;
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> data;   // readable non-executable sections
    std::vector<std::uintptr_t> starts;   // sorted: function starts (BuildStarts)
    std::vector<std::uintptr_t> taken;    // sorted: the starts whose address is stored somewhere (virtual methods, callbacks)
    std::size_t startsFor = 0;            // code.size() when `starts` was built
    std::vector<PatchedModule> patched;
    std::set<std::uintptr_t> patchedBases;
    std::vector<PatchedVtable> vtables;
    void* real[kApiCount] = {};
    std::unordered_map<const CUiSurface*, const CUiWindow*> owners;
    std::unordered_map<HBITMAP, Dib> dibs;
    std::unordered_map<HDC, Session> sessions;
    std::unordered_map<HDC, std::string> windowDcs;
    std::unordered_map<const void*, HBITMAP> ddSurfaces;   // IDirectDrawSurface7* -> its DIB
    std::unordered_map<const void*, PendingLock> locks;
    std::map<std::string, Row> rows;                        // owner \t size \t primitive \t chain
    std::map<std::string, SurfaceStats> stats;              // owner \t size
};

// Function-local: the harness starts the census from its static constructor, before this file's
// globals would be constructed.
State& S() {
    static State state;
    return state;
}

// --- reading game memory that may be gone ---------------------------------------------------------

bool SafeRead(const void* from, void* to, std::size_t size) {
    __try {
        std::memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint64_t HashBytes(const void* bits, std::size_t bytes) {
    const auto* p = static_cast<const std::uint32_t*>(bits);
    std::size_t n = bytes / 8;
    std::uint32_t a = 0x811C9DC5u, b = 0x9E3779B9u;
    for (std::size_t i = 0; i < n; ++i) {
        a = (a ^ p[2 * i]) * 0x01000193u;
        b = (b ^ p[2 * i + 1]) * 0x85EBCA6Bu;
    }
    return (std::uint64_t(a) << 32) | b;
}

std::uint64_t SafeHash(const void* bits, std::size_t bytes) {
    __try {
        return HashBytes(bits, bytes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The members of a CUiSurface the census reads; false when it cannot be read or is no surface.
struct SurfaceView {
    HDC dc = nullptr;
    CDXSurface* holder = nullptr;
    int width = 0, height = 0;
};

bool ReadSurface(const CUiSurface* surface, SurfaceView* view) {
    if (!surface) return false;
    alignas(4) unsigned char raw[sizeof(CUiSurface)];
    if (!SafeRead(surface, raw, sizeof(raw))) return false;
    const auto* copy = reinterpret_cast<const CUiSurface*>(raw);
    auto vtable = reinterpret_cast<std::uintptr_t>(copy->vtable_);
    if (vtable < 0x0073B000 || vtable >= 0x00800000) return false;
    view->dc = copy->dc_;
    view->holder = copy->holder_;
    view->width = copy->width_;
    view->height = copy->height_;
    return true;
}

// --- labels ----------------------------------------------------------------------------------------

std::string Hex(std::uintptr_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", unsigned(value));
    return text;
}

std::string Size(int width, int height) {
    char text[24];
    std::snprintf(text, sizeof(text), "%dx%d", width, height);
    return text;
}

bool InGameVtables(std::uintptr_t vtable) { return vtable >= 0x0073B000 && vtable < 0x00800000; }

// The game vtable of an object that may wear a verify check's stand-in copy of it for the length of
// a call: verify_ui_window_slots.cpp's StandInTable (the real vtable in the entry before the table;
// a container's children while their slots 22 / 23 / 24 are dispatched, which is when the item
// panels, the unit panel and the HUD map draw) or slot_recorder.h's SlotRecorder (its real vtable).
// Such a copy lives on the heap: it gave N0's census window labels like W2FBD6554 (ui-census.md).
std::uintptr_t RealVtable(std::uintptr_t vtable) {
    if (vtable == 0 || InGameVtables(vtable)) return vtable;
    std::uintptr_t before = 0, slot0 = 0, realSlot0 = 0;
    if (!SafeRead(reinterpret_cast<const void*>(vtable), &slot0, sizeof(slot0))) return vtable;
    if (SafeRead(reinterpret_cast<const void*>(vtable - 4), &before, sizeof(before)) && InGameVtables(before) &&
        SafeRead(reinterpret_cast<const void*>(before), &realSlot0, sizeof(realSlot0)) && realSlot0 == slot0)
        return before;
    auto recorded = reinterpret_cast<std::uintptr_t>(SlotRecorder::RealVtable());
    if (InGameVtables(recorded) && SafeRead(reinterpret_cast<const void*>(recorded), &realSlot0, sizeof(realSlot0)) &&
        realSlot0 == slot0)
        return recorded;
    return vtable;
}

// "W<window vtable>#<index in its surfaces>" for a window's surface.
std::string WindowLabel(const CUiWindow* window, const CUiSurface* surface) {
    std::uintptr_t vtable = 0;
    if (!SafeRead(window, &vtable, sizeof(vtable))) return "W?";
    vtable = RealVtable(vtable);
    CUiSurface** data = nullptr;
    int size = 0;
    SafeRead(&window->surfaces_.data, &data, sizeof(data));
    SafeRead(&window->surfaces_.size, &size, sizeof(size));
    int index = -1;
    for (int i = 0; i < size && i < 64 && data; ++i) {
        CUiSurface* entry = nullptr;
        if (SafeRead(data + i, &entry, sizeof(entry)) && entry == surface) index = i;
    }
    return "W" + Hex(vtable) + "#" + std::to_string(index);
}

// Every surface the census can name: a window's (with the window) or one of the interface's own
// (with its name).
struct Candidate {
    const CUiSurface* surface;
    const CUiWindow* window;
    const char* name;
    std::string Label() const { return window ? WindowLabel(window, surface) : name; }
};

std::vector<Candidate> Candidates(const State& s) {
    std::vector<Candidate> out;
    out.reserve(s.owners.size() + 6);
    for (const auto& [surface, window] : s.owners) out.push_back({surface, window, nullptr});
    const auto* ui = reinterpret_cast<const CInterface3D*>(kInterface);
    out.push_back({ui->backdrop_, nullptr, "backdrop"});
    out.push_back({ui->secondBackdrop_, nullptr, "backdrop2"});
    out.push_back({ui->fpsSurface_, nullptr, "fps"});
    out.push_back({ui->statusSurface_, nullptr, "status"});
    if (const console::ConsoleWindow* console = ui->console_) {
        void* surface = nullptr;
        if (SafeRead(&console->surface_, &surface, sizeof(surface)))
            out.push_back({static_cast<const CUiSurface*>(surface), nullptr, "console"});
    }
    return out;
}

// The surface whose DC is `dc` (005FDC00 keeps it in dc_ while held), named; empty when none.
std::string NameByDc(const State& s, HDC dc, SurfaceView* found) {
    for (const Candidate& c : Candidates(s)) {
        SurfaceView view;
        if (ReadSurface(c.surface, &view) && view.dc == dc) {
            *found = view;
            return c.Label();
        }
    }
    return {};
}

// --- call chains ----------------------------------------------------------------------------------

void AddCodeRanges(State& s, HMODULE module, char tag, const char* name) {
    auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        std::uintptr_t begin = base + section->VirtualAddress;
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
            if ((section->Characteristics & IMAGE_SCN_MEM_READ) && !(section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE))
                s.data.push_back({begin, begin + section->Misc.VirtualSize});
            continue;
        }
        s.code.push_back({begin, begin + section->Misc.VirtualSize, base, tag, name});
    }
}

const CodeRange* CodeAt(const State& s, std::uintptr_t address) {
    for (const CodeRange& r : s.code)
        if (address >= r.begin + 8 && address < r.end) return &r;
    return nullptr;
}

// Whether `a` follows a CALL: E8 rel32, or FF /2 (register, [register], [register + disp8 / disp32],
// [disp32], with or without SIB).
bool FollowsCall(std::uintptr_t a) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(a);
    if (p[-5] == 0xE8) return true;
    auto isCall = [](std::uint8_t opcode, std::uint8_t modrm) { return opcode == 0xFF && ((modrm >> 3) & 7) == 2; };
    if (isCall(p[-2], p[-1]) && ((p[-1] >> 6) == 3 || ((p[-1] >> 6) == 0 && (p[-1] & 7) != 4 && (p[-1] & 7) != 5)))
        return true;
    if (isCall(p[-3], p[-2]) && (p[-2] >> 6) == 1 && (p[-2] & 7) != 4) return true;     // [reg + disp8]
    if (isCall(p[-3], p[-2]) && (p[-2] >> 6) == 0 && (p[-2] & 7) == 4) return true;     // [SIB]
    if (isCall(p[-4], p[-3]) && (p[-3] >> 6) == 1 && (p[-3] & 7) == 4) return true;     // [SIB + disp8]
    if (isCall(p[-6], p[-5]) && (((p[-5] >> 6) == 2 && (p[-5] & 7) != 4) || ((p[-5] >> 6) == 0 && (p[-5] & 7) == 5)))
        return true;                                                                    // [reg + disp32], [disp32]
    if (isCall(p[-7], p[-6]) && (p[-6] >> 6) == 2 && (p[-6] & 7) == 4) return true;     // [SIB + disp32]
    return false;
}

void AppendFrame(std::string& out, std::uintptr_t a, const CodeRange& r) {
    char text[48];
    if (r.tag == 'G') std::snprintf(text, sizeof(text), "%08X", unsigned(a));
    else if (r.tag == 'O') std::snprintf(text, sizeof(text), "%s+%X", r.name.c_str(), unsigned(a - r.base));
    else std::snprintf(text, sizeof(text), "%c+%X", r.tag, unsigned(a - r.base));
    if (!out.empty()) out += ' ';
    out += text;
}

const CodeRange* CodeContaining(const State& s, std::uintptr_t address) {
    for (const CodeRange& r : s.code)
        if (address >= r.begin && address < r.end) return &r;
    return nullptr;
}

// Whether code may begin at `target`: after the padding between functions (MSVC6 pads with 90, newer
// MSVC with CC) or right after a RET (C3, C2 imm16).
bool AfterFunctionEnd(std::uintptr_t target) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(target);
    return p[-1] == 0xCC || p[-1] == 0x90 || p[-1] == 0xC3 || (p[-3] == 0xC2 && p[-1] == 0x00);
}

// Follows the jump at the entry of a function (E9 rel32: an incremental link's thunk, a hook's
// redirected entry; `mov eax, imm32; jmp eax`: EI_ORIGINAL's forwarders) to where it goes; 0 when the
// code at `entry` is not such a jump.
std::uintptr_t JumpTarget(std::uintptr_t entry) {
    const auto* q = reinterpret_cast<const std::uint8_t*>(entry);
    if (q[0] == 0xE9) return entry + 5 + *reinterpret_cast<const std::int32_t*>(entry + 1);
    if (q[0] == 0xB8 && q[5] == 0xFF && q[6] == 0xE0) return *reinterpret_cast<const std::uintptr_t*>(entry + 1);
    return 0;
}

// The function starts the stack walk validates against, from code with no symbols and no
// disassembly: a value is a start when it lies in a code range, follows a function's end
// (AfterFunctionEnd) and is the target of an E8 rel32 (called) or is stored as a 4-byte value in a
// data section or in the code (a virtual method, a callback: `taken`); and every place a jump at
// such a start leads. A stray byte pattern adds a start now and then (E8 in an immediate; a switch
// table's label after a RET), which can reject a real frame, never accept a stale one; a function
// nothing calls or stores (never reached through its own address) is missing, which only widens its
// predecessor's span.
void BuildStarts(State& s) {
    if (s.startsFor == s.code.size()) return;
    s.starts.clear();
    s.taken.clear();
    auto plausible = [&](std::uintptr_t target) {
        const CodeRange* in = CodeContaining(s, target);
        return in && target >= in->begin + 4 && AfterFunctionEnd(target);
    };
    for (const CodeRange& r : s.code) {
        for (std::uintptr_t a = r.begin; a + 5 <= r.end; ++a) {
            if (*reinterpret_cast<const std::uint8_t*>(a) == 0xE8) {
                std::uintptr_t target = a + 5 + *reinterpret_cast<const std::int32_t*>(a + 1);
                if (plausible(target)) s.starts.push_back(target);
            }
            std::uintptr_t value = *reinterpret_cast<const std::uintptr_t*>(a);   // immediates
            if (plausible(value)) s.taken.push_back(value);
        }
    }
    for (const auto& [begin, end] : s.data)
        for (std::uintptr_t a = (begin + 3) & ~std::uintptr_t(3); a + 4 <= end; a += 4) {
            std::uintptr_t value = *reinterpret_cast<const std::uintptr_t*>(a);
            if (plausible(value)) s.taken.push_back(value);
        }
    std::sort(s.taken.begin(), s.taken.end());
    s.taken.erase(std::unique(s.taken.begin(), s.taken.end()), s.taken.end());
    std::vector<std::uintptr_t> landed;
    for (std::uintptr_t entry : s.taken) {
        s.starts.push_back(entry);
        std::uintptr_t at = entry;
        for (int hop = 0; hop < 6; ++hop) {
            at = JumpTarget(at);
            if (!at || !CodeContaining(s, at)) break;
            landed.push_back(at);
        }
    }
    s.taken.insert(s.taken.end(), landed.begin(), landed.end());
    s.starts.insert(s.starts.end(), landed.begin(), landed.end());
    std::sort(s.taken.begin(), s.taken.end());
    s.taken.erase(std::unique(s.taken.begin(), s.taken.end()), s.taken.end());
    std::sort(s.starts.begin(), s.starts.end());
    s.starts.erase(std::unique(s.starts.begin(), s.starts.end()), s.starts.end());
    s.startsFor = s.code.size();
}

// Whether the function holding `inner` has its address stored somewhere, so that a call through a
// register or memory can lead to it.
bool AddressTaken(const State& s, std::uintptr_t inner) {
    auto next = std::upper_bound(s.starts.begin(), s.starts.end(), inner);
    if (next == s.starts.begin()) return false;
    return std::binary_search(s.taken.begin(), s.taken.end(), *(next - 1));
}

// Whether `inner` (a return address) lies in the function that begins at `entry`: the same code
// range, and no function start in (entry, inner].
bool InFunction(const State& s, std::uintptr_t entry, std::uintptr_t inner) {
    const CodeRange* r = CodeContaining(s, entry);
    if (!r || inner < entry || inner >= r->end) return false;
    auto next = std::upper_bound(s.starts.begin(), s.starts.end(), entry);
    return next == s.starts.end() || *next > inner;
}

// Whether the call before `a` is a direct one (E8 rel32): its target can be checked.
bool IsDirectCall(std::uintptr_t a) { return reinterpret_cast<const std::uint8_t*>(a)[-5] == 0xE8; }

// Whether the function at `entry` jumps (JMP rel32 as a tail call, after setting its arguments up)
// into the function holding `inner`. No decoding: any E9 byte in the first 0x100 bytes of the
// function counts, a stray one only lets a stale frame through that has such a function and a
// target that lands exactly there.
bool TailCallsInto(const State& s, std::uintptr_t entry, std::uintptr_t inner) {
    auto next = std::upper_bound(s.starts.begin(), s.starts.end(), entry);
    std::uintptr_t end = entry + 0x100;
    if (next != s.starts.end() && *next < end) end = *next;
    for (std::uintptr_t at = entry; at + 5 <= end; ++at) {
        if (*reinterpret_cast<const std::uint8_t*>(at) != 0xE9) continue;
        std::uintptr_t target = at + 5 + *reinterpret_cast<const std::int32_t*>(at + 1);
        for (int hop = 0; hop < 6 && target && CodeContaining(s, target); ++hop) {
            if (InFunction(s, target, inner)) return true;
            target = JumpTarget(target);
        }
    }
    return false;
}

// Whether the direct call before `a` leads into the function holding `inner`, a return address found
// earlier up the stack. A stale return address (left in a frame's unwritten slot) follows a call
// whose target is some other function. The entry may be a jump on the way (JumpTarget), and the
// function may end by tail-calling the one holding `inner` (TailCallsInto).
bool CallsInto(const State& s, std::uintptr_t a, std::uintptr_t inner) {
    std::uintptr_t entry = a + *reinterpret_cast<const std::int32_t*>(a - 4);
    for (int hop = 0; hop < 6; ++hop) {
        if (!CodeContaining(s, entry)) return false;
        if (InFunction(s, entry, inner) || TailCallsInto(s, entry, inner)) return true;
        entry = JumpTarget(entry);
        if (!entry) return false;
    }
    return false;
}

// The hook's return address (`frame[0]`, the real return address of the call being observed), then up
// to seven more found up the stack.
//
// x86 code here has no frame pointers or unwind tables, so the stack is scanned for values that follow
// a CALL (FollowsCall). The unwritten slots of live frames hold return addresses from earlier, deeper
// calls, which would put stale callers into the chain. A value is therefore kept only when its call
// can be tied to the frames found so far:
// - a direct call must lead into the function holding the last kept frame (CallsInto), or, when
//   frames after the last proven one are unproven, into the function holding one of them (the later
//   ones were stale) or the last proven one;
// - a call through a register or memory has no target to check. It is considered only when the
//   function it would call (the one holding the frame before) has its address stored somewhere
//   (AddressTaken), and is kept only once a later direct call proves it (or a frame above it); the
//   unproven ones left at the end are dropped.
std::string Chain(State& s, void* const* frame) {
    BuildStarts(s);
    struct Frame {
        std::uintptr_t address;
        const CodeRange* range;
    };
    std::vector<Frame> kept;       // proven, in stack order
    std::vector<Frame> unproven;   // after the last proven frame
    auto first = reinterpret_cast<std::uintptr_t>(frame[0]);
    const CodeRange* firstRange = CodeAt(s, first);
    if (firstRange) kept.push_back({first, firstRange});
    std::uintptr_t anchor = first;
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    auto* const* top = reinterpret_cast<void* const*>(tib->StackBase);
    for (void* const* p = frame + 1; p < top && p < frame + 128 && kept.size() < 8; ++p) {
        auto a = reinterpret_cast<std::uintptr_t>(*p);
        const CodeRange* r = CodeAt(s, a);
        if (!r || !FollowsCall(a)) continue;
        if (!firstRange) {   // the observed call came from somewhere unknown: nothing to prove against
            kept.push_back({a, r});
            continue;
        }
        if (!IsDirectCall(a)) {
            // The callee is the function holding the frame before: it must have its address stored
            // somewhere. An import call (CALL [address]) leads into a DLL, never into this code
            // (a callback comes back through the DLL's own frames, which this walk does not see).
            const auto* code = reinterpret_cast<const std::uint8_t*>(a);
            bool import = code[-6] == 0xFF && code[-5] == 0x15;
            if (!import && AddressTaken(s, unproven.empty() ? anchor : unproven.back().address))
                unproven.push_back({a, r});
            continue;
        }
        // The latest of the unproven frames, else the proven one, that this call leads into.
        int into = -2;   // -1: the last proven frame; i >= 0: unproven[i]
        for (int i = int(unproven.size()) - 1; i >= 0 && into == -2; --i)
            if (CallsInto(s, a, unproven[i].address)) into = i;
        if (into == -2 && CallsInto(s, a, anchor)) into = -1;
        if (into == -2) continue;
        for (int i = 0; i <= into; ++i) kept.push_back(unproven[i]);
        unproven.clear();
        kept.push_back({a, r});
        anchor = a;
    }
    if (kept.size() > 8) kept.resize(8);
    std::string out = firstRange ? std::string() : std::string("?");
    for (const Frame& f : kept) AppendFrame(out, f.address, *f.range);
    return out;
}


// --- rows -----------------------------------------------------------------------------------------

void AddRow(State& s, const char* kind, const std::string& owner, const std::string& size, const char* primitive,
            const std::string& chain, const std::string& detail) {
    Row& row = s.rows[owner + '\t' + size + '\t' + primitive + '\t' + chain];
    row.kind = kind;
    ++row.count;
    if (row.lastFrame != s.frame) {
        row.lastFrame = s.frame;
        ++row.frames;
    }
    if (detail.empty()) return;
    for (const std::string& d : row.details)
        if (d == detail) return;
    if (row.details.size() < kMaxDetails) row.details.push_back(detail);
    else row.moreDetails = true;
}

// --- DIBs and DC sessions -------------------------------------------------------------------------

Dib* TrackDib(State& s, HBITMAP bitmap, const std::string& owner) {
    DIBSECTION section{};
    if (GetObjectA(bitmap, sizeof(section), &section) != sizeof(section) || !section.dsBm.bmBits) return nullptr;
    auto it = s.dibs.find(bitmap);
    if (it != s.dibs.end() && it->second.bits != section.dsBm.bmBits) {
        s.dibs.erase(it);   // the handle was reused
        it = s.dibs.end();
    }
    if (it == s.dibs.end()) {
        Dib dib;
        dib.bits = section.dsBm.bmBits;
        dib.width = section.dsBm.bmWidth;
        dib.height = section.dsBm.bmHeight < 0 ? -section.dsBm.bmHeight : section.dsBm.bmHeight;
        dib.bytes = std::size_t(section.dsBm.bmWidthBytes) * std::size_t(dib.height);
        dib.owner = owner;
        dib.hash = SafeHash(dib.bits, dib.bytes);
        it = s.dibs.emplace(bitmap, std::move(dib)).first;
    } else if (owner != "unowned or dib") {
        it->second.owner = owner;
    }
    return &it->second;
}

std::string StatsKey(const Dib& dib) { return dib.owner + '\t' + Size(dib.width, dib.height); }

// Something hooked drew into, wrote or copied into the DIB. Counted for its surface at once: a
// surface may be swapped out (its DIB freed) before the frame ends.
void MarkDrawn(State& s, Dib& dib) {
    dib.touched = true;
    s.stats[StatsKey(dib)].pending = true;
}

bool SessionOpenOn(const State& s, HBITMAP bitmap) {
    for (const auto& [dc, session] : s.sessions)
        if (session.bitmap == bitmap) return true;
    return false;
}

// A bitmap goes into `dc`: a surface's DIB starts a session.
void BeginSession(State& s, HDC dc, HBITMAP bitmap, const std::string& chain) {
    if (GetObjectType(dc) != OBJ_MEMDC) return;
    SurfaceView view;
    std::string owner = NameByDc(s, dc, &view);
    // Not named: a surface no window or interface field keeps (tooltips, temporary copies) when its
    // dc_ was set before the select (005FDC00 does), otherwise some other DIB in a memory DC.
    if (owner.empty()) owner = "unowned or dib";
    Dib* dib = TrackDib(s, bitmap, owner);
    if (!dib) return;
    s.sessions[dc] = {bitmap, SafeHash(dib->bits, dib->bytes), 0, chain};
}

// Another bitmap replaces the session's in `dc` (005FDC60): the end of the session.
void EndSession(State& s, HDC dc, HBITMAP replacement) {
    auto it = s.sessions.find(dc);
    if (it == s.sessions.end() || it->second.bitmap == replacement) return;
    Session session = std::move(it->second);
    s.sessions.erase(it);
    auto found = s.dibs.find(session.bitmap);
    if (found == s.dibs.end()) return;
    Dib& dib = found->second;
    // Counted at the end: 005FDC00 names its DC after the select, and some windows draw a surface
    // before AddSurface gives it an owner.
    if (dib.owner == "unowned or dib") dib.owner = IsSurfaceMemoryDc(dc) ? "unowned" : "dib";
    ++s.stats[StatsKey(dib)].sessions;
    AddRow(s, "dc", dib.owner, Size(dib.width, dib.height), "GetDC(surface)", session.chain,
           session.draws ? std::string() : std::string("no drawing call"));
    GdiFlush();
    std::uint64_t hash = SafeHash(dib.bits, dib.bytes);
    dib.lastSession = session.chain;
    if (hash == session.hash) return;
    MarkDrawn(s, dib);
    if (session.draws == 0) {
        ++s.stats[StatsKey(dib)].uncaughtSession;
        AddRow(s, "uncaught", dib.owner, Size(dib.width, dib.height), "(in DC session)", session.chain, {});
    }
}

// --- placing a call -------------------------------------------------------------------------------

struct Target {
    std::string owner, size;
    Dib* dib = nullptr;
    Session* session = nullptr;
};

Target TargetOf(State& s, HDC dc) {
    Target t;
    if (!dc) {
        t.owner = "(no DC)";
        t.size = "-";
        return t;
    }
    if (auto it = s.sessions.find(dc); it != s.sessions.end()) {
        auto found = s.dibs.find(it->second.bitmap);
        if (found != s.dibs.end()) {
            // 005FDC00 records its DC after selecting the DIB: by the first drawing call it is known.
            if (found->second.owner == "unowned or dib") found->second.owner = IsSurfaceMemoryDc(dc) ? "unowned" : "dib";
            t.dib = &found->second;
            t.session = &it->second;
            t.owner = t.dib->owner;
            t.size = Size(t.dib->width, t.dib->height);
            return t;
        }
    }
    if (auto it = s.windowDcs.find(dc); it != s.windowDcs.end()) {
        t.owner = it->second;
        t.size = "-";
        return t;
    }
    DWORD type = GetObjectType(dc);
    if (type == OBJ_DC) {
        // A DirectDraw surface's DC (005FDC00 without "cpuwindows") or another device DC.
        SurfaceView view;
        std::string name = NameByDc(s, dc, &view);
        t.owner = name.empty() ? "(DC)" : name + " (DirectDraw DC)";
        t.size = name.empty() ? "-" : Size(view.width, view.height);
        return t;
    }
    t.owner = type == OBJ_MEMDC ? "(memory DC)" : "(other DC)";
    BITMAP bitmap{};
    HGDIOBJ selected = GetCurrentObject(dc, OBJ_BITMAP);
    t.size = selected && GetObjectA(selected, sizeof(bitmap), &bitmap) ? Size(bitmap.bmWidth, bitmap.bmHeight) : "-";
    return t;
}

bool Draws(const char* kind) { return !std::strcmp(kind, "draw") || !std::strcmp(kind, "text"); }

void Note(void* const* frame, HDC dc, const char* primitive, const char* kind, const std::string& detail) {
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    std::string chain = Chain(s, frame);
    Target t = TargetOf(s, dc);
    if (t.dib && Draws(kind)) {
        MarkDrawn(s, *t.dib);
        if (t.session) ++t.session->draws;
    }
    AddRow(s, kind, t.owner, t.size, primitive, chain, detail);
}

// --- details --------------------------------------------------------------------------------------

std::string Format(const char* format, ...) {
    char text[160];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

std::string FontText(HGDIOBJ font) {
    LOGFONTA f{};
    if (!font || GetObjectA(font, sizeof(f), &f) != sizeof(f)) return "font?";
    return Format("%s h%ld w%ld q%d", f.lfFaceName, f.lfHeight, f.lfWeight, f.lfQuality);
}

std::string CurrentFont(HDC dc) { return FontText(GetCurrentObject(dc, OBJ_FONT)); }

std::string ObjectText(HGDIOBJ object) {
    switch (GetObjectType(object)) {
    case OBJ_FONT:
        return FontText(object);
    case OBJ_PEN: {
        LOGPEN pen{};
        GetObjectA(object, sizeof(pen), &pen);
        return Format("pen s%u w%ld %06X", pen.lopnStyle, pen.lopnWidth.x, unsigned(pen.lopnColor));
    }
    case OBJ_EXTPEN:
        return "extpen";
    case OBJ_BRUSH: {
        LOGBRUSH brush{};
        GetObjectA(object, sizeof(brush), &brush);
        return Format("brush s%u %06X", brush.lbStyle, unsigned(brush.lbColor));
    }
    default:
        return Format("type %lu", GetObjectType(object));
    }
}

const char* ObjectKind(HGDIOBJ object) {
    switch (GetObjectType(object)) {
    case OBJ_FONT: return "SelectObject(font)";
    case OBJ_PEN:
    case OBJ_EXTPEN: return "SelectObject(pen)";
    case OBJ_BRUSH: return "SelectObject(brush)";
    case OBJ_REGION: return "SelectObject(region)";
    default: return "SelectObject(other)";
    }
}

std::string DcText(State& s, HDC dc) {
    if (!dc) return "to (no DC)";
    Target t = TargetOf(s, dc);
    return "with " + t.owner + " " + t.size;
}

// --- the hooks ------------------------------------------------------------------------------------

void* OriginalOf(Api api, const void* returnAddress) {
    State& s = S();
    auto r = reinterpret_cast<std::uintptr_t>(returnAddress);
    for (const PatchedModule& m : s.patched)
        if (r >= m.begin && r < m.end && m.original[api]) return m.original[api];
    return s.real[api];
}

template <typename F>
F Original(Api api, void* const* frame) {
    return reinterpret_cast<F>(OriginalOf(api, frame[0]));
}

#define EI_FRAME void* const* frame = static_cast<void* const*>(_AddressOfReturnAddress())
#define EI_CALL(api, fn) Original<decltype(&::fn)>(api, frame)

int WINAPI HookDrawTextA(HDC dc, LPCSTR text, int length, LPRECT rect, UINT format) {
    EI_FRAME;
    bool measure = (format & DT_CALCRECT) != 0;
    Note(frame, dc, measure ? "DrawTextA(CALCRECT)" : "DrawTextA", measure ? "metric" : "text",
         Format("fmt %X, %s", format, CurrentFont(dc).c_str()));
    return EI_CALL(kDrawTextA, DrawTextA)(dc, text, length, rect, format);
}

int WINAPI HookDrawTextExA(HDC dc, LPSTR text, int length, LPRECT rect, UINT format, LPDRAWTEXTPARAMS params) {
    EI_FRAME;
    bool measure = (format & DT_CALCRECT) != 0;
    Note(frame, dc, measure ? "DrawTextExA(CALCRECT)" : "DrawTextExA", measure ? "metric" : "text",
         Format("fmt %X, %s", format, CurrentFont(dc).c_str()));
    return EI_CALL(kDrawTextExA, DrawTextExA)(dc, text, length, rect, format, params);
}

LONG WINAPI HookTabbedTextOutA(HDC dc, int x, int y, LPCSTR text, int count, int tabs, const INT* positions, int origin) {
    EI_FRAME;
    Note(frame, dc, "TabbedTextOutA", "text", CurrentFont(dc));
    return EI_CALL(kTabbedTextOutA, TabbedTextOutA)(dc, x, y, text, count, tabs, positions, origin);
}

DWORD WINAPI HookGetTabbedTextExtentA(HDC dc, LPCSTR text, int count, int tabs, const INT* positions) {
    EI_FRAME;
    Note(frame, dc, "GetTabbedTextExtentA", "metric", CurrentFont(dc));
    return EI_CALL(kGetTabbedTextExtentA, GetTabbedTextExtentA)(dc, text, count, tabs, positions);
}

int WINAPI HookFillRect(HDC dc, const RECT* rect, HBRUSH brush) {
    EI_FRAME;
    std::string detail = reinterpret_cast<std::uintptr_t>(brush) <= 31 ? Format("system colour %u", unsigned(reinterpret_cast<std::uintptr_t>(brush)) - 1)
                                                                        : ObjectText(brush);
    Note(frame, dc, "FillRect", "draw", detail);
    return EI_CALL(kFillRect, FillRect)(dc, rect, brush);
}

int WINAPI HookFrameRect(HDC dc, const RECT* rect, HBRUSH brush) {
    EI_FRAME;
    Note(frame, dc, "FrameRect", "draw", ObjectText(brush));
    return EI_CALL(kFrameRect, FrameRect)(dc, rect, brush);
}

BOOL WINAPI HookInvertRect(HDC dc, const RECT* rect) {
    EI_FRAME;
    Note(frame, dc, "InvertRect", "draw", {});
    return EI_CALL(kInvertRect, InvertRect)(dc, rect);
}

BOOL WINAPI HookDrawFocusRect(HDC dc, const RECT* rect) {
    EI_FRAME;
    Note(frame, dc, "DrawFocusRect", "draw", {});
    return EI_CALL(kDrawFocusRect, DrawFocusRect)(dc, rect);
}

BOOL WINAPI HookDrawEdge(HDC dc, LPRECT rect, UINT edge, UINT flags) {
    EI_FRAME;
    Note(frame, dc, "DrawEdge", "draw", Format("edge %X flags %X", edge, flags));
    return EI_CALL(kDrawEdge, DrawEdge)(dc, rect, edge, flags);
}

BOOL WINAPI HookDrawIconEx(HDC dc, int x, int y, HICON icon, int w, int h, UINT step, HBRUSH brush, UINT flags) {
    EI_FRAME;
    Note(frame, dc, "DrawIconEx", "draw", {});
    return EI_CALL(kDrawIconEx, DrawIconEx)(dc, x, y, icon, w, h, step, brush, flags);
}

std::string WindowDcName(HWND window, const char* what) {
    if (!window) return std::string("(") + what + " screen)";
    char name[64] = "?";
    GetClassNameA(window, name, sizeof(name));
    return std::string("(") + what + " " + name + ")";
}

HDC WINAPI HookGetDC(HWND window) {
    EI_FRAME;
    HDC dc = EI_CALL(kGetDC, GetDC)(window);
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    if (dc) s.windowDcs[dc] = WindowDcName(window, "GetDC");
    AddRow(s, "dc", WindowDcName(window, "GetDC"), "-", "GetDC(window)", Chain(s, frame), {});
    return dc;
}

HDC WINAPI HookGetWindowDC(HWND window) {
    EI_FRAME;
    HDC dc = EI_CALL(kGetWindowDC, GetWindowDC)(window);
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    if (dc) s.windowDcs[dc] = WindowDcName(window, "GetWindowDC");
    AddRow(s, "dc", WindowDcName(window, "GetWindowDC"), "-", "GetWindowDC", Chain(s, frame), {});
    return dc;
}

int WINAPI HookReleaseDC(HWND window, HDC dc) {
    EI_FRAME;
    {
        State& s = S();
        std::lock_guard<std::mutex> hold(s.lock);
        s.windowDcs.erase(dc);
    }
    return EI_CALL(kReleaseDC, ReleaseDC)(window, dc);
}

LRESULT WINAPI HookSendMessageA(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    EI_FRAME;
    if (message == EM_FORMATRANGE && lParam) {
        const auto* range = reinterpret_cast<const FORMATRANGE*>(lParam);
        Note(frame, range->hdc, wParam ? "EM_FORMATRANGE" : "EM_FORMATRANGE(measure)", wParam ? "text" : "metric",
             range->hdcTarget == range->hdc ? "target the same DC" : "target another DC");
    } else if (message == EM_DISPLAYBAND) {
        Note(frame, nullptr, "EM_DISPLAYBAND", "text", {});
    }
    return EI_CALL(kSendMessageA, SendMessageA)(window, message, wParam, lParam);
}

BOOL WINAPI HookExtTextOutA(HDC dc, int x, int y, UINT options, const RECT* rect, LPCSTR text, UINT count, const INT* dx) {
    EI_FRAME;
    // No text with ETO_OPAQUE: a fill in the background colour (MFC's CDC::FillSolidRect 0070BADA /
    // 0070BB04 is SetBkColor plus this).
    if (count == 0 && (options & ETO_OPAQUE))
        Note(frame, dc, "ExtTextOutA(opaque fill)", "draw", Format("%06X", unsigned(GetBkColor(dc))));
    else
        Note(frame, dc, "ExtTextOutA", "text", Format("opt %X, %s", options, CurrentFont(dc).c_str()));
    return EI_CALL(kExtTextOutA, ExtTextOutA)(dc, x, y, options, rect, text, count, dx);
}

BOOL WINAPI HookTextOutA(HDC dc, int x, int y, LPCSTR text, int count) {
    EI_FRAME;
    Note(frame, dc, "TextOutA", "text", CurrentFont(dc));
    return EI_CALL(kTextOutA, TextOutA)(dc, x, y, text, count);
}

BOOL WINAPI HookGetTextExtentPoint32A(HDC dc, LPCSTR text, int count, LPSIZE size) {
    EI_FRAME;
    Note(frame, dc, "GetTextExtentPoint32A", "metric", CurrentFont(dc));
    return EI_CALL(kGetTextExtentPoint32A, GetTextExtentPoint32A)(dc, text, count, size);
}

BOOL WINAPI HookGetTextExtentPointA(HDC dc, LPCSTR text, int count, LPSIZE size) {
    EI_FRAME;
    Note(frame, dc, "GetTextExtentPointA", "metric", CurrentFont(dc));
    return EI_CALL(kGetTextExtentPointA, GetTextExtentPointA)(dc, text, count, size);
}

BOOL WINAPI HookGetTextExtentExPointA(HDC dc, LPCSTR text, int count, int maxExtent, LPINT fit, LPINT dx, LPSIZE size) {
    EI_FRAME;
    Note(frame, dc, "GetTextExtentExPointA", "metric", CurrentFont(dc));
    return EI_CALL(kGetTextExtentExPointA, GetTextExtentExPointA)(dc, text, count, maxExtent, fit, dx, size);
}

BOOL WINAPI HookGetTextMetricsA(HDC dc, LPTEXTMETRICA metrics) {
    EI_FRAME;
    Note(frame, dc, "GetTextMetricsA", "metric", CurrentFont(dc));
    return EI_CALL(kGetTextMetricsA, GetTextMetricsA)(dc, metrics);
}

BOOL WINAPI HookGetCharWidthA(HDC dc, UINT first, UINT last, LPINT widths) {
    EI_FRAME;
    Note(frame, dc, "GetCharWidthA", "metric", CurrentFont(dc));
    return EI_CALL(kGetCharWidthA, GetCharWidthA)(dc, first, last, widths);
}

BOOL WINAPI HookGetCharWidth32A(HDC dc, UINT first, UINT last, LPINT widths) {
    EI_FRAME;
    Note(frame, dc, "GetCharWidth32A", "metric", CurrentFont(dc));
    return EI_CALL(kGetCharWidth32A, GetCharWidth32A)(dc, first, last, widths);
}

BOOL WINAPI HookGetCharABCWidthsA(HDC dc, UINT first, UINT last, LPABC widths) {
    EI_FRAME;
    Note(frame, dc, "GetCharABCWidthsA", "metric", CurrentFont(dc));
    return EI_CALL(kGetCharABCWidthsA, GetCharABCWidthsA)(dc, first, last, widths);
}

int WINAPI HookGetTextFaceA(HDC dc, int count, LPSTR name) {
    EI_FRAME;
    Note(frame, dc, "GetTextFaceA", "metric", CurrentFont(dc));
    return EI_CALL(kGetTextFaceA, GetTextFaceA)(dc, count, name);
}

int WINAPI HookGetDeviceCaps(HDC dc, int index) {
    EI_FRAME;
    Note(frame, dc, "GetDeviceCaps", "metric", Format("index %d", index));
    return EI_CALL(kGetDeviceCaps, GetDeviceCaps)(dc, index);
}

BOOL WINAPI HookMoveToEx(HDC dc, int x, int y, LPPOINT previous) {
    EI_FRAME;
    Note(frame, dc, "MoveToEx", "state", {});
    return EI_CALL(kMoveToEx, MoveToEx)(dc, x, y, previous);
}

BOOL WINAPI HookLineTo(HDC dc, int x, int y) {
    EI_FRAME;
    Note(frame, dc, "LineTo", "draw", ObjectText(GetCurrentObject(dc, OBJ_PEN)));
    return EI_CALL(kLineTo, LineTo)(dc, x, y);
}

BOOL WINAPI HookPolyline(HDC dc, const POINT* points, int count) {
    EI_FRAME;
    Note(frame, dc, "Polyline", "draw", ObjectText(GetCurrentObject(dc, OBJ_PEN)));
    return EI_CALL(kPolyline, Polyline)(dc, points, count);
}

BOOL WINAPI HookRectangle(HDC dc, int left, int top, int right, int bottom) {
    EI_FRAME;
    Note(frame, dc, "Rectangle", "draw", ObjectText(GetCurrentObject(dc, OBJ_BRUSH)));
    return EI_CALL(kRectangle, Rectangle)(dc, left, top, right, bottom);
}

BOOL WINAPI HookEllipse(HDC dc, int left, int top, int right, int bottom) {
    EI_FRAME;
    Note(frame, dc, "Ellipse", "draw", {});
    return EI_CALL(kEllipse, Ellipse)(dc, left, top, right, bottom);
}

COLORREF WINAPI HookSetPixel(HDC dc, int x, int y, COLORREF colour) {
    EI_FRAME;
    Note(frame, dc, "SetPixel", "draw", Format("%06X", unsigned(colour)));
    return EI_CALL(kSetPixel, SetPixel)(dc, x, y, colour);
}

BOOL WINAPI HookSetPixelV(HDC dc, int x, int y, COLORREF colour) {
    EI_FRAME;
    Note(frame, dc, "SetPixelV", "draw", Format("%06X", unsigned(colour)));
    return EI_CALL(kSetPixelV, SetPixelV)(dc, x, y, colour);
}

COLORREF WINAPI HookGetPixel(HDC dc, int x, int y) {
    EI_FRAME;
    Note(frame, dc, "GetPixel", "read", {});
    return EI_CALL(kGetPixel, GetPixel)(dc, x, y);
}

// A blit: drawing into `to`, and a read of `from` when it is a DC.
void NoteBlit(void* const* frame, HDC to, HDC from, const char* primitive, const char* source, DWORD rop) {
    std::string toText, fromText;
    {
        State& s = S();
        std::lock_guard<std::mutex> hold(s.lock);
        toText = DcText(s, to);
        if (from) fromText = DcText(s, from);
    }
    Note(frame, to, primitive, "draw", Format("rop %lX ", rop) + (from ? fromText : std::string()));
    if (from) Note(frame, from, source, "read", toText);
}

BOOL WINAPI HookBitBlt(HDC to, int x, int y, int w, int h, HDC from, int fx, int fy, DWORD rop) {
    EI_FRAME;
    NoteBlit(frame, to, from, "BitBlt", "BitBlt(source)", rop);
    return EI_CALL(kBitBlt, BitBlt)(to, x, y, w, h, from, fx, fy, rop);
}

BOOL WINAPI HookStretchBlt(HDC to, int x, int y, int w, int h, HDC from, int fx, int fy, int fw, int fh, DWORD rop) {
    EI_FRAME;
    NoteBlit(frame, to, from, "StretchBlt", "StretchBlt(source)", rop);
    return EI_CALL(kStretchBlt, StretchBlt)(to, x, y, w, h, from, fx, fy, fw, fh, rop);
}

BOOL WINAPI HookPatBlt(HDC dc, int x, int y, int w, int h, DWORD rop) {
    EI_FRAME;
    Note(frame, dc, "PatBlt", "draw", Format("rop %lX, %s", rop, ObjectText(GetCurrentObject(dc, OBJ_BRUSH)).c_str()));
    return EI_CALL(kPatBlt, PatBlt)(dc, x, y, w, h, rop);
}

int WINAPI HookStretchDIBits(HDC dc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void* bits,
                             const BITMAPINFO* info, UINT usage, DWORD rop) {
    EI_FRAME;
    Note(frame, dc, "StretchDIBits", "draw", Format("rop %lX", rop));
    return EI_CALL(kStretchDIBits, StretchDIBits)(dc, x, y, w, h, sx, sy, sw, sh, bits, info, usage, rop);
}

int WINAPI HookSetDIBitsToDevice(HDC dc, int x, int y, DWORD w, DWORD h, int sx, int sy, UINT start, UINT lines,
                                 const void* bits, const BITMAPINFO* info, UINT usage) {
    EI_FRAME;
    Note(frame, dc, "SetDIBitsToDevice", "draw", {});
    return EI_CALL(kSetDIBitsToDevice, SetDIBitsToDevice)(dc, x, y, w, h, sx, sy, start, lines, bits, info, usage);
}

HGDIOBJ WINAPI HookSelectObject(HDC dc, HGDIOBJ object) {
    EI_FRAME;
    DWORD type = GetObjectType(object);
    if (type != OBJ_BITMAP) {
        if (type == OBJ_FONT || type == OBJ_PEN || type == OBJ_EXTPEN || type == OBJ_BRUSH)
            Note(frame, dc, ObjectKind(object), "state", ObjectText(object));
        return EI_CALL(kSelectObject, SelectObject)(dc, object);
    }
    State& s = S();
    std::string chain;
    {
        std::lock_guard<std::mutex> hold(s.lock);
        chain = Chain(s, frame);
        EndSession(s, dc, static_cast<HBITMAP>(object));
    }
    HGDIOBJ previous = EI_CALL(kSelectObject, SelectObject)(dc, object);
    if (previous) {
        std::lock_guard<std::mutex> hold(s.lock);
        if (!s.sessions.count(dc)) BeginSession(s, dc, static_cast<HBITMAP>(object), chain);
    }
    return previous;
}

HFONT WINAPI HookCreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                             DWORD underline, DWORD strikeOut, DWORD charSet, DWORD precision, DWORD clip,
                             DWORD quality, DWORD pitch, LPCSTR face) {
    EI_FRAME;
    Note(frame, nullptr, "CreateFontA", "font", Format("%s h%d w%d q%lu", face ? face : "", height, weight, quality));
    return EI_CALL(kCreateFontA, CreateFontA)(height, width, escapement, orientation, weight, italic, underline,
                                              strikeOut, charSet, precision, clip, quality, pitch, face);
}

HFONT WINAPI HookCreateFontIndirectA(const LOGFONTA* font) {
    EI_FRAME;
    Note(frame, nullptr, "CreateFontIndirectA", "font",
         font ? Format("%s h%ld w%ld q%d", font->lfFaceName, font->lfHeight, font->lfWeight, font->lfQuality) : "");
    return EI_CALL(kCreateFontIndirectA, CreateFontIndirectA)(font);
}

HPEN WINAPI HookCreatePen(int style, int width, COLORREF colour) {
    EI_FRAME;
    Note(frame, nullptr, "CreatePen", "font", Format("s%d w%d %06X", style, width, unsigned(colour)));
    return EI_CALL(kCreatePen, CreatePen)(style, width, colour);
}

HBRUSH WINAPI HookCreateSolidBrush(COLORREF colour) {
    EI_FRAME;
    Note(frame, nullptr, "CreateSolidBrush", "font", Format("%06X", unsigned(colour)));
    return EI_CALL(kCreateSolidBrush, CreateSolidBrush)(colour);
}

COLORREF WINAPI HookSetTextColor(HDC dc, COLORREF colour) {
    EI_FRAME;
    Note(frame, dc, "SetTextColor", "state", Format("%06X", unsigned(colour)));
    return EI_CALL(kSetTextColor, SetTextColor)(dc, colour);
}

COLORREF WINAPI HookSetBkColor(HDC dc, COLORREF colour) {
    EI_FRAME;
    Note(frame, dc, "SetBkColor", "state", Format("%06X", unsigned(colour)));
    return EI_CALL(kSetBkColor, SetBkColor)(dc, colour);
}

int WINAPI HookSetBkMode(HDC dc, int mode) {
    EI_FRAME;
    Note(frame, dc, "SetBkMode", "state", Format("%d", mode));
    return EI_CALL(kSetBkMode, SetBkMode)(dc, mode);
}

UINT WINAPI HookSetTextAlign(HDC dc, UINT align) {
    EI_FRAME;
    Note(frame, dc, "SetTextAlign", "state", Format("%X", align));
    return EI_CALL(kSetTextAlign, SetTextAlign)(dc, align);
}

#undef EI_CALL
#undef EI_FRAME

struct ApiHook {
    const char* dll;
    const char* name;
    void* hook;
};

const ApiHook kApis[kApiCount] = {
    {"user32.dll", "DrawTextA", reinterpret_cast<void*>(&HookDrawTextA)},
    {"user32.dll", "DrawTextExA", reinterpret_cast<void*>(&HookDrawTextExA)},
    {"user32.dll", "TabbedTextOutA", reinterpret_cast<void*>(&HookTabbedTextOutA)},
    {"user32.dll", "GetTabbedTextExtentA", reinterpret_cast<void*>(&HookGetTabbedTextExtentA)},
    {"user32.dll", "FillRect", reinterpret_cast<void*>(&HookFillRect)},
    {"user32.dll", "FrameRect", reinterpret_cast<void*>(&HookFrameRect)},
    {"user32.dll", "InvertRect", reinterpret_cast<void*>(&HookInvertRect)},
    {"user32.dll", "DrawFocusRect", reinterpret_cast<void*>(&HookDrawFocusRect)},
    {"user32.dll", "DrawEdge", reinterpret_cast<void*>(&HookDrawEdge)},
    {"user32.dll", "DrawIconEx", reinterpret_cast<void*>(&HookDrawIconEx)},
    {"user32.dll", "GetDC", reinterpret_cast<void*>(&HookGetDC)},
    {"user32.dll", "GetWindowDC", reinterpret_cast<void*>(&HookGetWindowDC)},
    {"user32.dll", "ReleaseDC", reinterpret_cast<void*>(&HookReleaseDC)},
    {"user32.dll", "SendMessageA", reinterpret_cast<void*>(&HookSendMessageA)},
    {"gdi32.dll", "ExtTextOutA", reinterpret_cast<void*>(&HookExtTextOutA)},
    {"gdi32.dll", "TextOutA", reinterpret_cast<void*>(&HookTextOutA)},
    {"gdi32.dll", "GetTextExtentPoint32A", reinterpret_cast<void*>(&HookGetTextExtentPoint32A)},
    {"gdi32.dll", "GetTextExtentPointA", reinterpret_cast<void*>(&HookGetTextExtentPointA)},
    {"gdi32.dll", "GetTextExtentExPointA", reinterpret_cast<void*>(&HookGetTextExtentExPointA)},
    {"gdi32.dll", "GetTextMetricsA", reinterpret_cast<void*>(&HookGetTextMetricsA)},
    {"gdi32.dll", "GetCharWidthA", reinterpret_cast<void*>(&HookGetCharWidthA)},
    {"gdi32.dll", "GetCharWidth32A", reinterpret_cast<void*>(&HookGetCharWidth32A)},
    {"gdi32.dll", "GetCharABCWidthsA", reinterpret_cast<void*>(&HookGetCharABCWidthsA)},
    {"gdi32.dll", "GetTextFaceA", reinterpret_cast<void*>(&HookGetTextFaceA)},
    {"gdi32.dll", "GetDeviceCaps", reinterpret_cast<void*>(&HookGetDeviceCaps)},
    {"gdi32.dll", "MoveToEx", reinterpret_cast<void*>(&HookMoveToEx)},
    {"gdi32.dll", "LineTo", reinterpret_cast<void*>(&HookLineTo)},
    {"gdi32.dll", "Polyline", reinterpret_cast<void*>(&HookPolyline)},
    {"gdi32.dll", "Rectangle", reinterpret_cast<void*>(&HookRectangle)},
    {"gdi32.dll", "Ellipse", reinterpret_cast<void*>(&HookEllipse)},
    {"gdi32.dll", "SetPixel", reinterpret_cast<void*>(&HookSetPixel)},
    {"gdi32.dll", "SetPixelV", reinterpret_cast<void*>(&HookSetPixelV)},
    {"gdi32.dll", "GetPixel", reinterpret_cast<void*>(&HookGetPixel)},
    {"gdi32.dll", "BitBlt", reinterpret_cast<void*>(&HookBitBlt)},
    {"gdi32.dll", "StretchBlt", reinterpret_cast<void*>(&HookStretchBlt)},
    {"gdi32.dll", "PatBlt", reinterpret_cast<void*>(&HookPatBlt)},
    {"gdi32.dll", "StretchDIBits", reinterpret_cast<void*>(&HookStretchDIBits)},
    {"gdi32.dll", "SetDIBitsToDevice", reinterpret_cast<void*>(&HookSetDIBitsToDevice)},
    {"gdi32.dll", "SelectObject", reinterpret_cast<void*>(&HookSelectObject)},
    {"gdi32.dll", "CreateFontA", reinterpret_cast<void*>(&HookCreateFontA)},
    {"gdi32.dll", "CreateFontIndirectA", reinterpret_cast<void*>(&HookCreateFontIndirectA)},
    {"gdi32.dll", "CreatePen", reinterpret_cast<void*>(&HookCreatePen)},
    {"gdi32.dll", "CreateSolidBrush", reinterpret_cast<void*>(&HookCreateSolidBrush)},
    {"gdi32.dll", "SetTextColor", reinterpret_cast<void*>(&HookSetTextColor)},
    {"gdi32.dll", "SetBkColor", reinterpret_cast<void*>(&HookSetBkColor)},
    {"gdi32.dll", "SetBkMode", reinterpret_cast<void*>(&HookSetBkMode)},
    {"gdi32.dll", "SetTextAlign", reinterpret_cast<void*>(&HookSetTextAlign)},
};

// --- DirectDraw surfaces --------------------------------------------------------------------------

const PatchedVtable* VtableOf(const void* surface) {
    void** vtable = *static_cast<void** const*>(surface);
    for (const PatchedVtable& v : S().vtables)
        if (v.vtable == vtable) return &v;
    return nullptr;
}

void RefreshSurfaces(State& s);

// The DIB of a known surface's DirectDraw surface (null when unknown). A surface swapped out frees
// its DirectDraw surface and the next one made may get the same pointer: the map is rebuilt before
// a hit is believed.
Dib* DibOfSurface(State& s, const void* surface) {
    if (!s.ddSurfaces.count(surface)) return nullptr;
    RefreshSurfaces(s);
    auto it = s.ddSurfaces.find(surface);
    if (it == s.ddSurfaces.end()) return nullptr;
    auto found = s.dibs.find(it->second);
    return found == s.dibs.end() ? nullptr : &found->second;
}

void NoteSurfaceCopy(void* const* frame, const void* to, const void* from, const char* what) {
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    Dib* target = DibOfSurface(s, to);
    Dib* source = from ? DibOfSurface(s, from) : nullptr;
    if (!target && !source) return;
    std::string chain = Chain(s, frame);
    std::string targetName = target ? target->owner + " " + Size(target->width, target->height) : "another surface";
    std::string sourceName = source ? source->owner + " " + Size(source->width, source->height)
                                    : (from ? "another surface" : "a fill");
    if (target) {
        MarkDrawn(s, *target);
        AddRow(s, "write", target->owner, Size(target->width, target->height), (std::string(what) + "(target)").c_str(),
               chain, "from " + sourceName);
    }
    if (source)
        AddRow(s, "read", source->owner, Size(source->width, source->height), (std::string(what) + "(source)").c_str(),
               chain, "to " + targetName);
}

HRESULT WINAPI HookBlt(IDirectDrawSurface7* self, RECT* to, IDirectDrawSurface7* from, RECT* fromRect, DWORD flags,
                       DDBLTFX* fx) {
    void* const* frame = static_cast<void* const*>(_AddressOfReturnAddress());
    NoteSurfaceCopy(frame, self, from, "Blt");
    return VtableOf(self)->blt(self, to, from, fromRect, flags, fx);
}

HRESULT WINAPI HookBltFast(IDirectDrawSurface7* self, DWORD x, DWORD y, IDirectDrawSurface7* from, RECT* fromRect,
                           DWORD flags) {
    void* const* frame = static_cast<void* const*>(_AddressOfReturnAddress());
    NoteSurfaceCopy(frame, self, from, "BltFast");
    return VtableOf(self)->bltFast(self, x, y, from, fromRect, flags);
}

HRESULT WINAPI HookLock(IDirectDrawSurface7* self, RECT* rect, DDSURFACEDESC2* desc, DWORD flags, HANDLE event) {
    void* const* frame = static_cast<void* const*>(_AddressOfReturnAddress());
    HRESULT hr = VtableOf(self)->lock(self, rect, desc, flags, event);
    if (hr == 0) {
        State& s = S();
        std::lock_guard<std::mutex> hold(s.lock);
        if (Dib* dib = DibOfSurface(s, self)) s.locks[self] = {SafeHash(dib->bits, dib->bytes), Chain(s, frame)};
    }
    return hr;
}

HRESULT WINAPI HookUnlock(IDirectDrawSurface7* self, RECT* rect) {
    HRESULT hr = VtableOf(self)->unlock(self, rect);
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    auto it = s.locks.find(self);
    if (it == s.locks.end()) return hr;
    PendingLock pending = std::move(it->second);
    s.locks.erase(it);
    if (Dib* dib = DibOfSurface(s, self)) {
        bool wrote = SafeHash(dib->bits, dib->bytes) != pending.hash;
        if (wrote) MarkDrawn(s, *dib);
        AddRow(s, wrote ? "write" : "read", dib->owner, Size(dib->width, dib->height), wrote ? "Lock(wrote)" : "Lock(read)",
               pending.chain, {});
    }
    return hr;
}

void PatchVtable(State& s, void** vtable) {
    for (const PatchedVtable& v : s.vtables)
        if (v.vtable == vtable) return;
    if (s.vtables.size() == s.vtables.capacity()) return;   // never reallocated: hooks read it unlocked
    PatchedVtable patched{vtable, reinterpret_cast<BltFn>(vtable[kBltSlot]), reinterpret_cast<BltFastFn>(vtable[kBltFastSlot]),
                          reinterpret_cast<LockFn>(vtable[kLockSlot]), reinterpret_cast<UnlockFn>(vtable[kUnlockSlot])};
    s.vtables.push_back(patched);
    DWORD old;
    VirtualProtect(vtable, sizeof(void*) * (kUnlockSlot + 1), PAGE_READWRITE, &old);
    vtable[kBltSlot] = reinterpret_cast<void*>(&HookBlt);
    vtable[kBltFastSlot] = reinterpret_cast<void*>(&HookBltFast);
    vtable[kLockSlot] = reinterpret_cast<void*>(&HookLock);
    vtable[kUnlockSlot] = reinterpret_cast<void*>(&HookUnlock);
    VirtualProtect(vtable, sizeof(void*) * (kUnlockSlot + 1), old, &old);
    ei::Log("census: DirectDraw surface vtable %p hooked (Blt, BltFast, Lock, Unlock)", static_cast<void*>(vtable));
}

// --- import slots ---------------------------------------------------------------------------------

int ApiByName(const char* name) {
    for (int i = 0; i < kApiCount; ++i)
        if (!std::strcmp(kApis[i].name, name)) return i;
    return -1;
}

int ApiByAddress(const State& s, std::uintptr_t address) {
    for (int i = 0; i < kApiCount; ++i)
        if (reinterpret_cast<std::uintptr_t>(s.real[i]) == address) return i;
    return -1;
}

int PatchModule(State& s, HMODULE module) {
    auto base = reinterpret_cast<std::uintptr_t>(module);
    if (!s.patchedBases.insert(base).second || s.patched.size() == s.patched.capacity()) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return 0;
    PatchedModule patched{base, base + nt->OptionalHeader.SizeOfImage, {}};
    int count = 0;
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); d->Name; ++d) {
        const char* dll = reinterpret_cast<const char*>(base + d->Name);
        if (_stricmp(dll, "user32.dll") != 0 && _stricmp(dll, "gdi32.dll") != 0) continue;
        auto* slots = reinterpret_cast<std::uintptr_t*>(base + d->FirstThunk);
        const auto* names = d->OriginalFirstThunk ? reinterpret_cast<const IMAGE_THUNK_DATA32*>(base + d->OriginalFirstThunk)
                                                  : nullptr;
        for (int i = 0; slots[i]; ++i) {
            int api = -1;
            if (names && !(names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG32))
                api = ApiByName(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData)->Name);
            else
                api = ApiByAddress(s, slots[i]);
            if (api < 0) continue;
            patched.original[api] = reinterpret_cast<void*>(slots[i]);
            DWORD old;
            VirtualProtect(&slots[i], sizeof(slots[i]), PAGE_READWRITE, &old);
            slots[i] = reinterpret_cast<std::uintptr_t>(kApis[api].hook);
            VirtualProtect(&slots[i], sizeof(slots[i]), old, &old);
            ++count;
        }
    }
    s.patched.push_back(patched);
    return count;
}

// The modules beside game.exe that may draw (addon.dll and other mods), not the graphics, sound and
// video wrappers.
void HookModules(State& s) {
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    if (char* slash = std::strrchr(exeDir, '\\')) *slash = '\0';
    HMODULE modules[512];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) return;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&HookModules), &self);
    static const char* const kSkipped[] = {"ddraw", "d3d", "dinput", "dsound", "dxgi", "binkw32", "mss32", "smackw32",
                                           "ei_hook"};
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
        if (modules[i] == GetModuleHandleA(nullptr) || modules[i] == self) continue;
        char path[MAX_PATH];
        if (!GetModuleFileNameA(modules[i], path, MAX_PATH)) continue;
        const char* name = std::strrchr(path, '\\');
        name = name ? name + 1 : path;
        bool addon = _stricmp(name, "addon.dll") == 0;
        bool beside = std::strlen(path) > std::strlen(exeDir) && _strnicmp(path, exeDir, std::strlen(exeDir)) == 0;
        if (!addon && !beside) continue;
        bool skip = false;
        for (const char* prefix : kSkipped)
            if (_strnicmp(name, prefix, std::strlen(prefix)) == 0) skip = true;
        if (skip || s.patchedBases.count(reinterpret_cast<std::uintptr_t>(modules[i]))) continue;
        AddCodeRanges(s, modules[i], addon ? 'A' : 'O', name);
        int count = PatchModule(s, modules[i]);
        ei::Log("census: %s: %d gdi32 / user32 import slots hooked", name, count);
    }
}

// --- owners ---------------------------------------------------------------------------------------

void OnOwner(const CUiWindow* window, const CUiSurface* surface, bool added) {
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    if (added) s.owners[surface] = window;
    else s.owners.erase(surface);
}

// Every named surface's DIB tracked under its name, its DirectDraw surface's vtable hooked.
void RefreshSurfaces(State& s) {
    s.ddSurfaces.clear();
    for (const Candidate& c : Candidates(s)) {
        SurfaceView view;
        if (!ReadSurface(c.surface, &view) || !view.holder) continue;
        HBITMAP bitmap = ClientBitmap(view.holder);
        void* dd = nullptr;
        SafeRead(&view.holder->surface, &dd, sizeof(dd));
        if (!bitmap) continue;
        Dib* dib = TrackDib(s, bitmap, c.Label());
        if (!dib || !dd) continue;
        s.ddSurfaces[dd] = bitmap;
        void** vtable = nullptr;
        if (SafeRead(dd, &vtable, sizeof(vtable)) && vtable) PatchVtable(s, vtable);
    }
}

// A whole-surface pixel helper ran (g_uiSurfacePixelObserver, ui_surface_pixels.cpp): CaptureScreen,
// Rgb565Copy, Greyscale, PixelAt or SwapContents. They go through a Lock, a Blt or the new renderer's
// memory, often on a surface made in the same frame or one no window keeps (the global map's pick
// picture, Rgb565Copy's temporary copy), which the DirectDraw hooks cannot place; and our callers
// call them directly, past their verify wrappers. Counted as a "read" or "write" row of the surface.
void OnPixels(const CUiSurface* surface, const char* what, bool writes, void* const* frame) {
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    if (!s.installed) return;
    std::string owner = "unowned";
    for (const Candidate& c : Candidates(s))
        if (c.surface == surface) {
            owner = c.Label();
            break;
        }
    SurfaceView view;
    bool known = ReadSurface(surface, &view);
    std::string size = known ? Size(view.width, view.height) : "?";
    if (writes && known && view.holder) {
        auto it = s.dibs.find(ClientBitmap(view.holder));
        if (it != s.dibs.end()) MarkDrawn(s, it->second);
    }
    AddRow(s, writes ? "write" : "read", owner, size, what, Chain(s, frame), {});
}

std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

bool g_enabled = false;

} // namespace

void Enable() { g_enabled = true; }

bool Enabled() {
    static int fromEnvironment = -1;
    if (fromEnvironment < 0) {
        char value[8];
        DWORD n = GetEnvironmentVariableA("EI_UI_CENSUS", value, sizeof(value));
        fromEnvironment = n && n < sizeof(value) && value[0] == '1';
    }
    return g_enabled || fromEnvironment == 1;
}

void Install() {
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    if (s.installed) return;
    s.installed = true;
    s.patched.reserve(32);
    s.vtables.reserve(16);
    for (int i = 0; i < kApiCount; ++i)
        if (HMODULE dll = GetModuleHandleA(kApis[i].dll)) s.real[i] = reinterpret_cast<void*>(GetProcAddress(dll, kApis[i].name));
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&Install), &self);
    AddCodeRanges(s, GetModuleHandleA(nullptr), 'G', "game.exe");
    if (self) AddCodeRanges(s, self, 'E', "ei_hook.dll");
    int count = PatchModule(s, GetModuleHandleA(nullptr));
    g_uiSurfaceOwnerObserver = &OnOwner;
    g_uiSurfacePixelObserver = &OnPixels;
    ei::Log("census: interface drawing census on (ui_census.csv); game.exe: %d gdi32 / user32 import slots hooked", count);
}

void Frame() {
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    if (!s.installed) return;
    ++s.frame;
    if (!s.modulesHooked || s.frame % 600 == 0) {
        if (!s.modulesHooked && !render::CpuWindows())
            ei::Log("census: the window surfaces are not in memory DCs (EI_RENDER without new,cpuwindows): "
                    "no checksums, calls placed by the surface's DirectDraw DC only");
        s.modulesHooked = true;
        HookModules(s);
    }
    RefreshSurfaces(s);
    for (auto it = s.sessions.begin(); it != s.sessions.end();) {
        if (GetObjectType(it->first) == 0) it = s.sessions.erase(it);   // the DC was deleted with the DIB in it
        else ++it;
    }
    for (auto it = s.dibs.begin(); it != s.dibs.end();) {
        DIBSECTION section{};
        if (GetObjectA(it->first, sizeof(section), &section) != sizeof(section) || section.dsBm.bmBits != it->second.bits) {
            it = s.dibs.erase(it);
            continue;
        }
        Dib& dib = it->second;
        SurfaceStats& stats = s.stats[StatsKey(dib)];
        ++stats.alive;
        std::uint64_t hash = SafeHash(dib.bits, dib.bytes);
        bool changed = hash != dib.hash;
        dib.hash = hash;
        if (changed) ++stats.changed;
        if (changed && !dib.touched && !SessionOpenOn(s, it->first)) {
            ++stats.uncaughtFrame;
            AddRow(s, "uncaught", dib.owner, Size(dib.width, dib.height), "(between frames)", dib.lastSession, {});
        }
        if (changed) stats.pending = true;
        dib.touched = false;
        ++it;
    }
    for (auto& [key, stats] : s.stats) {
        if (!stats.pending) continue;
        stats.pending = false;
        stats.run = stats.lastDrawn + 1 == s.frame ? stats.run + 1 : 1;
        if (stats.run > stats.maxRun) stats.maxRun = stats.run;
        stats.lastDrawn = s.frame;
        ++stats.drawn;
    }
}

void Write() {
    State& s = S();
    std::lock_guard<std::mutex> hold(s.lock);
    if (!s.installed) return;
    char path[MAX_PATH];
    ei::OutputPath("ui_census.csv", path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    std::fprintf(f, "# ui_census install=%s frames=%u cpuwindows=%d\n", GetModuleHandleA("addon.dll") ? "rebirth" : "vanilla",
                 s.frame, render::CpuWindows() ? 1 : 0);
    std::fprintf(f, "kind,owner,size,primitive,chain,count,frames,detail\n");
    for (const auto& [key, row] : s.rows) {
        std::size_t a = key.find('\t'), b = key.find('\t', a + 1), c = key.find('\t', b + 1);
        std::string detail;
        for (const std::string& d : row.details) detail += (detail.empty() ? "" : " | ") + d;
        if (row.moreDetails) detail += " | ...";
        std::fprintf(f, "%s,%s,%s,%s,%s,%u,%u,%s\n", row.kind, Quote(key.substr(0, a)).c_str(),
                     key.substr(a + 1, b - a - 1).c_str(), Quote(key.substr(b + 1, c - b - 1)).c_str(),
                     Quote(key.substr(c + 1)).c_str(), row.count, row.frames, Quote(detail).c_str());
    }
    for (const auto& [key, st] : s.stats) {
        std::size_t a = key.find('\t');
        std::fprintf(f, "surface,%s,%s,-,,%u,%u,\"alive=%u;changed=%u;maxrun=%u;uncaught_session=%u;uncaught_frame=%u\"\n",
                     Quote(key.substr(0, a)).c_str(), key.substr(a + 1).c_str(), st.sessions, st.drawn, st.alive,
                     st.changed, st.maxRun, st.uncaughtSession, st.uncaughtFrame);
    }
    std::fclose(f);
    ei::Log("census: %u rows, %u surfaces written to ui_census.csv", unsigned(s.rows.size()), unsigned(s.stats.size()));
}

} // namespace ei::verify::census
