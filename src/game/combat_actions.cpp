// Combat, second part: what happens when an order's animation reaches its moment (attack hits and bow
// missiles, casts, skills, quick items), the spells items cast on hit, item charges, healing, death and the
// AI's reactions to it (witnesses, hostility claims). See combat.h and docs/notes/combat.md.
#include "ai_alarm.h"
#include "combat.h"
#include "map_object.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "base_manager.h"
#include "item.h"
#include "maths.h"
#include "object_diplomacy.h"
#include "party.h"
#include "player_messages.h"
#include "resource_manager.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/x87.h"
#include "world_effect.h"
#include "game_singletons.h"

#include <bit>
#include <cstring>
#include <list>

namespace {
constexpr float kHitNoiseScale = 2.0f;
constexpr float kSkillNoiseScale = 1.5f;
constexpr std::uint32_t kOneBits = 0x3F800000u;                 // 1.0f, compared by its bits
constexpr int kNoiseKind = 0x1A;

constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
// PropagateHostility's squared call radius: the original rounds it to a float through this global.
float g_squaredCallRadius = /*007AF40C*/ {};
constexpr std::uint32_t kSpellOnWeapon = 0x10000;       // CSpell::targetFlags_: cast by the item
constexpr std::uint32_t kSpellNotOnHit = 0x180000;      // fires when not hit
constexpr int kMissileObject = 0x5B;                    // object factory kind of a missile

CDiplomacy* DiplomacyOf(CWorldServer* world) { return &world->diplomacy_; }

AIEventTable* AIEventsOf(CWorldServer* world) { return &world->aiMap_.events; }

float AbsBits(float v) { return std::bit_cast<float>(std::bit_cast<std::uint32_t>(v) & 0x7FFFFFFFu); }

bool ListContains(const std::list<CUnit*>* list, const CUnit* unit) {
    for (auto node = list->begin(); node != list->end(); ++node)
        if (*node == unit) return true;
    return false;
}

// OnDamaged's search of the party (the player's units, walked as the list's begin() and operator++ do): the
// first member that is not `self`; a null member ends it.
CUnit* FirstOtherPartyMember(CPlayer* player, const CUnit* self) {
    const auto& map = player->units_;
    for (auto node = map.begin(); node != map.end(); ++node) {
        CUnit* member = node->second;
        if (!member) return nullptr;
        if (member != self) return member;
    }
    return nullptr;
}

} // namespace

// Player messages: message `code` about `unit`.
void PlayerMessage(CPlayerServer* messages, CUnit* unit, AckKind kind) {
    messages->SendObject0B(unit, kind);
}

void CUnitServer::DropQueuedOrder() {
    action2.kind = kOrderNone;
    if (action2.target) action2.target->Release();
    action2.target = nullptr;
    action2.pointIsUseSpot = 0;
    action2.param = 0;
    action2.modifier = 0;
    action2.kind = kOrderIdle;
    if (action2.target) {
        action2.target->Release();
        action2.target = nullptr;
    }
    action2.modifier = 0;
}

void CUnitServer::FinishAction(int location) {
    switch (action.kind) {
    case kOrderAttack: {
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
    case kOrderCastAt:
    case kOrderCastOn:
        FinishCast();
        break;
    case kOrderUse:
        UseSkill(action.target);
        break;
    case kOrderUseItem: {
        std::uint32_t slot = action.source;
        auto* item = unit->GetQuickItem(slot);
        if (item && item->GetCategory() == kCategoryConsumable) {
            ++item->refCount_;
            unit->RemoveQuickItem(slot);
            g_serverWorld->UseQuickItem(unit);
            item->Release();
        }
        action.kind = kOrderNone;
        if (action.target) action.target->Release();
        action.target = nullptr;
        action.pointIsUseSpot = 0;
        action.param = 0;
        action.modifier = 0;
        if (action2.kind == kOrderNone && action.kind != kOrderIdle) DropQueuedOrder();
        return;
    }
    default:
        return;
    }
    action.Clear();
    action.param = 0;
    action.modifier = 0;
    if (action2.kind == kOrderNone) Idle();
}

void CUnitServer::DeliverAttack(CMapObject* target, DamageInfo* hit) {
    CWorldServer* world = ServerWorld();
    CItemWeapon* weapon = unit->GetWeapon(unit->activeWeapon);
    if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) {
        CBaseManager* factory = world->TimedObjectManager();
        auto* arrow = factory->CreateObjectAs<CEffectArrowFigure>(kMissileObject, -1);
        if (!arrow) return;
        Vector3 position;
        const Vector3* from = unit->EyePoint(&position);
        arrow->localPosition_ = *from;
        arrow->SetTarget(target->id_);
        arrow->fromCaster_ = 1;
        arrow->casterId_ = unit->id_;
        std::memcpy(&arrow->hit_, hit, sizeof(DamageInfo));
        world->AddTimedObject(arrow);
    } else {
        target->ReceiveHit(hit, unit, unit);
    }
    TriggerWeaponSpell(target);
    if (std::bit_cast<std::uint32_t>(hit->backstabFactor) == kOneBits) {
        float loudness = static_cast<float>(static_cast<double>(stats.conspicuity[3].current) * kHitNoiseScale);
        AIEventsOf(world)->Noise(this, loudness, kNoiseKind, nullptr);
    }
}

void CUnitServer::TriggerWeaponSpell(CMapObject* target) {
    auto* weapon = unit->GetWeapon(unit->activeWeapon);
    if (!weapon) return;
    auto* spell = weapon->spell_;
    if (!spell || !(spell->targetFlags_ & kSpellOnWeapon)) return;
    float cost = spell->manaCost_;
    if (cost > weapon->charge_) return;
    UseItemCharge(weapon, cost);
    CUnit* self = unit;
    Vector3 point;
    const Vector3* at = target->AimPoint(&point);
    g_serverWorld->CastSpell(spell, self, &self->localPosition_, target, at, 0);
}

void CUnitServer::TriggerItemSpells(char onHit) {
    for (std::uint32_t slot = 0; slot < 7; ++slot) {
        auto* item = unit->armor[slot];
        if (!item) continue;
        auto* spell = item->spell_;
        if (!spell || !(spell->targetFlags_ & kSpellOnWeapon)) continue;
        float cost = spell->manaCost_;
        if (cost > item->charge_) continue;
        bool fires = (spell->prototypeId_ & 0xFFFF0000u) == kSpellNotOnHit ? onHit == 0 : onHit != 0;
        if (!fires) continue;
        UseItemCharge(item, cost);
        CUnit* self = unit;
        g_serverWorld->CastSpell(spell, self, &self->localPosition_, self, &self->localPosition_, 0);
    }
}

void CUnitServer::UseItemCharge(CItem* item, float cost) {
    if (!item) return;
    CWorldServer* world = ServerWorld();
    item->charge_ = item->charge_ - cost;
    world->AddCounted(item);
    auto* player = unit->player;
    const CItemLink* link = item->ServerLink();
    if (!player || !link) return;
    CResourceManager* items = player->Party()->ItemFactory();
    CItem* copy = FindItem(items, link->value);
    if (!copy) return;
    copy->charge_ = item->charge_;
    player->ItemUsed(copy);
}

BOOL CUnitServer::Heal(float amount) {
    if (!stats.Heal(ServerStatsOf(unit), amount)) return 0;
    stats.Recompute(ServerStatsOf(unit), unit);
    unit->MarkChanged(0);
    unit->MarkChanged(4);
    console::EndLine(*kCombatLog << "Unit " << static_cast<long>(unit->id_) << " was healed");
    return 1;
}

std::uint32_t CUnitServer::HitDirection(CUnit* attacker, int hitKind) {
    if (!attacker) return 0;
    if (hitKind) return 0;
    const CUnit* self = unit;
    float dx = attacker->localPosition_.x - self->localPosition_.x;
    float dy = attacker->localPosition_.y - self->localPosition_.y;
    float dz = attacker->localPosition_.z - self->localPosition_.z;
    const Quaternion* rotation = &self->localRotation_;
    Vector3 axisY;
    rotation->AxisY(&axisY);
    // The unit's Y axis points backwards: "front" is the negated projection on it.
    double front = -DotUnrounded<Sum3::ZYX, double>(axisY.x, axisY.y, axisY.z, dx, dy, dz);
    float frontStored = static_cast<float>(front);
    // The X axis of the rotation, kept on the FPU stack (unrounded).
    AxisUnrounded<double> axisX = AxisXUnrounded<double>(*rotation);
    double side = -DotUnrounded<Sum3::ZYX, double>(axisX.x, axisX.y, axisX.z, dx, dy, dz);
    float sideStored = static_cast<float>(side);
    if (AbsBits(sideStored) >= AbsBits(frontStored)) return sideStored > 0.0f ? 0xC00000 : 0x1000000;
    return frontStored > 0.0f ? 0x800000 : 0x400000;
}

void CUnitServer::WitnessMurder(std::list<CUnit*>* witnesses, CUnit* victim, CUnit* murderer) {
    CWorldServer* world = ServerWorld();
    CDiplomacy* diplomacy = DiplomacyOf(unit->ServerWorld());
    int side = unit->side;
    auto node = witnesses->begin();
    if (node == witnesses->end()) return;
    std::uint32_t bit = 1u << (static_cast<std::uint32_t>(side) & 31);
    do {
        CUnit* other = *node;
        ++node;
        if (!(diplomacy->allied_[other->side] & bit)) continue;
        other->flags_ |= 0x2000000;
        CUnitServer* controller = other->ServerController();
        AIEventsOf(world)->Update(controller, &controller->brain.memory);
        std::list<CUnit*>* watched = &controller->brain.memory.visible;
        if (!ListContains(watched, victim)) continue;
        if (!ListContains(watched, murderer)) {
            auto added = watched->emplace(watched->end());
            if (murderer != *added) {
                if (*added) (*added)->Release();
                *added = nullptr;
                if (murderer) ++murderer->refCount_;
                *added = murderer;
            }
            console::EndLine(*kCombatLog << "Unit " << static_cast<long>(other->id_)
                                       << " has seen murder and is forced to see unit " << static_cast<long>(murderer->id_));
        }
        DiplomacyOf(world)->OnAttack(murderer, other);
    } while (node != witnesses->end());
}

void CUnitServer::PropagateHostility() {
    if (!(unit->flags_ & 0x80000)) return;
    unit->flags_ &= ~0x80000u;
    claimedHostile |= unit->relationsA;
    CWorldServer* world = ServerWorld();
    double radius = brain.callRadius;
    // The squared radius goes through a global and is compared as an int.
    volatile float* limit = &g_squaredCallRadius;
    *limit = static_cast<float>(radius * radius);
    CDiplomacy* diplomacy = DiplomacyOf(world);
    int side = unit->side;
    auto node = brain.memory.nearby.begin();
    while (node != brain.memory.nearby.end()) {
        CUnit* other = *node;
        ++node;
        if (!(diplomacy->allied_[side] & (1u << (static_cast<std::uint32_t>(other->side) & 31)))) continue;
        CUnit* self = unit;
        double dx = static_cast<double>(other->localPosition_.x) - self->localPosition_.x;
        double dy = static_cast<double>(other->localPosition_.y) - self->localPosition_.y;
        float distance = static_cast<float>(LengthSquared2DUnrounded(dy, dx));
        float squaredRadius = *limit;
        if (!(std::bit_cast<std::int32_t>(distance) < std::bit_cast<std::int32_t>(squaredRadius))) continue;
        std::uint32_t mine = self->relationsA;
        CUnitServer* theirController = other->ServerController();
        std::uint32_t theirClaims = theirController->claimedHostile;
        if (mine == theirClaims) continue;
        std::uint32_t toThem = ~theirClaims & mine;
        std::uint32_t toMe = other->relationsA & ~claimedHostile;
        if (toThem) theirController->ClaimHostility(self, toThem);
        if (toMe) ClaimHostility(other, toMe);
    }
}

void CUnitServer::OnDamaged(CUnit* attacker, int hitKind) {
    CWorldServer* world = ServerWorld();
    if (unit->type_ < 0x32 || unit->type_ > 0x36) {
        unit->flags_ |= 1;
        return;
    }
    if (CompanionSlots(ServerObjectOf(unit))->IsDead() == 0) {
        TriggerItemSpells(0);
        if (attacker && attacker->GetClassId() == kClassUnit) DiplomacyOf(world)->OnAttack(attacker, unit);
        brain.SetSeen(attacker);
        unit->flags_ |= 0x80000;
        PropagateHostility();
        std::uint32_t direction = HitDirection(attacker, hitKind);
        if (!keepHitDirection) hitDirection = direction;
        return;
    }

    if (attacker) {
        if (attacker->player) {
            CUnit* killer = attacker->ServerController()->unit;
            if (killer->player)
                PlayerMessage(killer->player->Server(), killer, AckKind::Kill);
        } else if (unit->player) {
            attacker->ServerController()->voice.Raise(kRemarkKill);
        }
        if (attacker->GetClassId() == kClassUnit) {
            CUnitServer* theirs = attacker->ServerController();
            WitnessMurder(&brain.memory.nearby, unit, attacker);
            WitnessMurder(&theirs->brain.memory.nearby, attacker, attacker);
        }
    }
    unit->ClearEffects();
    SetDead(1);
    console::EndLine(*kCombatLog << "Unit " << static_cast<long>(unit->id_) << " damaged to death");

    AnimationSteps animation;
    std::uint32_t direction = HitDirection(attacker, hitKind);
    BuildDeathAnimation(&animation, direction, 1, 0);
    animationHeld = 0;
    frozen = 0;
    PlayAnimationOrder(animation);
    AIEventsOf(world)->ForgetUnit(this);
    if (CPlayer* player = unit->player) {
        if (CUnit* member = FirstOtherPartyMember(player, unit))
            member->ServerController()->brain.memory.SeeCorpse(member, unit);   // `member` sees the dead unit
        if (IsMultiplayerClient() & 0xFF) unit->flags_ |= 0x20000000;
    }
}

void CUnitServer::UseSkill(CMapObject* target) {
    CWorldServer* world = ServerWorld();
    if (action.kind == kOrderUse && action.source == kSourceSearchBody) {
        CUnit* looted = UnitOrNull(target);
        if (!looted) return;
        if (CPlayer* player = unit->player) g_serverWorld->Loot(looted, player);
        g_serverWorld->RemoveObject(target->id_);
        float loudness = static_cast<float>(static_cast<double>(stats.conspicuity[3].current) * kSkillNoiseScale);
        AIEventsOf(world)->Noise(this, loudness, kNoiseKind, nullptr);
        return;
    }
    std::uint32_t skill = action.source;
    if (skill != kSourceSteal && skill != kSourceUse) return;
    CUnit* other = UnitOrNull(target);
    float mine = static_cast<float>(stats.AttributeWithItems(0, unit));
    float theirs = 0;
    if (other) theirs = static_cast<float>(other->ServerController()->stats.AttributeWithItems(0, other));

    *kCombatLog << "Unit" << static_cast<long>(unit->id_) << " is using skill ";
    const char* name;
    switch (action.source) {
    case kSourceUse: name = "science"; break;
    case kSourceSteal: name = "stealing"; break;
    case kSourceTame: name = "tame"; break;
    default: name = "unknown"; break;
    }
    console::EndLine(*kCombatLog << name << " his_skill = " << mine << " target_skill = " << theirs);

    skill = action.source;
    if (skill != kSourceUse && theirs >= mine) {
        DiplomacyOf(world)->OnAttack(unit, other);
        return;
    }
    AckKind message;
    switch (skill) {
    case kSourceUse: {
        LockKey record = {1, 0, 0};
        auto* items = world->ItemFactory();
        CItem* studied = FindItem(items, action.param);
        record = {1, 0, 0};
        if (studied && studied->GetUse().mask != 0) record = studied->GetUse();
        record.level += x87::fistp(mine);
        if (target->ApplyScience(&record, unit)) return;
        if (!unit->player) return;
        message = AckKind::ScienceFailed;
        break;
    }
    case kSourceSteal: {
        if (!other) return;
        if (other->player) return;
        CItem* item = other->TakeRandomItem();
        if (item) {
            console::EndLine(*kCombatLog << "Item has been stolen from");
            if (CPlayer* player = unit->player) {
                std::list<CItem*> list;
                list.push_front(item);
                player->GiveItems(&list, 0);
                list.clear();
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
    case kSourceTame: {
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

void CUnitServer::Idle() {
    if (action.kind == kOrderIdle) return;
    if (action2.kind == kOrderAnimation || action2.kind == kOrderEmote) return;
    DropQueuedOrder();
}

void CUnitServer::SetDead(int dead) {
    auto* world = ServerWorld();
    if (unit->dead == dead) return;
    CSectorMap* units = &world->aiMap_.sectorMap;
    units->Remove(unit);
    unit->dead = dead;
    units->Add(unit);
    if (dead && effectObject) {
        world->RemoveTimedObject(effectObject->id_);
        CEngineObject* old = effectObject;
        effectObject = nullptr;
        if (old) old->Release();
    }
    unit->MarkChanged(7);
}
