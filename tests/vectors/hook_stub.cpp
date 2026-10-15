// ei_vectors.exe links our game sources without the hook framework and without game.exe: their
// registrations install nothing, they only say which of our functions stands for an address. An
// EI_ORIGINAL forwarder jumps to that function (ei_replay_dispatch), as the standalone image's jump at
// the address would; an address without a replacement stops the replay.
#include "ei/hook.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace ei {

namespace {
HookEntry* g_entries = nullptr;   // constant-initialised: safe for the registrars' static initialisers
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

// The replacement for `address` (called from ei_replay_dispatch with the caller's registers saved).
extern "C" void* __cdecl ei_replay_lookup(std::uintptr_t address) {
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
    std::exit(3);
}

// EI_ORIGINAL's target in ei_vectors: [ESP] the address, [ESP+4] the caller's return address, the
// arguments in their registers (ECX, EDX) and on the stack as the forwarder received them.
extern "C" __declspec(naked) void ei_replay_dispatch() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 8]
        call ei_replay_lookup
        add esp, 4
        pop edx
        pop ecx
        add esp, 4
        jmp eax
    }
}
