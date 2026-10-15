// ei_vectors.exe links our game sources without the hook framework and without game.exe: their
// registrations install nothing, they only say which of our functions stands for an address. An
// EI_ORIGINAL forwarder jumps to that function (ei_replay_dispatch), as the standalone image's jump at
// the address would, or to a replay stand-in a check registered (ReplayStandIn) for code the replay
// leaves out. An address with neither raises kMissingForwarder: a check that guards its call fails
// naming the address, elsewhere the replay stops.
#include "ei/hook.h"
#include "vectors.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace ei {

namespace {
HookEntry* g_entries = nullptr;   // constant-initialised: safe for the registrars' static initialisers

struct StandIn {
    std::uintptr_t address;
    void* function;
};
// Function-local: filled by the checks' static initialisers.
std::vector<StandIn>& StandIns() {
    static std::vector<StandIn> standIns;
    return standIns;
}
}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* replacement, const char* name)
    : entry{address, replacement, name, g_entries} {
    g_entries = &entry;
}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen, void** original)
    : entry{address, wrapper, name, nullptr, stolen, original, true} {}

// ei_vectors runs without game.exe: code that would write the exe's own runtime data in the hook build
// (the game CRT's errno, libcrt::WideToBytes) leaves it alone, as in the standalone image.
bool Standalone() { return true; }

} // namespace ei

void ei::vectors::ReplayStandIn(std::uintptr_t address, void* function, const char*) {
    ei::StandIns().push_back({address, function});
}

// Where the forwarder to `address` goes (called from ei_replay_dispatch with the caller's registers
// saved): a stand-in first, then the replacement.
extern "C" void* __cdecl ei_replay_lookup(std::uintptr_t address) {
    for (const ei::StandIn& s : ei::StandIns())
        if (s.address == address) return s.function;
    static const std::unordered_map<std::uintptr_t, void*> table = [] {
        std::unordered_map<std::uintptr_t, void*> t;
        for (const ei::HookEntry* e = ei::g_entries; e; e = e->next) {
            auto [at, added] = t.emplace(e->address, e->replacement);
            if (!added && at->second != e->replacement) {
                std::fprintf(stderr, "ei_vectors: two replacements for %08X\n", static_cast<unsigned>(e->address));
                std::exit(3);
            }
        }
        return t;
    }();
    const auto at = table.find(address);
    if (at != table.end()) return at->second;
    std::fprintf(stderr, "ei_vectors: game code %08X has no rewrite (an EI_ORIGINAL forwarder was called)\n",
                 static_cast<unsigned>(address));
    std::fflush(stderr);
    const ULONG_PTR info[2] = {0, address};
    RaiseException(ei::vectors::kMissingForwarder, EXCEPTION_NONCONTINUABLE, 2, info);
    std::exit(3);
}

// EI_ORIGINAL's target in ei_vectors: [ESP] the address, [ESP+4] the caller's return address, the
// arguments in their registers (EAX too: some originals take one there; ECX, EDX) and on the stack as
// the forwarder received them. The target is reached with all of them as they were.
extern "C" __declspec(naked) void ei_replay_dispatch() {
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 12]
        call ei_replay_lookup
        add esp, 4
        mov [esp + 12], eax
        pop edx
        pop ecx
        pop eax
        ret
    }
}
