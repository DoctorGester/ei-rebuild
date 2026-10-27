// What runs for an address of game.exe's code (CodeAddress). See docs/notes/standalone.md, "The image's
// source files".
#include "ei/hook.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ei {

namespace {

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

} // namespace

std::uintptr_t CodeAddress(std::uintptr_t address) {
    ++g_lookups;
    if (!InGameText(address)) return address;
    if (void* replacement = Registered(address)) return reinterpret_cast<std::uintptr_t>(replacement);
    return reinterpret_cast<std::uintptr_t>(Stub(address));
}

std::uint64_t CodeLookups() { return g_lookups; }

bool HasCode(std::uintptr_t address) { return !InGameText(address) || Registered(address); }

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

} // namespace ei
