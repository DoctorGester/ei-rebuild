// Combat, ninth part: taming (the skill and a spell call it). A tame attempt that beats the roll moves the
// creature a stage along: 1 no longer hostile to the tamer's side, 2 on the tamer's side (its own side kept in
// the controller), 3 in the tamer's party with a "tamed" AI mode; a failed attempt (or one on a creature with
// a player) undoes the taming when it was the tamer's side doing it, and counts as an attack. See
// docs/notes/combat.md ("Order lifecycle").
#include "combat.h"
#include "engine_object.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "player_messages.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include <cstdint>
#include <list>

namespace {

constexpr int kOrderNone = 9;
constexpr int kStatTaming = 2;

template <typename T>
void ReferenceRelease(T** a0) { static_cast<AIRef*>((void*)a0)->Reset(); }

int& Stage(CUnitServer* self) { return self->tameStage; }

// The side that tamed the unit (-1 when not tamed) is the attacker's: the taming is undone.
void UndoIfTamedBy(CUnitServer* self, CUnit* tamer) {
    int side = Stage(self) == 0 ? -1 : self->tamerSide;
    if (side == tamer->side) self->UndoTaming();
}

// An empty reference list of the brain (CList of one reference per node): each released, then RemoveAll.
// `release` picks how the original releases the element.
template <bool kViaRelease>
void ClearReferenceList(std::list<CUnit*>& list) {
    for (auto node = list.begin(); node != list.end(); ++node) {
        if (kViaRelease) {
            ReferenceRelease(&*node);
        } else {
            if (*node) (*node)->Release();
            *node = nullptr;
        }
    }
    list.clear();
}

} // namespace

BOOL CUnitServer::TryTame(CUnit* tamer, float roll) {
    float bonus = static_cast<float>(stats.AttributeWithItems(kStatTaming, unit));
    int level = TamingStage();
    float limit = static_cast<float>(level * 10) + bonus;
    if (unit->player || limit >= roll) {   // FCOMP: C0 (limit below the roll, or unordered) succeeds
        // A unit with a player cannot be tamed; a roll not above the limit fails. Either way an attack.
        UndoIfTamedBy(this, tamer);
        ServerWorld()->diplomacy_.OnAttack(tamer, unit, 10);
        return 0;
    }
    CPlayer* tamerPlayer = tamer->player;
    switch (Stage(this)) {
    case 0:
        unit->SetRelationsB(~(1u << (tamer->side & 31)));
        Stage(this) = 1;
        break;
    case 1:
        ownSide = unit->side;
        unit->SetSide(tamer->side);
        Stage(this) = 2;
        break;
    case 2: {
        if (!tamerPlayer) break;
        tamerPlayer->ReleaseTamedUnits();
        unit->SetSide(tamer->side);
        unit->SetPlayer(tamerPlayer);
        brain.AddMode(new TamedMode());
        ClearReferenceList<false>(brain.memory.seenCorpses);
        ClearReferenceList<true>(brain.memory.corpses);
        Stage(this) = 3;
        break;
    }
    default:
        break;
    }
    tamerSide = tamer->side;
    brain.StopAction();
    if (action.kind != 0 && action2.kind != 7 && action2.kind != 8) {
        action2.kind = kOrderNone;
        action2.target.Reset();
        action2.pointIsUseSpot = 0;
        action2.param = 0;
        action2.modifier = 0;
        action2.kind = 0;
        if (action2.target) {
            action2.target.Reset();
            action2.target = nullptr;
        }
        action2.modifier = 0;
    }
    return 1;
}

void CUnitServer::UndoTaming() {
    if (Stage(this) == 0) return;
    brain.RemoveModesOfType(kModeTamed);   // unit_ai.h: the tamed mode (8)
    unit->SetPlayer(nullptr);
    unit->SetRelationsB(0xFFFFFFFFu);
    if (Stage(this) > 1) unit->SetSide(ownSide);
    Stage(this) = 0;
}
