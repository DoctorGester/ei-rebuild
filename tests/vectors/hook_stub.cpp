// ei_vectors.exe links our game sources without the hook framework: their EI_REPLACE registrations
// register nothing here.
#include "ei/hook.h"

namespace ei {

HookRegistrar::HookRegistrar(std::uintptr_t address, void* replacement, const char* name)
    : entry{address, replacement, name, nullptr} {}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen, void** original)
    : entry{address, wrapper, name, nullptr, stolen, original, true} {}

} // namespace ei
