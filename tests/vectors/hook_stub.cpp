// ei_vectors.exe links our game sources without the hook framework: their EI_REPLACE registrations
// register nothing here.
#include "ei/hook.h"

namespace ei {

HookRegistrar::HookRegistrar(std::uintptr_t address, void* replacement, const char* name)
    : entry{address, replacement, name, nullptr} {}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen, void** original)
    : entry{address, wrapper, name, nullptr, stolen, original, true} {}

// ei_vectors runs without game.exe: code that would write the exe's own runtime data in the hook build
// (the game CRT's errno, libcrt::WideToBytes) leaves it alone, as in the standalone image.
bool Standalone() { return true; }

} // namespace ei
