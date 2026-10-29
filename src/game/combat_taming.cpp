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
#include <memory>

namespace {

constexpr int kStatTaming = 2;


// The side that tamed the unit (-1 when not tamed) is the attacker's: the taming is undone.
void UndoIfTamedBy(CUnitServer* self, CUnit* tamer) {
    int side = self->tameStage == 0 ? -1 : self->tamerSide;
    if (side == tamer->side) self->UndoTaming();
}

} // namespace

bool CUnitServer::TryTame(CUnit* tamer, float roll) {
    float bonus = static_cast<float>(stats.AttributeWithItems(kStatTaming, unit));
    int level = TamingStage();
    float limit = static_cast<float>(level * 10) + bonus;
    if (unit->player || limit >= roll) {   // not taken (the taming succeeds) when the limit is below the roll or NaN
        // A unit with a player cannot be tamed; a roll not above the limit fails. Either way an attack.
        UndoIfTamedBy(this, tamer);
        ServerWorld()->diplomacy_.OnAttack(tamer, unit);
        return false;
    }
    CPlayer* tamerPlayer = tamer->player;
    switch (tameStage) {
    case 0:
        unit->SetRelationsB(~(1u << (tamer->side & 31)));
        tameStage = 1;
        break;
    case 1:
        ownSide = unit->side;
        unit->SetSide(tamer->side);
        tameStage = 2;
        break;
    case 2: {
        if (!tamerPlayer) break;
        tamerPlayer->ReleaseTamedUnits();
        unit->SetSide(tamer->side);
        unit->SetPlayer(tamerPlayer);
        brain.AddMode(std::make_unique<TamedMode>());
        brain.memory.seenCorpses.clear();
        brain.memory.corpses.clear();
        tameStage = 3;
        break;
    }
    default:
        break;
    }
    tamerSide = tamer->side;
    brain.StopAction();
    if (action.kind != UnitOrderKind::Idle && action2.kind != UnitOrderKind::Animation && action2.kind != UnitOrderKind::Emote) {
        action2.kind = UnitOrderKind::None;
        action2.target.Reset();
        action2.pointIsUseSpot = false;
        action2.param = 0;
        action2.modifier = 0;
        action2.kind = UnitOrderKind::Idle;
        if (action2.target) {
            action2.target.Reset();
        }
        action2.modifier = 0;
    }
    return true;
}

void CUnitServer::UndoTaming() {
    if (tameStage == 0) return;
    brain.RemoveModesOfType(AIModeType::Tamed);   // unit_ai.h: the tamed mode (8)
    unit->SetPlayer(nullptr);
    unit->SetRelationsB(0xFFFFFFFFu);
    if (tameStage > 1) unit->SetSide(ownSide);
    tameStage = 0;
}
