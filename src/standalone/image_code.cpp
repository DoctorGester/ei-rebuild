// game.exe's code in the standalone image (S18i; docs/notes/standalone.md, "Own data"). Nothing of game.exe's code is
// at its addresses (S18k: the host keeps what of game.exe's range is free reserved with no access): nothing runs there
// and nothing jumps through it. What runs for an
// address of game.exe's code is CodeAddress's: the replacement registered for it (InstallHooks hands the table over:
// SetCodeTable), else a stub that reports there is no code at the address. The code cells of game.exe's data (vtable
// slots, callback tables, the static initialiser list: image_code_cells.inc, tools/gen_image_cells.py) are pointed at
// it once the table is in (FixCodeCells), and the static initialisers, the code literals and the harness ask
// CodeAddress.
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

namespace {


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

// How often CodeAddress was asked (the harness's renderstats; counted without locking).
std::uint64_t g_lookups;

std::atomic<bool> g_cellsFixed{false};
std::vector<std::uintptr_t*> g_notedPointers;   // FixUpCodePointer's before the cells are fixed (start-up only)

} // namespace

std::uintptr_t CodeAddress(std::uintptr_t address) {
    ++g_lookups;
    if (!InGameText(address)) return address;
    if (void* replacement = Registered(address)) return reinterpret_cast<std::uintptr_t>(replacement);
    return reinterpret_cast<std::uintptr_t>(Stub(address));
}

std::uint64_t CodeLookups() { return g_lookups; }

bool HasCode(std::uintptr_t address) { return !InGameText(address) || Registered(address); }

void FixUpCodePointer(std::uintptr_t* pointer) {
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
