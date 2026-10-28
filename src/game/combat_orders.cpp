// Combat orders: the setters of the queued order (+0x1B4) that the AI and the player's commands use in a
// fight (turn, move, attack, cast, stop) and the end of a cast. See combat.h and docs/notes/combat.md.
#include "combat.h"
#include "combat_internal.h"

#include "item.h"
#include "item_db.h"
#include "maths.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/bytes.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kCastNoiseScale = 2.0f;
constexpr int kNoiseKind = 0x1A;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kNoSource = 0xFFFFFFFFu;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr std::uint32_t kNoPointZ = 0xC4A42000u;    // -1313.0f: "no point"
constexpr std::uint32_t kFlagSpellBlocked = 0x10000000;
constexpr float kReachHeight = 1.5f;


AIEventTable* AIEventsOf(CWorldServer* world) { return &world->aiMap_.events; }

const itemdb::SpellRecord* SpellPrototype(CSpell* spell) {
    return spell->GetPrototype();
}
const UnitPrototypeRecord* UnitPrototypeOf(CUnit* unit) {
    return unit->GetPrototype();
}

// The reset of an order: kind 9, target released, pointIsUseSpot / +0x1C / +0x20 cleared.
void ResetOrder(UnitAction* order) {
    order->kind = kOrderNone;
    if (order->target) order->target->Release();
    order->target = nullptr;
    order->pointIsUseSpot = 0;
    order->param = 0;
    order->modifier = 0;
}

void ReleaseTarget(UnitAction* order) {
    if (order->target) order->target->Release();
    order->target = nullptr;
}

void SetTarget(UnitAction* order, CMapObject* target) {
    if (target == order->target) return;
    ReleaseTarget(order);
    if (target) ++target->refCount_;
    order->target = target;
}

void TellParty(CUnit* unit, AckKind kind) {
    if (unit->player) PlayerMessage(unit->player->Server(), unit, kind);
}

// The spell checks OrderCastAt and OrderCast share: the spell behind `source`; for a party unit's own spell
// (not an item) its skill requirement (+0x50) against the unit's skill. True when the order may be queued.
bool MayCast(CUnitServer* self, std::uint32_t source) {
    CSpell* spell;
    if (source & kQuickItemSource) {
        CItem* carrier = self->SpellCarrier(source);
        if (!carrier) return !(self->unit->flags_ & kFlagSpellBlocked);
        spell = carrier->spell_;
    } else {
        spell = self->unit->GetSpellObject(source);
    }
    if (spell && self->unit->player && !(source & kQuickItemSource)) {
        int needed = static_cast<int>(spell->skillNeeded_);
        float skill = static_cast<float>(
            self->stats.SpellSchoolSkill(SpellPrototype(spell)->school));
        if (x87::fistp(skill) < needed) return false;
    }
    return !(self->unit->flags_ & kFlagSpellBlocked);
}

} // namespace

void CUnitServer::FinishCast() {
    CWorldServer* world = ServerWorld();
    std::uint32_t source = action.source;
    CItem* carrier = nullptr;
    CSpell* spell;
    if (source & kQuickItemSource) {
        auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
        if (!item || item->GetClassId() != ClassId::ItemQuick) return;
        if (item->GetCategory() != kCategoryWand && item->GetCategory() != kCategoryPotion) return;
        carrier = item;
        spell = item->spell_;
    } else {
        spell = unit->GetSpellObject(source);
    }
    if (!spell) return;
    Vector3 at;
    if (!action.target) {
        at = action.point;
    } else {
        Vector3 point;
        at = *action.target->AimPoint(&point);
    }
    if (carrier) {
        float cost = spell->manaCost_;
        if (cost > carrier->charge_) {
            action.source = kNoSource;
            return;
        }
        UseItemCharge(carrier, cost);
    } else if (unit->player) {
        float cost = spell->manaCost_;
        UnitVitals* vitals = ServerStatsOf(unit);
        double needed = static_cast<double>(stats.Period20()) * cost;
        float maximum;
        maximum = vitals->maxMana;
        float* mana = &vitals->mana;
        float left = static_cast<float>(-needed + *mana);
        *mana = left;
        // Branch-free clamps: min(left, maximum) (left when unordered), then max(0, that).
        float capped = left >= maximum ? maximum : left;
        float result = capped <= 0.0f ? 0.0f : capped;
        *mana = result;
        CompanionOwner(ServerObjectOf(unit))->MarkChanged(0);
        unit->MarkChanged(0);
    }
    world->CastSpell(spell, unit, &unit->localPosition_, action.target, &at, 0);
    if (SpellPrototype(spell)->castType != 1) {
        float loudness = static_cast<float>(static_cast<double>(stats.conspicuity[3].current) * kCastNoiseScale);
        AIEventsOf(world)->Noise(this, loudness, kNoiseKind, nullptr);
    }
    action.source = kNoSource;
}

void CUnitServer::OrderTurn(const float* direction, std::uint32_t time) {
    ResetOrder(&action2);
    action2.kind = kOrderTurn;
    lib::LoadBytes(action2.point, direction);
    ReleaseTarget(&action2);
    action2.modifier = 0;
    action2.param = static_cast<int>(time);
}

void CUnitServer::OrderMove(const Vector3* point, int run, int walk, float range) {
    const CWorld* world = WorldContext();
    ResetOrder(&action2);
    action2.kind = kOrderMove;
    action2.point = *point;
    ReleaseTarget(&action2);
    action2.source = static_cast<std::uint8_t>(walk) != 0 ? 1 : 0;
    action2.point.z = range;
    action2.modifier = static_cast<std::uint8_t>(run);
    action2.param = 0;
    if (!unit->player) return;
    if (std::bit_cast<std::uint32_t>(point->z) == kNoPointZ) return;
    if (world->paused_ == 0) {
        action2.param = 1;
        return;
    }
    PlanMove(point, walk, run, range);
}

void CUnitServer::OrderAttack(CMapObject* target, int location, int calledShot, AckKind planArg) {
    if (!target || target == unit) return;
    CUnit* victim = UnitOrNull(target);
    if (victim && location < 6) {
        CUnitServer* controller = victim->ServerController();
        if (controller->stats.slots[location].kind != 3) location = 6;
    }
    ResetOrder(&action2);
    action2.kind = kOrderAttack;
    SetTarget(&action2, target);
    action2.source = static_cast<std::uint32_t>(location);
    action2.modifier = static_cast<std::uint8_t>(calledShot);
    // CanReachTarget inlined on the queued order.
    if (action2.kind == kOrderAttack || action2.kind == kOrderUse) {
        CUnit* other = UnitOrNull(action2.target);
        if (other && other->IsDead() == 0 && other->sizeClass == 0) {
            bool ranged = false;
            if (action2.kind == kOrderAttack) {
                CItemWeapon* weapon = unit->GetWeapon(unit->activeWeapon);
                ranged = weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow);
            }
            if (!ranged) {
                double height =
                    static_cast<double>(std::bit_cast<float>(UnitPrototypeOf(other)->height)) + other->localPosition_.z;
                if (height >= kReachHeight) {
                    TellParty(unit, AckKind::OutOfReach);
                    action2.kind = kOrderNone;
                    ReleaseTarget(&action2);
                    action2.pointIsUseSpot = 0;
                    action2.param = 0;
                    action2.modifier = 0;
                    return;
                }
            }
        }
    }
    if (!PlanOrderRoute(&action2, planArg)) ResetOrder(&action2);
}

void CUnitServer::OrderCastAt(std::uint32_t source, const Vector3* point, int run) {
    Vector3 at;
    at = *point;
    if (source & kQuickItemSource) {
        std::uint32_t slot = source & ~kQuickItemSource;
        auto* item = unit->GetQuickItem(slot);
        if (item && item->GetClassId() == ClassId::ItemQuick &&
            (item->GetCategory() == kCategoryWand || item->GetCategory() == kCategoryPotion) &&
            item->GetCategory() == kCategoryPotion) {
            CWorldServer* world = ServerWorld();
            auto* scroll = unit->GetQuickItem(slot);
            if (!scroll || !scroll->count_) return;
            CItem* carrier = SpellCarrier(source);
            if (!carrier || !carrier->spell_) return;
            world->CastSpell(carrier->spell_, unit, &unit->localPosition_, nullptr, point, 0);
            world->UseItemUp(unit, scroll);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, AckKind::SpellTooComplex);
        return;
    }
    action2.kind = kOrderNone;
    ReleaseTarget(&action2);
    action2.pointIsUseSpot = 0;
    action2.param = 0;
    action2.modifier = 0;
    action2.kind = kOrderCastAt;
    if (action2.target) ReleaseTarget(&action2);
    action2.source = source;
    action2.point = at;
    action2.modifier = static_cast<std::uint8_t>(run);
    if (!PlanOrderRoute(&action2, CanCast(&action2) ? AckKind::Cast : AckKind::None)) ResetOrder(&action2);
}

void CUnitServer::OrderCast(std::uint32_t source, CMapObject* target, int run) {
    Vector3 aim;
    target->AimPoint(&aim);    // result unused (sic)
    if (source & kQuickItemSource) {
        CItem* carrier = SpellCarrier(source);
        if (carrier && carrier->GetCategory() == kCategoryPotion) {
            CWorldServer* world = ServerWorld();
            auto* scroll = unit->GetQuickItem(source & ~kQuickItemSource);
            if (!scroll || !scroll->count_) return;
            CItem* spellCarrier = SpellCarrier(source);
            if (!spellCarrier || !spellCarrier->spell_) return;
            world->CastSpell(spellCarrier->spell_, unit, &unit->localPosition_, target,
                                   &target->localPosition_, 0);
            world->UseItemUp(unit, scroll);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, AckKind::SpellTooComplex);
        return;
    }
    ResetOrder(&action2);
    action2.kind = kOrderCastOn;
    SetTarget(&action2, target);
    action2.source = source;
    action2.point = {};
    action2.modifier = static_cast<std::uint8_t>(run);
    if (!PlanOrderRoute(&action2, CanCast(&action2) ? AckKind::Cast : AckKind::None)) ResetOrder(&action2);
}

void CUnitServer::StopQueued() {
    if (action.kind == kOrderStop) return;
    ResetOrder(&action2);
    action2.kind = kOrderStop;
    ReleaseTarget(&action2);
    action2.modifier = 0;
}
