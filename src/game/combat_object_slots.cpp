// CUnitServer slots about other objects and the unit's own setup (A-U5): hostility claims, taking
// up a weapon, an object being deleted, settling the unit again. See combat.h and
// docs/notes/combat.md.
#include "combat.h"

#include "ai_map.h"
#include "item.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "originals_ai.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace {
constexpr int kOrderAnimation = 7;
constexpr int kOrderEmote = 8;
constexpr int kOrderNone = 9;
constexpr int kCallClaim = 0x1E;        // the kind of the AI memory's call for a claim
constexpr int kPartWeapons = 6;         // CUnit::MarkChanged part

AIBrain* BrainOf(CUnitServer* self) { return &self->brain; }

// The weapon's condition (CItem +0x20, a float) rounded to nearest (FISTP) is above 0.
bool Usable(const CItem* weapon) { return weapon && x87::RoundToInt(std::bit_cast<float>(weapon->condition_)) > 0; }
} // namespace

void CUnitServer::ClaimHostility(CUnit* claimant, std::uint32_t mask) {
    claimedHostile |= mask;
    mfc::CString text;
    for (int player = 0; player < 32; ++player) {
        if (!(mask & (1u << player))) continue;
        mfc::CString piece;
        piece.Format("Unit %i claims that player %i are enemies! ", claimant->id_, player);
        text += piece;
    }
    CallsAddClaim(&BrainOf(this)->calls, nullptr, mask, kCallClaim, text);   // 005CBBC0 on the memory: its calls
}
EI_REPLACE_METHOD(0x00552D00, CUnitServer::ClaimHostility);

BOOL CUnitServer::TakeUpWeapon(int slot) {
    auto* weapon = static_cast<CItem*>(unit->GetWeapon(static_cast<std::uint32_t>(slot)));
    int taken = Usable(weapon) ? slot : -1;
    unit->MarkChanged(kPartWeapons);
    unit->activeWeapon = static_cast<std::uint32_t>(taken);
    unit->pendingWeapon = taken;
    if (unit->player) BrainOf(this)->attention.Build(unit);
    RefreshItemSpells();
    return Usable(weapon) ? 1 : 0;
}
EI_REPLACE_METHOD(0x00553610, CUnitServer::TakeUpWeapon);

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
        CEngineObject* old = followedLeader;
        followedLeader = nullptr;
        if (old) old->Release();
    }
}
EI_REPLACE_METHOD(0x00552670, CUnitServer::OnObjectDeleted);

void CUnitServer::Resettle() {
    if (ObjectSlots(unit)->IsDead()) return;
    auto* world = static_cast<CWorldServer*>(ManagerContext());
    world->Sectors()->Remove(unit);
    unit->Object3D()->SetUpAnimatedFigure(unit->name);
    unit->UpdateSizeClasses();   // CUnit slot 37 (both unit vtables hold 00530A50)
    unit->UpdateSizeClass();
    CAIMap* map = world->AIMap();
    if (!map->CanLeaveCell(unit)) {
        auto* position = &unit->position;
        Vector3 spot;
        std::memcpy(&spot, position, sizeof(spot));
        if (map->FindFreeCellNear(unit, position, &spot)) {
            std::memcpy(position, &spot, sizeof(spot));
        } else {
            DamageInfo hit;
            ResetDamageInfo(&hit);
            ServerSlots(this)->TakeHitSlot(&hit, nullptr, nullptr);
            TheServerWorld()->AIMap()->SectorMap()->Add(unit);
            return;
        }
    }
    TheServerWorld()->AIMap()->SectorMap()->Add(unit);
}
EI_REPLACE_METHOD(0x00553260, CUnitServer::Resettle);

int CUnitServer::CreateServer() {
    ResetController();
    BrainOf(this)->SetOwner(this);
    return 1;
}
EI_REPLACE_METHOD(0x00546450, CUnitServer::CreateServer);

void CUnitServer::ResetSlot() { ResetController(); }
EI_REPLACE_METHOD(0x00546080, CUnitServer::ResetSlot);

void CUnitServer::DestroyServer() { ResetController(); }
EI_REPLACE_METHOD(0x00546470, CUnitServer::DestroyServer);

int CUnitServer::TameStage() { return tameStage; }
EI_REPLACE_METHOD(0x005460A0, CUnitServer::TameStage);
