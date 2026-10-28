// CUnitServer slots about other objects and the unit's own setup (A-U5): hostility claims, taking up a
// weapon, an object being deleted, settling the unit again. See combat.h and docs/notes/combat.md.
#include "combat.h"

#include "ai_map.h"
#include "item.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/x87.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"

#include <bit>
#include <cmath>

namespace {
constexpr int kOrderAnimation = 7;
constexpr int kOrderEmote = 8;
constexpr int kOrderNone = 9;
constexpr int kCallClaim = 0x1E;        // the kind of the AI memory's call for a claim
constexpr int kPartWeapons = 6;         // CUnit::MarkChanged part

AIBrain* BrainOf(CUnitServer* self) { return &self->brain; }

// The weapon's condition (CItem +0x20, a float) rounded to nearest is above 0.
bool Usable(const CItem* weapon) { return weapon && x87::RoundToInt(weapon->condition_) > 0; }
} // namespace

void CUnitServer::ClaimHostility(CUnit* claimant, std::uint32_t mask) {
    claimedHostile |= mask;
    std::string text;
    for (int player = 0; player < 32; ++player) {
        if (!(mask & (1u << player))) continue;
        std::string piece;
        piece = str::Format("Unit %i claims that player %i are enemies! ", claimant->id_, player);
        text += piece;
    }
    BrainOf(this)->memory.AddClaim(mask, kCallClaim, text);
}

BOOL CUnitServer::TakeUpWeapon(int slot) {
    auto* weapon = unit->GetWeapon(static_cast<std::uint32_t>(slot));
    int taken = Usable(weapon) ? slot : -1;
    unit->MarkChanged(kPartWeapons);
    unit->activeWeapon = static_cast<std::uint32_t>(taken);
    unit->pendingWeapon = taken;
    if (unit->player) BrainOf(this)->attention.Build(unit);
    RefreshItemSpells();
    return Usable(weapon) ? 1 : 0;
}

void CUnitServer::OnObjectDeleted(CEngineObject* object) {
    if (!object) return;
    if (action.target == object) {
        action.Reset();
        if (action2.kind == kOrderNone) Idle();
    }
    if (action2.target == object && action.kind != 0 && action2.kind != kOrderAnimation && action2.kind != kOrderEmote)
        DropQueuedOrder();
    if (currentPlan.targetId == object->id_) EndOrder(0);
    BrainOf(this)->OnObjectDeleted(object);
    if (followedLeader == object) {
        followsLeader = 0;
        CUnit* old = followedLeader;
        followedLeader = nullptr;
        if (old) old->Release();
    }
}

void CUnitServer::Resettle() {
    if (unit->IsDead()) return;
    auto* world = ServerWorld();
    world->aiMap_.sectorMap.Remove(unit);
    unit->Object3D()->SetUpAnimatedFigure(unit->name);
    unit->UpdateSizeClasses();
    unit->UpdateSizeClass();
    CAIMap* map = &world->aiMap_;
    if (!map->CanLeaveCell(unit)) {
        auto* position = &unit->localPosition_;
        Vector3 spot;
        spot = *position;
        if (map->FindFreeCellNear(unit, position, &spot)) {
            *position = spot;
        } else {
            DamageInfo hit;
            ResetDamageInfo(&hit);
            TakeHit(&hit, nullptr, nullptr);
            g_serverWorld->aiMap_.sectorMap.Add(unit);
            return;
        }
    }
    g_serverWorld->aiMap_.sectorMap.Add(unit);
}

int CUnitServer::Create() {
    ResetController();
    BrainOf(this)->SetOwner(this);
    return 1;
}

void CUnitServer::ClearState() { ResetController(); }

void CUnitServer::Destroy() { ResetController(); }

int CUnitServer::TamingStage() { return tameStage; }
