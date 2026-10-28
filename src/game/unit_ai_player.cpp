// The Player mode's fighting and party code (party units, PlayerMode::Execute's helpers): choosing
// whom to fight, the party's targets, the party itself and
// whether the unit's followers keep up.
#include "unit_ai.h"
#include "combat.h"

#include "logic_settings.h"
#include "map_object.h"
#include "player_messages.h"
#include "world.h"
#include "game_singletons.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>

namespace {
constexpr int kStandby = 1;                                     // CUnit::fightAtWill: may fight
constexpr int kAttackHow = 6;
constexpr int kDirectControl = 2;                               // CPlayerServer::refreshPending_
constexpr std::uint32_t kChoosingFlag = 0x4000000;              // CEngineObject::flags_ while choosing
constexpr float kReachScale = 1.1f;
static_assert(std::bit_cast<std::uint32_t>(kReachScale) == 0x3F8CCCCDu);
constexpr float kMovingSlack = 2.0f;
constexpr float kStandingSlack = 1.4f;
static_assert(std::bit_cast<std::uint32_t>(kStandingSlack) == 0x3FB33333u);
constexpr float kNoRadius = -1.0f;


// A list on the stack.
void InitList(CountedUnits& list) {
    list = {};
}

// The player controls the unit directly (its player's server connection in state 2).
bool DirectlyControlled(const CUnit* unit) {
    CPlayer* player = unit->player;
    return player && player->Server()->refreshPending_ == kDirectControl;
}

// Whom a party unit follows: its Player mode's leader, or the unit it follows (action 6).
CUnit* LeaderOf(const CUnit* unit) {
    AIBrain* brain = &unit->ServerController()->brain;
    CMapObject* leader = nullptr;
    if (auto* mode = brain->FindPlayerMode()) leader = mode->leader;
    if (!leader && brain->action == AIBrain::kActionFollow) leader = brain->target;
    return static_cast<CUnit*>(leader);
}

// The player's attack check: None (the planner says nothing) when the player's order check accepts an attack
// on `object`, else the AI's DecidedToAttack.
AckKind AttackFlags(AIBrain* brain, CMapObject* object, bool clearTwice) {
    AckKind flags = AckKind::DecidedToAttack;
    if (!brain->owner->unit->player) return flags;
    AIOrder order;
    order.target = nullptr;
    order.Clear();
    if (clearTwice) {
        // The second attack builds the order by hand after a second Clear.
        order.Clear();
        order.kind = kOrderAttack;
        order.source = kAttackHow;
        order.target.Assign(object);
    } else {
        order.MakeAttack(object);
    }
    if (brain->owner->CanReachAndPlan(&order)) flags = AckKind::None;
    order.target.Reset();
    return flags;
}

void Attack(AIBrain* brain, CMapObject* object, bool clearTwice) {
    brain->action = AIBrain::kActionAttack;
    brain->target.Assign(object);
    AckKind flags = AttackFlags(brain, object, clearTwice);
    brain->owner->OrderAttack(brain->target, kAttackHow, 0, flags);
}
} // namespace

void PlayerMode::UpdateCombat(AIBrain* brain) {
    CUnit* unit = brain->owner->unit;
    if (unit->fightAtWill != kStandby) return;

    // The commanded target first, when it can be reached from here.
    if (target && target->IsDead() == 0) {
        AITargetChoice choice{-1, static_cast<CUnit*>(target.object)};   // taken as a unit
        if (brain->attention.CanReach(brain, &choice, 1)) {
            Attack(brain, target, false);
            return;
        }
    }

    CountedUnits candidates;
    InitList(candidates);
    AITargetChoice best{-1, nullptr};
    float radius;
    if (state == kStateMoveTo) {
        radius = g_logicSettings.swarmRadius;
    } else {
        point = Vector3{0.0f, 0.0f, 0.0f};
        radius = kNoRadius;
    }
    brain->memory.FindEnemiesNear(unit, &candidates, &point, radius);
    // Only enemies that could notice the unit (1.1 x the larger of their sight and hearing).
    for (auto node = candidates.begin(); node != candidates.end();) {
        auto next = std::next(node);
        auto* other = *node;
        CUnitServer* control = other->ServerController();
        float sight = static_cast<float>(static_cast<double>(control->moveFactors[3]) * control->stats.perception[0].current);
        float hearing = control->stats.perception[2].current;
        EI_NAN_AUDIT(sight, hearing);
        float reach = sight < hearing ? hearing : sight;
        double limit = static_cast<double>(reach) * kReachScale;
        double dy = static_cast<double>(other->localPosition_.y) - unit->localPosition_.y;
        double dx = static_cast<double>(other->localPosition_.x) - unit->localPosition_.x;
        if (limit * limit < LengthSquared2DUnrounded(dx, dy)) candidates.erase(node);
        node = next;
    }
    AddPartyTargets(brain, &candidates);
    unit->flags_ |= kChoosingFlag;
    brain->ChooseTarget(&best, &candidates, &unit->localPosition_, 0, 0);
    unit->flags_ &= ~kChoosingFlag;

    CUnit* chosen = best.object;
    if (chosen) {
        if (!DirectlyControlled(brain->owner->unit)) {
            // The other followers that are not fighting stop following.
            CountedUnits party;
            InitList(party);
            CollectParty(brain, &party);
            for (auto node = party.begin(); node != party.end();) {
                auto* other = *node;
                ++node;
                if (other == unit) continue;
                AIBrain* theirs = &other->ServerController()->brain;
                auto* mode = theirs->FindPlayerMode();
                if (!mode || theirs->action == AIBrain::kActionAttack || theirs->action == AIBrain::kActionCast) continue;
                mode->state = kStateFollow;
                if (theirs->target) {
                    theirs->target.Reset();
                    theirs->target = nullptr;
                }
                theirs->action = AIBrain::kActionNone;
            }
            party.clear();
        }
        AITargetChoice now{-1, nullptr};
        brain->attention.CurrentAttack(brain, &now);
        if (now.object != best.object || now.part != best.part) Attack(brain, chosen, true);
        if (target) {
            target.Reset();
            target = nullptr;
        }
    }
    candidates.clear();
}

void PlayerMode::AddPartyTargets(AIBrain* brain, CountedUnits* targets) {
    CountedUnits nearby;
    InitList(nearby);
    CUnit* unit = brain->owner->unit;
    g_serverWorld->aiMap_.sectorMap.FindUnitsInRadius(&unit->localPosition_, &nearby,
                               g_logicSettings.playerCallForHelp);
    for (auto node = nearby.begin(); node != nearby.end();) {
        auto* other = *node;
        ++node;
        if (other == unit || other->player != unit->player) continue;
        AIBrain* theirs = &other->ServerController()->brain;
        auto* foe = static_cast<CUnit*>(theirs->target.object);
        if (!foe || foe->player == unit->player || theirs->action != AIBrain::kActionAttack ||
            foe->GetClassId() != ClassId::Unit)
            continue;
        if (lib::Contains(*targets, foe)) continue;
        targets->push_back(foe);
    }
    nearby.clear();
}

void PlayerMode::CollectParty(AIBrain* brain, CountedUnits* party) {
    party->clear();
    CUnit* unit = brain->owner->unit;
    if (!unit) return;
    party->push_back(unit);
    CPlayer* player = unit->player;
    if (!player) return;
    // The chain of leaders above the unit.
    for (const CUnit* member = unit;;) {
        CUnit* above = LeaderOf(member);
        if (!above || lib::Contains(*party, above)) break;
        party->push_back(above);
        member = above;
    }
    // Everyone following someone listed, until nobody is added.
    bool added;
    do {
        added = false;
        for (auto node = player->units_.begin(); node != player->units_.end() && node->second; ++node) {
            CUnit* member = node->second;
            CUnit* followed = LeaderOf(member);
            if (!lib::Contains(*party, followed) || lib::Contains(*party, member)) continue;
            party->push_back(member);
            added = true;
        }
    } while (added);
}

bool PlayerMode::StaysWithParty(AIBrain* brain, bool moving) {
    float slack = moving ? kMovingSlack : kStandingSlack;
    CUnit* unit = brain->owner->unit;
    CPlayer* player = unit->player;
    if (!player) return true;
    for (auto node = player->units_.begin(); node != player->units_.end() && node->second; ++node) {
        CUnit* member = node->second;
        PlayerMode* mode = member->ServerController()->brain.FindPlayerMode();
        // A unit without a Player mode would have its leader read at address 0x0C (a crash).
        if (!mode || mode->leader != unit) continue;
        double dx = static_cast<double>(member->localPosition_.x) - unit->localPosition_.x;
        double dy = static_cast<double>(member->localPosition_.y) - unit->localPosition_.y;
        double dz = static_cast<double>(member->localPosition_.z) - unit->localPosition_.z;
        double limit = static_cast<double>(g_logicSettings.followMaxDist) * slack + 1.0f;
        EI_NAN_AUDIT(limit, dx, dy, dz);
        if (limit * limit < LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) return false;
    }
    return true;
}
