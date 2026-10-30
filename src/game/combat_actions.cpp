// Combat, second part: what happens when an order's animation reaches its moment (attack hits and bow
// missiles, casts, skills, quick items), the spells items cast on hit, item charges, healing, death and the
// AI's reactions to it (witnesses, hostility claims). See combat.h and docs/notes/combat.md.
#include "ai_alarm.h"
#include "base_manager.h"
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"
#include "game_singletons.h"
#include "item.h"
#include "item_manager.h"
#include "map_object.h"
#include "maths.h"
#include "object_diplomacy.h"
#include "party.h"
#include "player_messages.h"
#include "resource_manager.h"
#include "sector_map.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"

#include "lib/float_select.h"
#include "lib/rounding.h"

#include <bit>
#include <cstring>
#include <vector>

namespace {
constexpr float kHitNoiseScale = 2.0f;
constexpr float kSkillNoiseScale = 1.5f;
constexpr std::uint32_t kOneBits = 0x3F800000u;                 // 1.0f, compared by its bits

constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
// PropagateHostility's squared call radius: the original rounds it to a float through this global.
float g_squaredCallRadius = /*007AF40C*/ {};
constexpr int kMissileObject = 0x5B;                    // object factory kind of a missile

Diplomacy* DiplomacyOf(WorldServer* world) { return &world->diplomacy; }



// OnDamaged's search of the party (the player's units, walked as the list's begin() and operator++ do): the
// first member that is not `self`; a null member ends it.
CUnit* FirstOtherPartyMember(CPlayer* player, const CUnit* self) {
    const auto& map = player->units;
    for (auto& node : map) {
        CUnit* member = node.second;
        if (!member) return nullptr;
        if (member != self) return member;
    }
    return nullptr;
}

} // namespace

// Player messages: message `code` about `unit`.
void PlayerMessage(PlayerServer* messages, CUnit* unit, AckKind kind) {
    messages->SendObject0B(unit, kind);
}

void UnitServer::DropQueuedOrder() {
    action2.kind = UnitOrderKind::None;
    action2.target.Reset();
    action2.pointIsUseSpot = false;
    action2.param = 0;
    action2.modifier = 0;
    action2.kind = UnitOrderKind::Idle;
    action2.target.Reset();
    action2.modifier = 0;
}

void UnitServer::FinishAction(int location) {
    switch (action.kind) {
    case UnitOrderKind::Attack: {
        DeliverAttack(action.target, &pendingHit);
        CUnit* target = UnitOrNull(action.target);
        if (!target) return;
        if (location >= 6) return;
        // The kind of the target's body slot `location`: 3 means it can still be aimed at (OrderAttack aims at "all"
        // otherwise); when it is no longer 3 the order aimed at it now aims at "all" (6).
        if (target->ServerController()->stats.slots[location].kind == 3) return;
        if (location != static_cast<int>(action.source)) return;
        action.source = 6;
        return;
    }
    case UnitOrderKind::CastAt:
    case UnitOrderKind::CastOn:
        FinishCast();
        break;
    case UnitOrderKind::Use:
        UseSkill(action.target);
        break;
    case UnitOrderKind::UseItem: {
        std::uint32_t slot = action.source;
        auto* item = unit->GetQuickItem(slot);
        if (item && item->GetCategory() == QuickItemCategory::Consumable) {
            item->AddRef();
            unit->RemoveQuickItem(slot);
            g_serverWorld.UseQuickItem(unit);
            item->Release();
        }
        action.kind = UnitOrderKind::None;
        action.target.Reset();
        action.pointIsUseSpot = false;
        action.param = 0;
        action.modifier = 0;
        if (action2.kind == UnitOrderKind::None && action.kind != UnitOrderKind::Idle) DropQueuedOrder();
        return;
    }
    default:
        return;
    }
    action.Clear();
    action.param = 0;
    action.modifier = 0;
    if (action2.kind == UnitOrderKind::None) Idle();
}

void UnitServer::DeliverAttack(CMapObject* target, DamageInfo* hit) {
    WorldServer* world = ServerWorld();
    ItemWeapon* weapon = unit->GetWeapon(unit->activeWeapon);
    if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) {
        BaseManager* factory = world->TimedObjectManager();
        auto* arrow = factory->CreateObjectAs<EffectArrowFigure>(kMissileObject, -1);
        if (!arrow) return;
        Vector3 position;
        const Vector3* from = unit->EyePoint(&position);
        arrow->localPosition = *from;
        arrow->SetTarget(target->id);
        arrow->fromCaster = 1;
        arrow->casterId = unit->id;
        std::memcpy(&arrow->hit, hit, sizeof(DamageInfo));
        world->AddTimedObject(arrow);
    } else {
        target->ReceiveHit(hit, unit, unit);
    }
    TriggerWeaponSpell(target);
    if (std::bit_cast<std::uint32_t>(hit->backstabFactor) == kOneBits) {
        float loudness = stats.conspicuity[3].current * kHitNoiseScale;
        world->aiMap.events.Noise(this, loudness, AIBrain::kCallDelay, unit->localPosition);
    }
}

void UnitServer::TriggerWeaponSpell(CMapObject* target) {
    auto* weapon = unit->GetWeapon(unit->activeWeapon);
    if (!weapon) return;
    auto* spell = weapon->spell.Get();
    if (!spell || !(spell->targetFlags & SpellTargetFlag::ItemTriggered)) return;
    float cost = spell->manaCost;
    if (cost > weapon->charge) return;
    UseItemCharge(weapon, cost);
    CUnit* self = unit;
    Vector3 point;
    const Vector3* at = target->AimPoint(&point);
    g_serverWorld.CastSpell(spell, self, self->localPosition, target, *at, 0);
}

void UnitServer::TriggerItemSpells(char onHit) {
    for (std::uint32_t slot = 0; slot < 7; ++slot) {
        auto* item = unit->armor[slot];
        if (!item) continue;
        auto* spell = item->spell.Get();
        if (!spell || !(spell->targetFlags & SpellTargetFlag::ItemTriggered)) continue;
        float cost = spell->manaCost;
        if (cost > item->charge) continue;
        bool fires = spell->Type() == SpellType::Healing ? onHit == 0 : onHit != 0;   // healing: when not hit
        if (!fires) continue;
        UseItemCharge(item, cost);
        CUnit* self = unit;
        g_serverWorld.CastSpell(spell, self, self->localPosition, self, self->localPosition, 0);
    }
}

void UnitServer::UseItemCharge(Item* item, float cost) {
    if (!item) return;
    WorldServer* world = ServerWorld();
    item->charge = item->charge - cost;
    world->AddCounted(item);
    auto* player = unit->player;
    const ItemLink* link = item->ServerLink();
    if (!player || !link) return;
    ResourceManager* items = player->GetParty()->ItemFactory();
    Item* copy = FindItem(items, link->value);
    if (!copy) return;
    copy->charge = item->charge;
    player->ItemUsed(copy);
}

bool UnitServer::Heal(float amount) {
    stats.Heal(ServerStatsOf(unit), amount);
    stats.Recompute(ServerStatsOf(unit), unit);
    unit->MarkChanged(0);
    unit->MarkChanged(4);
    console::EndLine(kCombatLog << "Unit " << static_cast<long>(unit->id) << " was healed");
    return true;
}

BlowSide UnitServer::HitDirection(CUnit* attacker, int hitKind) {
    if (!attacker) return BlowSide::None;
    if (hitKind) return BlowSide::None;
    const CUnit* self = unit;
    float dx = attacker->localPosition.x - self->localPosition.x;
    float dy = attacker->localPosition.y - self->localPosition.y;
    float dz = attacker->localPosition.z - self->localPosition.z;
    const Quaternion* rotation = &self->localRotation;
    Vector3 axisY;
    rotation->AxisY(&axisY);
    // The unit's Y axis points backwards: "front" is the negated projection on it.
    float front = -DotUnrounded<Sum3::ZYX>(axisY.x, axisY.y, axisY.z, dx, dy, dz);
    // The X axis of the rotation, computed in place (Quaternion::AxisX's maths in the original's order).
    AxisUnrounded<float> axisX = AxisXUnrounded<float>(*rotation);
    float side = -DotUnrounded<Sum3::ZYX>(axisX.x, axisX.y, axisX.z, dx, dy, dz);
    if (AbsBits(side) >= AbsBits(front)) return side > 0.0f ? BlowSide::MinusX : BlowSide::PlusX;
    return front > 0.0f ? BlowSide::Front : BlowSide::Back;
}

void UnitServer::WitnessMurder(const HeldUnits* witnesses, CUnit* victim, CUnit* murderer) {
    WorldServer* world = ServerWorld();
    Diplomacy* diplomacy = DiplomacyOf(unit->ServerWorld());
    int side = unit->side;
    auto node = witnesses->begin();
    if (node == witnesses->end()) return;
    do {
        CUnit* other = *node;
        ++node;
        if (!diplomacy->IsAllied(other->side, side)) continue;
        other->flags |= MapObjectFlag::SeeAll;
        UnitServer* controller = other->ServerController();
        world->aiMap.events.Update(controller, &controller->brain.memory);
        HeldUnits& watched = controller->brain.memory.visible;
        if (!lib::Contains(watched, victim)) continue;
        if (!lib::Contains(watched, murderer)) {
            watched.push_back(murderer);
            console::EndLine(kCombatLog << "Unit " << static_cast<long>(other->id)
                                       << " has seen murder and is forced to see unit " << static_cast<long>(murderer->id));
        }
        DiplomacyOf(world)->OnAttack(murderer, other);
    } while (node != witnesses->end());
}

void UnitServer::PropagateHostility() {
    if (!(unit->flags & MapObjectFlag::RelationsChanged)) return;
    unit->flags &= ~MapObjectFlag::RelationsChanged;
    claimedHostile |= unit->relationsA;
    WorldServer* world = ServerWorld();
    float radius = brain.callRadius;
    // The squared radius goes through a global and is compared as an int.
    volatile float* limit = &g_squaredCallRadius;
    *limit = radius * radius;
    Diplomacy* diplomacy = DiplomacyOf(world);
    int side = unit->side;
    auto node = brain.memory.nearby.begin();
    while (node != brain.memory.nearby.end()) {
        CUnit* other = *node;
        ++node;
        if (!diplomacy->IsAllied(side, other->side)) continue;
        CUnit* self = unit;
        float dx = other->localPosition.x - self->localPosition.x;
        float dy = other->localPosition.y - self->localPosition.y;
        float distance = LengthSquared2DUnrounded(dy, dx);
        float squaredRadius = *limit;
        if (!(std::bit_cast<std::int32_t>(distance) < std::bit_cast<std::int32_t>(squaredRadius))) continue;
        std::uint32_t mine = self->relationsA;
        UnitServer* theirController = other->ServerController();
        std::uint32_t theirClaims = theirController->claimedHostile;
        if (mine == theirClaims) continue;
        std::uint32_t toThem = ~theirClaims & mine;
        std::uint32_t toMe = other->relationsA & ~claimedHostile;
        if (toThem) theirController->ClaimHostility(self, toThem);
        if (toMe) ClaimHostility(other, toMe);
    }
}

void UnitServer::OnDamaged(CUnit* attacker, int hitKind) {
    WorldServer* world = ServerWorld();
    if (unit->MapType() < MapObjectType::Hero || unit->MapType() > MapObjectType::Building) {
        unit->flags |= MapObjectFlag::Removed;
        return;
    }
    if (CompanionSlots(ServerObjectOf(unit))->IsDead() == 0) {
        TriggerItemSpells(0);
        if (attacker && attacker->GetClassId() == ClassId::Unit) DiplomacyOf(world)->OnAttack(attacker, unit);
        brain.SetSeen(attacker);
        unit->flags |= MapObjectFlag::RelationsChanged;
        PropagateHostility();
        BlowSide direction = HitDirection(attacker, hitKind);
        if (!keepHitDirection) hitDirection = direction;
        return;
    }

    if (attacker) {
        if (attacker->player) {
            CUnit* killer = attacker->ServerController()->unit;
            if (killer->player)
                PlayerMessage(killer->player->Server(), killer, AckKind::Kill);
        } else if (unit->player) {
            attacker->ServerController()->voice.Raise(AIRemark::Kill);
        }
        if (attacker->GetClassId() == ClassId::Unit) {
            UnitServer* theirs = attacker->ServerController();
            WitnessMurder(&brain.memory.nearby, unit, attacker);
            WitnessMurder(&theirs->brain.memory.nearby, attacker, attacker);
        }
    }
    unit->ClearEffects();
    SetDead(1);
    console::EndLine(kCombatLog << "Unit " << static_cast<long>(unit->id) << " damaged to death");

    AnimationSteps animation;
    BlowSide direction = HitDirection(attacker, hitKind);
    BuildDeathAnimation(&animation, direction, 1, 0);
    animationHeld = 0;
    frozen = 0;
    PlayAnimationOrder(animation);
    world->aiMap.events.ForgetUnit(this);
    if (CPlayer* player = unit->player) {
        if (CUnit* member = FirstOtherPartyMember(player, unit))
            member->ServerController()->brain.memory.SeeCorpse(member, unit);   // `member` sees the dead unit
        if (IsMultiplayerClient()) unit->flags |= MapObjectFlag::Shared;
    }
}

void UnitServer::UseSkill(CMapObject* target) {
    WorldServer* world = ServerWorld();
    if (action.kind == UnitOrderKind::Use && action.Skill() == UseSource::SearchBody) {
        CUnit* looted = UnitOrNull(target);
        if (!looted) return;
        if (CPlayer* player = unit->player) g_serverWorld.Loot(looted, player);
        g_serverWorld.RemoveObject(target->id);
        float loudness = stats.conspicuity[3].current * kSkillNoiseScale;
        world->aiMap.events.Noise(this, loudness, AIBrain::kCallDelay, unit->localPosition);
        return;
    }
    UseSource skill = action.Skill();
    if (skill != UseSource::Steal && skill != UseSource::Use) return;
    CUnit* other = UnitOrNull(target);
    float mine = stats.AttributeWithItems(0, unit);
    float theirs = 0;
    if (other) theirs = other->ServerController()->stats.AttributeWithItems(0, other);

    kCombatLog << "Unit" << static_cast<long>(unit->id) << " is using skill ";
    const char* name;
    switch (skill) {
    case UseSource::Use: name = "science"; break;
    case UseSource::Steal: name = "stealing"; break;
    case UseSource::Tame: name = "tame"; break;
    default: name = "unknown"; break;
    }
    console::EndLine(kCombatLog << name << " his_skill = " << mine << " target_skill = " << theirs);

    skill = action.Skill();
    if (skill != UseSource::Use && theirs >= mine) {
        DiplomacyOf(world)->OnAttack(unit, other);
        return;
    }
    AckKind message;
    switch (skill) {
    case UseSource::Use: {
        LockKey record = {1, 0, 0};
        auto* items = world->ItemFactory();
        Item* studied = FindItem(items, action.param);
        record = {1, 0, 0};
        if (studied && studied->GetUse().mask != 0) record = studied->GetUse();
        record.level += lib::RoundEven(mine);
        if (target->ApplyScience(&record, unit)) return;
        if (!unit->player) return;
        message = AckKind::ScienceFailed;
        break;
    }
    case UseSource::Steal: {
        if (!other) return;
        if (other->player) return;
        Item* item = other->TakeRandomItem();
        if (item) {
            console::EndLine(kCombatLog << "Item has been stolen from");
            if (CPlayer* player = unit->player) {
                std::vector<Item*> list{item};
                player->GiveItems(&list, 0);
            }
            item->Release();
            return;
        }
        CPlayer* player = unit->player;
        if (player && world->Loot(other, player)) return;
        if (!unit->player) return;
        message = AckKind::StealFailed;
        break;
    }
    case UseSource::Tame: {
        // Not reached: skill 2 returned above (sic).
        if (!other) return;
        if (other->ServerController()->TryTame(unit, mine) != 0) return;
        if (!unit->player) return;
        message = AckKind::TameFailed;
        break;
    }
    default:
        return;
    }
    CUnit* self = unit;
    PlayerMessage(self->player->Server(), self, message);
}

void UnitServer::Idle() {
    if (action.kind == UnitOrderKind::Idle) return;
    if (action2.kind == UnitOrderKind::Animation || action2.kind == UnitOrderKind::Emote) return;
    DropQueuedOrder();
}

void UnitServer::SetDead(int dead) {
    auto* world = ServerWorld();
    if (unit->dead == dead) return;
    SectorMap* units = &world->aiMap.sectorMap;
    units->Remove(unit);
    unit->dead = dead;
    units->Add(unit);
    if (dead && effectObject) {
        world->RemoveTimedObject(effectObject->id);
        effectObject.Reset();
    }
    unit->MarkChanged(7);
}
