// The Player mode's fighting and party code (party units, PlayerMode::Execute's helpers): choosing
// whom to fight (005D4170), the party's targets (005D3F50), the party itself (005D4600) and
// whether the unit's followers keep up (005D4860).
#include "unit_ai.h"
#include "combat.h"

#include "logic_settings.h"
#include "map_object.h"
#include "originals_ai.h"
#include "player_messages.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <bit>
#include <cstdint>

namespace {
constexpr int kStandby = 1;                                     // CUnit::fightAtWill: may fight
constexpr int kAttackHow = 6;
constexpr int kAttackFlags = 0x1C;
constexpr int kDirectControl = 2;                               // CPlayerServer::refreshPending_
constexpr std::uint32_t kChoosingFlag = 0x4000000;              // CEngineObject::flags_ while choosing
constexpr float kReachScale = std::bit_cast<float>(0x3F8CCCCDu);    // 0073BAC4 (1.1)
constexpr float kOne = 1.0f;                                    // 0073B84C
constexpr float kMovingSlack = 2.0f;                            // 0x40000000
constexpr float kStandingSlack = std::bit_cast<float>(0x3FB33333u); // 1.4
constexpr float kNoRadius = -1.0f;                              // 0xBF800000

CUnitServer* ControllerOf(const CUnit* unit) { return reinterpret_cast<CUnitServer*>(unit->controller); }
AIBrain* BrainOf(const CUnit* unit) { return ControllerOf(unit)->Brain(); }
const Vector3* PositionOf(const CUnit* unit) { return &unit->position; }
CMapObjectVirtuals* MapObject(CEngineObject* object) { return static_cast<CMapObjectVirtuals*>(object); }
CPlayer* PlayerOf(const CUnit* unit) { return reinterpret_cast<CPlayer*>(unit->player); }

// A list on the stack as the original builds it (CPtrList, blocks of 10).
void InitList(AIObjectList& list) {
    list = {};
    list.vtable = mfc::PtrListVtable();
    list.blockSize = 10;
}

// The player controls the unit directly (its player's server connection in state 2).
bool DirectlyControlled(const CUnit* unit) {
    CPlayer* player = PlayerOf(unit);
    return player && reinterpret_cast<CPlayerServer*>(player->connection_)->refreshPending_ == kDirectControl;
}

// Whom a party unit follows: its Player mode's leader, or the unit it follows (action 6).
CEngineObject* LeaderOf(const CUnit* unit) {
    AIBrain* brain = BrainOf(unit);
    CEngineObject* leader = nullptr;
    if (auto* mode = static_cast<PlayerMode*>(brain->FindMode(kModePlayer))) leader = mode->leader;
    if (!leader && brain->action == AIBrain::kActionFollow) leader = brain->target;
    return leader;
}

// The player's attack check: -1 (no restrictions) when the player's order check accepts an attack
// on `object`, else the AI's flags.
int AttackFlags(AIBrain* brain, CEngineObject* object, bool clearTwice) {
    int flags = kAttackFlags;
    if (!brain->owner->unit->player) return flags;
    AIOrder order;
    order.object = nullptr;
    order.Clear();
    if (clearTwice) {
        // 005D4170's second attack builds the order by hand after a second Clear.
        order.Clear();
        order.kind = 3;
        order.what = kAttackHow;
        reinterpret_cast<AIRef*>(&order.object)->Assign(object);
    } else {
        order.MakeAttack(object);
    }
    if (brain->owner->CanReachAndPlan(AsUnitAction(&order))) flags = -1;
    reinterpret_cast<AIRef*>(&order.object)->Reset();
    return flags;
}

void Attack(AIBrain* brain, CEngineObject* object, bool clearTwice) {
    brain->action = AIBrain::kActionAttack;
    reinterpret_cast<AIRef*>(&brain->target)->Assign(object);
    int flags = AttackFlags(brain, object, clearTwice);
    brain->owner->OrderAttack(brain->target, kAttackHow, 0, flags);
}
} // namespace

void PlayerMode::UpdateCombat(AIBrain* brain) {
    CUnit* unit = brain->owner->unit;
    if (unit->fightAtWill != kStandby) return;

    // The commanded target first, when it can be reached from here.
    if (target && MapObject(target)->IsDead() == 0) {
        int partAndObject[2] = {-1, static_cast<int>(reinterpret_cast<std::uintptr_t>(target))};
        if (brain->attention.CanReach(brain, partAndObject, 1)) {
            Attack(brain, target, false);
            return;
        }
    }

    AIObjectList candidates;
    InitList(candidates);
    int best[2] = {-1, 0};          // {part, object}
    float radius;
    if (state == kStateMoveTo) {
        radius = GameLogicSettings().swarmRadius;
    } else {
        point = Vector3{0.0f, 0.0f, 0.0f};
        radius = kNoRadius;
    }
    brain->FindEnemiesNear(unit, &candidates, &point, radius);
    // Only enemies that could notice the unit (1.1 x the larger of their sight and hearing).
    for (auto* node = candidates.head; node;) {
        auto* next = node->next;
        auto* other = static_cast<CUnit*>(node->data);
        CUnitServer* control = ControllerOf(other);
        float sight = static_cast<float>(static_cast<double>(control->moveFactors[3]) * control->stats.valuesB0[0].current);
        float hearing = control->stats.valuesB0[2].current;
        float reach = (sight < hearing || x87::IsNaN(sight) || x87::IsNaN(hearing)) ? hearing : sight;
        double limit = static_cast<double>(reach) * kReachScale;
        double dy = static_cast<double>(other->position.y) - unit->position.y;
        double dx = static_cast<double>(other->position.x) - unit->position.x;
        if (limit * limit < LengthSquared2DUnrounded(dx, dy)) candidates.RemoveAt(node);
        node = next;
    }
    AddPartyTargets(brain, &candidates);
    unit->flags_ |= kChoosingFlag;
    brain->ChooseTarget(best, &candidates, PositionOf(unit), 0, 0);
    unit->flags_ &= ~kChoosingFlag;

    auto* chosen = reinterpret_cast<CEngineObject*>(best[1]);
    if (chosen) {
        if (!DirectlyControlled(brain->owner->unit)) {
            // The other followers that are not fighting stop following.
            AIObjectList party;
            InitList(party);
            CollectParty(brain, &party);
            for (auto* node = party.head; node;) {
                auto* other = static_cast<CUnit*>(node->data);
                node = node->next;
                if (other == unit) continue;
                AIBrain* theirs = BrainOf(other);
                auto* mode = static_cast<PlayerMode*>(theirs->FindMode(kModePlayer));
                if (!mode || theirs->action == AIBrain::kActionAttack || theirs->action == AIBrain::kActionCast) continue;
                mode->state = kStateFollow;
                if (theirs->target) {
                    reinterpret_cast<AIRef*>(&theirs->target)->Reset();
                    theirs->target = nullptr;
                }
                theirs->action = AIBrain::kActionNone;
            }
            party.RemoveAll();
        }
        int now[2] = {-1, 0};
        brain->CurrentAttack(now);
        if (now[1] != best[1] || now[0] != best[0]) Attack(brain, chosen, true);
        if (target) {
            reinterpret_cast<AIRef*>(&target)->Reset();
            target = nullptr;
        }
    }
    candidates.RemoveAll();
}

EI_REPLACE_METHOD(0x005D4170, PlayerMode::UpdateCombat);

void PlayerMode::AddPartyTargets(AIBrain* brain, AIObjectList* targets) {
    AIObjectList nearby;
    InitList(nearby);
    CUnit* unit = brain->owner->unit;
    WorldServer()->Sectors()->FindUnitsInRadius(PositionOf(unit), &nearby,
                               GameLogicSettings().playerCallForHelp);
    for (auto* node = nearby.head; node;) {
        auto* other = static_cast<CUnit*>(node->data);
        node = node->next;
        if (other == unit || other->player != unit->player) continue;
        AIBrain* theirs = BrainOf(other);
        CEngineObject* foe = theirs->target;
        if (!foe || static_cast<CUnit*>(foe)->player == unit->player || theirs->action != AIBrain::kActionAttack ||
            foe->GetClassId() != kClassUnit)
            continue;
        if (targets->Find(foe)) continue;
        targets->AddTail(foe);
    }
    nearby.RemoveAll();
}

EI_REPLACE_METHOD(0x005D3F50, PlayerMode::AddPartyTargets);

void PlayerMode::CollectParty(AIBrain* brain, AIObjectList* party) {
    party->RemoveAll();
    CUnit* unit = brain->owner->unit;
    if (!unit) return;
    party->AddTail(unit);
    CPlayer* player = PlayerOf(unit);
    if (!player) return;
    // The chain of leaders above the unit.
    for (const CUnit* member = unit;;) {
        CEngineObject* above = LeaderOf(member);
        if (!above || party->Find(above)) break;
        party->AddTail(above);
        member = static_cast<const CUnit*>(above);
    }
    // Everyone following someone listed, until nobody is added.
    bool added;
    do {
        added = false;
        for (auto* node = player->units_.First(); node && node->value; node = player->units_.After(node)) {
            CEngineObject* member = node->value;
            CEngineObject* followed = LeaderOf(static_cast<CUnit*>(member));
            if (!party->Find(followed) || party->Find(member)) continue;
            party->AddTail(member);
            added = true;
        }
    } while (added);
}

EI_REPLACE_METHOD(0x005D4600, PlayerMode::CollectParty);

bool PlayerMode::StaysWithParty(AIBrain* brain, bool moving) {
    float slack = moving ? kMovingSlack : kStandingSlack;
    CUnit* unit = brain->owner->unit;
    CPlayer* player = PlayerOf(unit);
    if (!player) return true;
    for (auto* node = player->units_.First(); node && node->value; node = player->units_.After(node)) {
        auto* member = static_cast<CUnit*>(node->value);
        PlayerMode* mode = nullptr;
        for (auto* entry = BrainOf(member)->modes.head; entry; entry = entry->next) {
            if (entry->data->GetType() == kModePlayer) {
                mode = static_cast<PlayerMode*>(entry->data);
                break;
            }
        }
        // The original reads the leader of a unit without a Player mode at address 0x0C (a crash).
        if (!mode || mode->leader != unit) continue;
        double dx = static_cast<double>(member->position.x) - unit->position.x;
        double dy = static_cast<double>(member->position.y) - unit->position.y;
        double dz = static_cast<double>(member->position.z) - unit->position.z;
        double limit = static_cast<double>(GameLogicSettings().followMaxDist) * slack + kOne;
        if (!(limit * limit >= LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz))) return false;
    }
    return true;
}

EI_REPLACE_METHOD(0x005D4860, PlayerMode::StaysWithParty);
