// ei_vectors.exe links our game sources without the hook framework and without game.exe: their
// registrations install nothing, they only say which of our functions stands for an address (the code
// literals' ei::Code, as in the standalone image).
#include "ei/hook.h"
#include "vectors.h"

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

// The code for game.exe's function at `address` in ei_vectors (ei/hook.h): the replacement registered for it, else
// the address itself (nothing runs there: calling it fails where it is called).
std::uintptr_t CodeAddress(std::uintptr_t address) {
    for (const HookEntry* e = g_entries; e; e = e->next)
        if (e->address == address) return reinterpret_cast<std::uintptr_t>(e->replacement);
    return address;
}

bool HasCode(std::uintptr_t address) { return CodeAddress(address) != address; }

} // namespace ei
