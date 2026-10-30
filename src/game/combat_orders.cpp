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

#include "lib/bytes.h"
#include "lib/float_select.h"
#include "lib/rounding.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kCastNoiseScale = 2.0f;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kNoSource = 0xFFFFFFFFu;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr std::uint32_t kNoPointZ = 0xC4A42000u;    // -1313.0f: "no point"
constexpr float kReachHeight = 1.5f;




// The reset of an order: kind 9, target released, pointIsUseSpot / +0x1C / +0x20 cleared.
void ResetOrder(UnitAction* order) {
    order->kind = UnitOrderKind::None;
    order->target.Reset();
    order->pointIsUseSpot = false;
    order->param = 0;
    order->modifier = 0;
}

void ReleaseTarget(UnitAction* order) {
    order->target.Reset();
}

void SetTarget(UnitAction* order, MapObject* target) {
    if (target == order->target) return;
    order->target.Assign(target);
}

void TellParty(Unit* unit, AckKind kind) {
    if (unit->player) PlayerMessage(unit->player->Server(), unit, kind);
}

// The spell checks OrderCastAt and OrderCast share: the spell behind `source`; for a party unit's own spell
// (not an item) its skill requirement (+0x50) against the unit's skill. True when the order may be queued.
bool MayCast(UnitServer* self, std::uint32_t source) {
    Spell* spell;
    if (source & kQuickItemSource) {
        Item* carrier = self->SpellCarrier(source);
        if (!carrier) return !(self->unit->flags & MapObjectFlag::NoCasting);
        spell = carrier->spell;
    } else {
        spell = self->unit->GetSpellObject(source);
    }
    if (spell && self->unit->player && !(source & kQuickItemSource)) {
        int needed = static_cast<int>(spell->skillNeeded);
        float skill = self->stats.SpellSchoolSkill(spell->GetPrototype()->school);
        if (lib::RoundEven(skill) < needed) return false;
    }
    return !(self->unit->flags & MapObjectFlag::NoCasting);
}

} // namespace

void UnitServer::FinishCast() {
    WorldServer* world = ServerWorld();
    std::uint32_t source = action.source;
    Item* carrier = nullptr;
    Spell* spell;
    if (source & kQuickItemSource) {
        auto* item = unit->GetQuickItem(source & ~kQuickItemSource);
        if (!item || item->GetClassId() != ClassId::ItemQuick) return;
        if (item->GetCategory() != QuickItemCategory::Wand && item->GetCategory() != QuickItemCategory::Potion) return;
        carrier = item;
        spell = item->spell;
    } else {
        spell = unit->GetSpellObject(source);
    }
    if (!spell) return;
    Vector3 at;
    if (!action.target) {
        at = action.point;
    } else {
        at = action.target->AimPoint();
    }
    if (carrier) {
        float cost = spell->manaCost;
        if (cost > carrier->charge) {
            action.source = kNoSource;
            return;
        }
        UseItemCharge(carrier, cost);
    } else if (unit->player) {
        float cost = spell->manaCost;
        UnitVitals* vitals = ServerStatsOf(unit);
        float needed = stats.Period20() * cost;
        float maximum;
        maximum = vitals->maxMana;
        float* mana = &vitals->mana;
        float left = -needed + *mana;
        *mana = left;
        // Branch-free clamps: min(left, maximum) (left when unordered), then max(0, that).
        float capped = MinKeepA(left, maximum);
        float result = MaxKeepB(0.0f, capped);
        *mana = result;
        CompanionOwner(ServerObjectOf(unit))->MarkChanged(0);
        unit->MarkChanged(0);
    }
    world->CastSpell(spell, unit, unit->localPosition, action.target, at, 0);
    if (spell->GetPrototype()->castType != 1) {
        float loudness = stats.conspicuity[3].current * kCastNoiseScale;
        world->aiMap.events.Noise(this, loudness, AIBrain::kCallDelay, unit->localPosition);
    }
    action.source = kNoSource;
}

void UnitServer::OrderTurn(const float* direction, std::uint32_t time) {
    ResetOrder(&action2);
    action2.kind = UnitOrderKind::Turn;
    lib::LoadBytes(action2.point, direction);
    ReleaseTarget(&action2);
    action2.modifier = 0;
    action2.param = static_cast<int>(time);
}

void UnitServer::OrderMove(const Vector3& point, int run, int walk, float range) {
    const World* world = WorldContext();
    ResetOrder(&action2);
    action2.kind = UnitOrderKind::Move;
    action2.point = point;
    ReleaseTarget(&action2);
    action2.source = static_cast<std::uint8_t>(walk) != 0 ? 1 : 0;
    action2.point.z = range;
    action2.modifier = static_cast<std::uint8_t>(run);
    action2.param = 0;
    if (!unit->player) return;
    if (std::bit_cast<std::uint32_t>(point.z) == kNoPointZ) return;
    if (world->paused == 0) {
        action2.param = 1;
        return;
    }
    PlanMove(point, walk, run, range);
}

void UnitServer::OrderAttack(MapObject* target, int location, int calledShot, AckKind planArg) {
    if (!target || target == unit) return;
    Unit* victim = UnitOrNull(target);
    if (victim && location < 6) {
        UnitServer* controller = victim->ServerController();
        if (controller->stats.slots[location].kind != 3) location = 6;
    }
    ResetOrder(&action2);
    action2.kind = UnitOrderKind::Attack;
    SetTarget(&action2, target);
    action2.source = static_cast<std::uint32_t>(location);
    action2.modifier = static_cast<std::uint8_t>(calledShot);
    // CanReachTarget inlined on the queued order.
    if (action2.kind == UnitOrderKind::Attack || action2.kind == UnitOrderKind::Use) {
        Unit* other = UnitOrNull(action2.target);
        if (other && other->IsDead() == 0 && other->sizeClass == 0) {
            bool ranged = false;
            if (action2.kind == UnitOrderKind::Attack) {
                ItemWeapon* weapon = unit->GetWeapon(unit->activeWeapon);
                ranged = weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow);
            }
            if (!ranged) {
                float height = std::bit_cast<float>(other->GetPrototype()->height) + other->localPosition.z;
                if (height >= kReachHeight) {
                    TellParty(unit, AckKind::OutOfReach);
                    action2.kind = UnitOrderKind::None;
                    ReleaseTarget(&action2);
                    action2.pointIsUseSpot = false;
                    action2.param = 0;
                    action2.modifier = 0;
                    return;
                }
            }
        }
    }
    if (!PlanOrderRoute(&action2, planArg)) ResetOrder(&action2);
}

void UnitServer::OrderCastAt(std::uint32_t source, const Vector3& point, int run) {
    Vector3 at;
    at = point;
    if (source & kQuickItemSource) {
        std::uint32_t slot = source & ~kQuickItemSource;
        auto* item = unit->GetQuickItem(slot);
        if (item && item->GetClassId() == ClassId::ItemQuick &&
            (item->GetCategory() == QuickItemCategory::Wand || item->GetCategory() == QuickItemCategory::Potion) &&
            item->GetCategory() == QuickItemCategory::Potion) {
            WorldServer* world = ServerWorld();
            auto* scroll = unit->GetQuickItem(slot);
            if (!scroll || !scroll->count) return;
            Item* carrier = SpellCarrier(source);
            if (!carrier || !carrier->spell) return;
            world->CastSpell(carrier->spell, unit, unit->localPosition, nullptr, point, 0);
            world->UseItemUp(unit, scroll);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, AckKind::SpellTooComplex);
        return;
    }
    action2.kind = UnitOrderKind::None;
    ReleaseTarget(&action2);
    action2.pointIsUseSpot = false;
    action2.param = 0;
    action2.modifier = 0;
    action2.kind = UnitOrderKind::CastAt;
    if (action2.target) ReleaseTarget(&action2);
    action2.source = source;
    action2.point = at;
    action2.modifier = static_cast<std::uint8_t>(run);
    if (!PlanOrderRoute(&action2, CanCast(&action2) ? AckKind::Cast : AckKind::None)) ResetOrder(&action2);
}

void UnitServer::OrderCast(std::uint32_t source, MapObject* target, int run) {
    target->AimPoint();    // result unused (sic)
    if (source & kQuickItemSource) {
        Item* carrier = SpellCarrier(source);
        if (carrier && carrier->GetCategory() == QuickItemCategory::Potion) {
            WorldServer* world = ServerWorld();
            auto* scroll = unit->GetQuickItem(source & ~kQuickItemSource);
            if (!scroll || !scroll->count) return;
            Item* spellCarrier = SpellCarrier(source);
            if (!spellCarrier || !spellCarrier->spell) return;
            world->CastSpell(spellCarrier->spell, unit, unit->localPosition, target,
                                   target->localPosition, 0);
            world->UseItemUp(unit, scroll);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, AckKind::SpellTooComplex);
        return;
    }
    ResetOrder(&action2);
    action2.kind = UnitOrderKind::CastOn;
    SetTarget(&action2, target);
    action2.source = source;
    action2.point = {};
    action2.modifier = static_cast<std::uint8_t>(run);
    if (!PlanOrderRoute(&action2, CanCast(&action2) ? AckKind::Cast : AckKind::None)) ResetOrder(&action2);
}

void UnitServer::StopQueued() {
    if (action.kind == UnitOrderKind::Stop) return;
    ResetOrder(&action2);
    action2.kind = UnitOrderKind::Stop;
    ReleaseTarget(&action2);
    action2.modifier = 0;
}
