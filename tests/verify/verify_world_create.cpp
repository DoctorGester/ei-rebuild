// Checks for the world's manager creation (src/game/world_create.cpp): ours only (it allocates
// and attaches the managers); afterwards every manager slot is set.
#include "verify.h"

#include "../../src/game/world.h"

namespace {
void* s_unused;
}

static int __fastcall WorldCreateManagersVerify(CWorld* world) {
    int result = WorldCreateManagers(world);
    bool ok = result == 1;
    for (const void* manager : {static_cast<const void*>(world->players_), static_cast<const void*>(world->manager3C_),
                                static_cast<const void*>(world->manager40_), static_cast<const void*>(world->manager48_),
                                static_cast<const void*>(world->manager4C_), static_cast<const void*>(world->manager50_),
                                static_cast<const void*>(world->itemManager_), static_cast<const void*>(world->manager58_),
                                static_cast<const void*>(world->shared5C_.ptr)})
        if (!manager) ok = false;
    ei::verify::Check("World::CreateManagers", true, ok, "world %p", world);
    return result;
}
EI_VERIFY(0x0055EE40, WorldCreateManagersVerify, 0, s_unused);
