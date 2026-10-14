// Checks for world_managers.cpp (A-U7). 0055F8E0 CWorld::DestroyManagers deletes the managers:
// ours only, every manager slot empty afterwards. Reached when a world goes away: zone changes
// (zone_gz2g_traps, zone_base_bz1g), loading a save (world_load, ui_saveload).
#include "verify.h"

#include "../../src/game/world.h"

namespace {
void* s_unused;
}

static void __fastcall DestroyManagersVerify(CWorld* self) {
    self->DestroyManagers();
    bool ok = !self->figureLoader_ && !self->players_ && !self->manager3C_ && !self->manager40_ && !self->manager44_ &&
              !self->manager48_ && !self->manager4C_ && !self->manager50_ && !self->itemManager_ && !self->manager58_ &&
              !self->shared5C_.ptr && !self->netSession_;
    ei::verify::Check("CWorld::DestroyManagers", true, ok, "");
}
EI_VERIFY(0x0055F8E0, DestroyManagersVerify, 0, s_unused);
