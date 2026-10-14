// Checks for unit_client_effects.cpp (A-U2). All ours only: they play sounds, make particle
// sources and decals and draw rand() values, so the original cannot run a second time.
// - HitEffect (00537500): a blood source joins the unit's effect sources exactly when the hit
//   reaches its location's part of loaded figures and the model has a blood kind 1..8.
// - SparkEffect (00537C00): one more effect source; the flash countdown is at least 4.
// - DeathEffect (005373D0) / OwnerPlayerChanged (00534140): the controller's fields stay as they
//   were.
// Reached by: combat and combat_more (hits, deaths on the client), spells_cast (sparks: lightning
// hits), units / unit_create (a unit given to a player: AddUnitToServer).
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"
#include "../../src/game/unit_db.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unusedHit;
void* s_unusedSpark;
void* s_unusedDeath;
void* s_unusedPlayer;

const char* const kHitParts[6] = {"hd", "bd", "lh2", "rh2", "ll2", "rl2"};

int EffectSources(CUnit* unit) { return unit->Object3D()->effectSources.count; }

// The original's FindPartByPrefix (00516A80) on the unit.
CFigureMesh* FindPart(CUnit* unit, const char* name) { return unit->Object3D()->FindPartByPrefix(name); }

constexpr int kControllerFrom = 0x1C;
constexpr int kControllerTo = 0xDC;
} // namespace

static void __fastcall HitEffectVerify(CUnitClientSpecific* self, void*, int location, float amount,
                                       std::uint32_t flags) {
    CUnit* unit = self->unit;
    int before = EffectSources(unit);
    bool expectSource = false;
    if (!(flags & 8) && amount > 0.0001f && location >= 0 && location <= 5 && (unit->flags_ & 0x80) &&
        FindPart(unit, kHitParts[location])) {
        const auto* model = reinterpret_cast<const UnitModelRecord*>(unit->GetPrototypeModel());
        expectSource = model->bloodKind >= 1 && model->bloodKind <= 8;
    }
    self->HitEffect(location, amount, flags);
    int after = EffectSources(unit);
    ei::verify::Check("CUnitClientSpecific::HitEffect", true, after == before + (expectSource ? 1 : 0),
                      "unit %d location %d amount %g: %d -> %d sources", unit->id_, location, amount, before, after);
}
EI_VERIFY(0x00537500, HitEffectVerify, 0, s_unusedHit);

static void __fastcall SparkEffectVerify(CUnitClientSpecific* self, void*, int unused) {
    int before = EffectSources(self->unit);
    self->SparkEffect(unused);
    bool ok = EffectSources(self->unit) == before + 1 && self->flashCountdown >= 4;
    ei::verify::Check("CUnitClientSpecific::SparkEffect", true, ok, "unit %d flash %d", self->unit->id_,
                      self->flashCountdown);
}
EI_VERIFY(0x00537C00, SparkEffectVerify, 0, s_unusedSpark);

static void __fastcall DeathEffectVerify(CUnitClientSpecific* self) {
    std::uint8_t before[kControllerTo - kControllerFrom];
    std::memcpy(before, reinterpret_cast<std::uint8_t*>(self) + kControllerFrom, sizeof(before));
    self->DeathEffect();
    bool same = std::memcmp(before, reinterpret_cast<std::uint8_t*>(self) + kControllerFrom, sizeof(before)) == 0;
    ei::verify::Check("CUnitClientSpecific::DeathEffect", true, same, "unit %d", self->unit->id_);
}
EI_VERIFY(0x005373D0, DeathEffectVerify, 0, s_unusedDeath);

static void __fastcall OwnerPlayerChangedVerify(CUnitClientSpecific* self, void*, void* player) {
    std::uint8_t before[kControllerTo - kControllerFrom];
    std::memcpy(before, reinterpret_cast<std::uint8_t*>(self) + kControllerFrom, sizeof(before));
    self->OwnerPlayerChanged(player);
    bool same = std::memcmp(before, reinterpret_cast<std::uint8_t*>(self) + kControllerFrom, sizeof(before)) == 0;
    ei::verify::Check("CUnitClientSpecific::OwnerPlayerChanged", true, same, "unit %d player %p", self->unit->id_,
                      player);
}
EI_VERIFY(0x00534140, OwnerPlayerChangedVerify, 0, s_unusedPlayer);
