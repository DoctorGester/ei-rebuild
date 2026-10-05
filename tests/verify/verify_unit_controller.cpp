// Checks for unit_controller.cpp (A-U). All call the controller, the player or the AI motion and
// release objects, so only ours runs and the state afterwards is checked:
// - SetHostileMask (0052E970): the mask stored, flags_ 0x80000 and part 11 marked.
// - RefreshMotion (0052E9D0): the motion's speed scale and turn rate are GetMoveSpeeds' values
//   (read before the call) and part 10 is marked.
// - SetPlayer (005300D0): the unit's player is the new one, which owns it (00665440); part 11
//   marked when it changed.
// - ReleaseUnit (0052E740): no player, empty item slots, no carried items.
// - DestroyUnit (0052E590): as ReleaseUnit, and no controller (Reset).
// Reached by: SetHostileMask the zone loader (005927C0) and diplomacy changes (CDiplomacy::
// SetRelation, script SetDiplomacy: every scenario's zone load, unit_ai); RefreshMotion every
// path start of the server controller (movement, combat, units, unit_ai); SetPlayer the zone
// loader, the script's SetPlayer, taming and a unit's death (unit_create, units, combat_more);
// ReleaseUnit / DestroyUnit every unit removed or destroyed (unit_create's RemoveObject, zone
// changes: zone_gz2g_traps, zone_base_bz1g).
#include "verify.h"

#include "../../src/game/ai_motion.h"
#include "../../src/game/originals_gameplay.h"
#include "../../src/game/unit.h"

#include <cstdint>

namespace {
void* s_unusedHostile;
void* s_unusedMotion;
void* s_unusedPlayer;
void* s_unusedRelease;
void* s_unusedDestroy;

bool Marked(const CUnit* unit, int part) { return (unit->changedParts & (1u << part)) != 0; }

bool SlotsEmpty(const CUnit* unit) {
    for (auto* item : unit->weapons)
        if (item) return false;
    for (auto* item : unit->quickItems)
        if (item) return false;
    for (auto* item : unit->spells)
        if (item) return false;
    for (auto* item : unit->armor)
        if (item) return false;
    return !unit->carried && unit->carriedCount == 0 && unit->carriedCapacity == 0;
}
} // namespace

static void __fastcall SetHostileMaskVerify(CUnit* self, void*, std::uint32_t mask) {
    self->SetHostileMask(mask);
    bool ok = self->relationsA == mask && (self->flags_ & 0x80000) && Marked(self, 11);
    ei::verify::Check("CUnit::SetHostileMask", true, ok, "unit %d mask %08X", self->id_, mask);
}
EI_VERIFY(0x0052E970, SetHostileMaskVerify, 0, s_unusedHostile);

static void __fastcall RefreshMotionVerify(CUnit* self) {
    float speed, turnRate;
    self->GetMoveSpeeds(&speed, &turnRate);
    self->RefreshMotion();
    const UnitMotion* motion = self->Motion();
    bool ok = motion->speedScale == speed && motion->baseTurnRate == turnRate && Marked(self, 10);
    ei::verify::Check("CUnit::RefreshMotion", true, ok, "unit %d: %g %g vs %g %g", self->id_, speed, turnRate,
                      motion->speedScale, motion->baseTurnRate);
}
EI_VERIFY(0x0052E9D0, RefreshMotionVerify, 0, s_unusedMotion);

static void __fastcall SetPlayerVerify(CUnit* self, void*, char* player) {
    bool changes = self->player != player;
    self->SetPlayer(player);
    bool ok = self->player == player && (!changes || Marked(self, 11)) &&
              (!player || PlayerOwnsUnitOriginal(player, nullptr, self));
    ei::verify::Check("CUnit::SetPlayer", true, ok, "unit %d player %p", self->id_, static_cast<void*>(player));
}
EI_VERIFY(0x005300D0, SetPlayerVerify, 0, s_unusedPlayer);

static void __fastcall ReleaseUnitVerify(CUnit* self) {
    self->ReleaseUnit();
    bool ok = !self->player && SlotsEmpty(self);
    ei::verify::Check("CUnit::ReleaseUnit", true, ok, "unit %d", self->id_);
}
EI_VERIFY(0x0052E740, ReleaseUnitVerify, 0, s_unusedRelease);

static void __fastcall DestroyUnitVerify(CUnit* self) {
    self->DestroyUnit();
    bool ok = !self->player && !self->controller && SlotsEmpty(self);
    ei::verify::Check("CUnit::DestroyUnit", true, ok, "unit %d", self->id_);
}
EI_VERIFY(0x0052E590, DestroyUnitVerify, 0, s_unusedDestroy);
