// UnitServer slots about other objects and the unit's own setup (A-U5): hostility claims, taking up a
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

#include <bit>
#include <cmath>
#include <format>

namespace {
constexpr int kCallClaim = 0x1E;        // the kind of the AI memory's call for a claim
constexpr int kPartWeapons = 6;         // CUnit::MarkChanged part


// The weapon's condition (Item +0x20, a float) rounded to nearest is above 0.
bool Usable(const Item* weapon) { return weapon && x87::RoundToInt(weapon->condition) > 0; }
} // namespace

void UnitServer::ClaimHostility(CUnit* claimant, std::uint32_t mask) {
    claimedHostile |= mask;
    std::string text;
    for (int player = 0; player < 32; ++player) {
        if (!(mask & (1u << player))) continue;
        std::string piece;
        piece = std::format("Unit {} claims that player {} are enemies! ", claimant->id, player);
        text += piece;
    }
    brain.memory.AddClaim(mask, kCallClaim, text);
}

bool UnitServer::TakeUpWeapon(int slot) {
    auto* weapon = unit->GetWeapon(static_cast<std::uint32_t>(slot));
    int taken = Usable(weapon) ? slot : -1;
    unit->MarkChanged(kPartWeapons);
    unit->activeWeapon = static_cast<std::uint32_t>(taken);
    unit->pendingWeapon = taken;
    if (unit->player) brain.attention.Build(unit);
    RefreshItemSpells();
    return Usable(weapon);
}

void UnitServer::OnObjectDeleted(EngineObject* object) {
    if (!object) return;
    if (action.target == object) {
        action.Reset();
        if (action2.kind == UnitOrderKind::None) Idle();
    }
    if (action2.target == object && action.kind != UnitOrderKind::Idle && action2.kind != UnitOrderKind::Animation && action2.kind != UnitOrderKind::Emote)
        DropQueuedOrder();
    if (currentPlan.targetId == object->id) EndOrder(0);
    brain.OnObjectDeleted(object);
    if (followedLeader == object) {
        followsLeader = 0;
        followedLeader.Reset();
    }
}

void UnitServer::Resettle() {
    if (unit->IsDead()) return;
    auto* world = ServerWorld();
    world->aiMap.sectorMap.Remove(unit);
    unit->Object3D()->SetUpAnimatedFigure(unit->name);
    unit->UpdateSizeClasses();
    unit->UpdateSizeClass();
    AIMap* map = &world->aiMap;
    if (!map->CanLeaveCell(unit)) {
        auto* position = &unit->localPosition;
        Vector3 spot;
        spot = *position;
        if (map->FindFreeCellNear(unit, *position, &spot)) {
            *position = spot;
        } else {
            DamageInfo hit;
            ResetDamageInfo(&hit);
            TakeHit(&hit, nullptr, nullptr);
            g_serverWorld.aiMap.sectorMap.Add(unit);
            return;
        }
    }
    g_serverWorld.aiMap.sectorMap.Add(unit);
}

void UnitServer::Create() {
    ResetController();
    brain.SetOwner(this);
}

void UnitServer::ClearState() { ResetController(); }

void UnitServer::Destroy() { ResetController(); }

int UnitServer::TamingStage() { return tameStage; }
