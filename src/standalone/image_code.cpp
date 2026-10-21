// game.exe's code in the standalone image (S18i; docs/notes/standalone.md, "Own data"). The image's code section is
// guarded (the host makes it PAGE_NOACCESS): nothing runs there and nothing jumps through it. What runs for an
// address of game.exe's code is CodeAddress's: the harness's replacement (RedirectCode), the replacement registered for
// it (InstallHooks hands the table over: SetCodeTable), else a stub that reports there is no code at the address. The
// code cells of game.exe's data (vtable slots, callback tables, the static initialiser list: image_code_cells.inc,
// tools/gen_image_cells.py) are pointed at it once the table is in (FixCodeCells), the forwarders (EI_ORIGINAL) jump
// to it through their CodeLink once it is fixed (BindCode; S18i1), and the static initialisers, the code literals and
// the harness ask CodeAddress.
// In game.exe (the original's runs) every address is its own code.
#include "ei/hook.h"
#include "game/game_data.h"
#include "hook/log.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ei {

// "no code at <address> (...), [esp] <caller>": reported and the process ends (hook/standalone.cpp).
[[noreturn]] void ReportNoCode(std::uintptr_t address, std::uintptr_t caller);

namespace {

// game.exe's code section (.text, 0x401000-0x73A670).
constexpr std::uintptr_t kTextBegin = 0x00401000;
constexpr std::uintptr_t kTextEnd = 0x0073A670;

bool InText(std::uintptr_t address) { return address - kTextBegin < kTextEnd - kTextBegin; }

struct CodeCell {
    std::uintptr_t cell;     // a dword of game.exe's .rdata or .data
    std::uintptr_t target;   // the address of game.exe's code it holds
};
constexpr CodeCell kCodeCells[] = {
#include "image_code_cells.inc"
};

// The registrations InstallHooks took, by address (SetCodeTable), and whether it has.
std::vector<std::pair<std::uintptr_t, void*>> g_table;
std::atomic<bool> g_tableReady{false};

// The harness's replacements (RedirectCode): few, set at its start-up; read from any thread.
struct Redirect {
    std::uintptr_t address;
    const void* to;
};
Redirect g_redirects[32];
std::atomic<int> g_redirectCount{0};

const void* RedirectedTo(std::uintptr_t address) {
    const int n = g_redirectCount.load(std::memory_order_acquire);
    for (int i = n - 1; i >= 0; --i)
        if (g_redirects[i].address == address) return g_redirects[i].to;
    return nullptr;
}

void* Registered(std::uintptr_t address) {
    if (!g_tableReady.load(std::memory_order_acquire)) return nullptr;
    const auto it = std::lower_bound(g_table.begin(), g_table.end(), address,
                                     [](const std::pair<std::uintptr_t, void*>& e, std::uintptr_t a) { return e.first < a; });
    return it != g_table.end() && it->first == address ? it->second : nullptr;
}

// A stub's CALL lands here: its return address points at the address the stub stands for (the dword after the
// CALL), the argument is the return address of the call into the stub.
__declspec(noinline) void __cdecl NoCodeEntry(std::uintptr_t caller) {
    ReportNoCode(*static_cast<const std::uint32_t*>(_ReturnAddress()), caller);
}

// The stubs: 16-byte slots (CALL NoCodeEntry, the address) cut from executable blocks, one per address.
SRWLOCK g_stubLock = SRWLOCK_INIT;
unsigned char* g_stubNext;
unsigned char* g_stubEnd;

void* Stub(std::uintptr_t address) {
    static constexpr std::size_t kSlot = 16, kBlock = 64 * 1024;
    AcquireSRWLockExclusive(&g_stubLock);
    static std::unordered_map<std::uintptr_t, void*> stubs;
    void*& stub = stubs[address];
    if (!stub) {
        if (g_stubNext == g_stubEnd) {
            g_stubNext = static_cast<unsigned char*>(
                VirtualAlloc(nullptr, kBlock, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            g_stubEnd = g_stubNext ? g_stubNext + kBlock : nullptr;
        }
        if (unsigned char* s = g_stubNext) {
            g_stubNext += kSlot;
            s[0] = 0xE8;   // CALL rel32
            const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&NoCodeEntry) -
                                                       (reinterpret_cast<std::uintptr_t>(s) + 5));
            std::memcpy(s + 1, &rel, 4);
            const auto at = static_cast<std::uint32_t>(address);
            std::memcpy(s + 5, &at, 4);
            std::memset(s + 9, 0xCC, kSlot - 9);
            stub = s;
        }
    }
    void* result = stub;
    ReleaseSRWLockExclusive(&g_stubLock);
    return result;
}

// The forwarders' bindings (BindCode): the bound links, listed once (ended by kLinksEnd, so a listed link's next is
// never null), and whether binding is on (the harness's A/B measurement turns it off). RedirectCode and turning it
// off unbind every link, under the lock that binding takes.
SRWLOCK g_linkLock = SRWLOCK_INIT;
CodeLink* const kLinksEnd = reinterpret_cast<CodeLink*>(1);
CodeLink* g_links = kLinksEnd;
bool g_binding = true;

void UnbindLinks() {
    for (CodeLink* link = g_links; link != kLinksEnd; link = link->next) link->code = nullptr;
}

// How often CodeAddress was asked, and by forwarders (the harness's renderstats; counted without locking).
std::uint64_t g_lookups;
std::uint64_t g_forwarderLookups;

std::atomic<bool> g_cellsFixed{false};
std::vector<std::uintptr_t*> g_notedPointers;   // FixUpCodePointer's before the cells are fixed (start-up only)

} // namespace

std::uintptr_t CodeAddress(std::uintptr_t address) {
    ++g_lookups;
    if (!Standalone() || !InText(address)) return address;
    if (const void* to = RedirectedTo(address)) return reinterpret_cast<std::uintptr_t>(to);
    if (void* replacement = Registered(address)) return reinterpret_cast<std::uintptr_t>(replacement);
    return reinterpret_cast<std::uintptr_t>(Stub(address));
}

// The forwarder's link is bound when its code is fixed: the address itself (game.exe, or outside game.exe's code)
// or the registration; never the harness's redirection or a stub, and not before the table is in.
std::uintptr_t BindCode(CodeLink* link) {
    ++g_forwarderLookups;
    const std::uintptr_t address = link->address;
    const std::uintptr_t code = CodeAddress(address);
    if (Standalone() && InText(address) && !Registered(address)) return code;
    AcquireSRWLockExclusive(&g_linkLock);
    if (g_binding && !RedirectedTo(address)) {
        if (!link->next) {
            link->next = g_links;
            g_links = link;
        }
        link->code = reinterpret_cast<const void*>(code);
    }
    ReleaseSRWLockExclusive(&g_linkLock);
    return code;
}

CodeLookupCounts CodeLookups() { return {g_lookups, g_forwarderLookups}; }

void SetCodeBinding(bool on) {
    AcquireSRWLockExclusive(&g_linkLock);
    g_binding = on;
    if (!on) UnbindLinks();
    ReleaseSRWLockExclusive(&g_linkLock);
}

bool RedirectedByHarness(std::uintptr_t address) { return RedirectedTo(address) != nullptr; }

bool HasCode(std::uintptr_t address) {
    return !Standalone() || !InText(address) || RedirectedTo(address) || Registered(address);
}

void RedirectCode(std::uintptr_t address, const void* to) {
    const std::uintptr_t before = CodeAddress(address);
    const int n = g_redirectCount.load(std::memory_order_relaxed);
    if (n == static_cast<int>(std::size(g_redirects))) {
        Log("standalone: the harness's replacement of %08X not taken: the table is full", static_cast<unsigned>(address));
        return;
    }
    AcquireSRWLockExclusive(&g_linkLock);
    g_redirects[n] = {address, to};
    g_redirectCount.store(n + 1, std::memory_order_release);
    UnbindLinks();
    ReleaseSRWLockExclusive(&g_linkLock);
    if (!g_cellsFixed.load(std::memory_order_acquire)) return;
    int cells = 0;
    for (const CodeCell& c : kCodeCells) {
        auto& value = GameData<std::uintptr_t>(c.cell);
        if (c.target != address || value != before) continue;
        value = reinterpret_cast<std::uintptr_t>(to);
        ++cells;
    }
    Log("standalone: %08X replaced by the harness's %p (%d code cell(s))", static_cast<unsigned>(address), to, cells);
}

void FixUpCodePointer(std::uintptr_t* pointer) {
    if (!Standalone()) return;
    if (g_cellsFixed.load(std::memory_order_acquire)) *pointer = CodeAddress(*pointer);
    else g_notedPointers.push_back(pointer);
}

// InstallHooks: the registrations it took (address, replacement), the last one per address winning.
void SetCodeTable(const std::vector<std::pair<std::uintptr_t, void*>>& installed) {
    std::vector<std::pair<std::uintptr_t, void*>> table;
    table.reserve(installed.size());
    for (auto it = installed.rbegin(); it != installed.rend(); ++it) table.push_back(*it);
    std::stable_sort(table.begin(), table.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    table.erase(std::unique(table.begin(), table.end(), [](const auto& a, const auto& b) { return a.first == b.first; }),
                table.end());
    g_table = std::move(table);
    g_tableReady.store(true, std::memory_order_release);
}

// Points every code cell of game.exe's data at CodeAddress of what it holds (after SetCodeTable, before anything of
// the game runs), then the pointers the harness noted. A cell that no longer holds game.exe's value (the harness
// took it) is left. Returns the number of cells without code (at a stub).
int FixCodeCells() {
    int registered = 0, stubs = 0, left = 0;
    for (const CodeCell& c : kCodeCells) {
        auto& value = GameData<std::uintptr_t>(c.cell);
        if (value != c.target) {
            Log("standalone: the code cell %08X holds %08X, not game.exe's %08X: left", static_cast<unsigned>(c.cell),
                static_cast<unsigned>(value), static_cast<unsigned>(c.target));
            ++left;
            continue;
        }
        (HasCode(c.target) ? registered : stubs) += 1;
        value = CodeAddress(c.target);
    }
    g_cellsFixed.store(true, std::memory_order_release);
    for (std::uintptr_t* pointer : g_notedPointers) *pointer = CodeAddress(*pointer);
    Log("standalone: %d code cell(s) of game.exe's data point at our code, %d at stubs (no code), %d left; %zu "
        "pointer(s) the harness took resolved",
        registered, stubs, left, g_notedPointers.size());
    g_notedPointers.clear();
    return stubs;
}

} // namespace ei

// An unbound EI_ORIGINAL forwarder's code (called from ei_code_bind with the caller's registers saved).
extern "C" std::uintptr_t __cdecl ei_code_bind_lookup(ei::CodeLink* link) { return ei::BindCode(link); }

// An unbound EI_ORIGINAL forwarder's target: [ESP] its CodeLink, [ESP+4] the caller's return address, the arguments
// in their registers (EAX too: some originals take one there; ECX, EDX) and on the stack as the forwarder received
// them. The code is reached with all of them as they were.
extern "C" __declspec(naked) void ei_code_bind() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 12]
        call ei_code_bind_lookup
        add esp, 4
        mov [esp + 12], eax
        pop edx
        pop ecx
        pop eax
        ret
    }
}
