// Checks for the world's manager creation (src/game/world_create.cpp): ours only (it allocates
// and attaches the managers); afterwards every manager slot is set.
#include "verify.h"

#include <cstdint>

int __fastcall WorldCreateManagers(std::uint8_t* world);

namespace {
void* s_unused;
}

static int __fastcall WorldCreateManagersVerify(std::uint8_t* world) {
    int result = WorldCreateManagers(world);
    bool ok = result == 1;
    for (int offset : {0x38, 0x3C, 0x40, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C})
        if (!*reinterpret_cast<void* const*>(world + offset)) ok = false;
    ei::verify::Check("World::CreateManagers", true, ok, "world %p", world);
    return result;
}
EI_VERIFY(0x0055EE40, WorldCreateManagersVerify, 0, s_unused);
