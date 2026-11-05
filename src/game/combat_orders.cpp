// Combat orders: the setters of the queued order that the AI and the player's commands use in a
// fight (turn, move, attack, cast, stop) and the end of a cast. See combat.h and docs/notes/combat.md.
#include "combat.h"
#include "combat_internal.h"

#include "item.h"
#include "item_db.h"
#include "lib/vecmath.h"
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
constexpr std::uint32_t kNoSource = 0xFFFFFFFFu;
constexpr float kNoPointZ = -1313.0f;    // a move order's "no point"




void TellParty(Unit* unit, AckKind kind) {
    if (unit->player) PlayerMessage(unit->player->Server(), unit, kind);
}

// The spell checks OrderCastAt and OrderCast share: the spell behind `source`; for a party unit's own spell
// (not an item) its skill requirement against the unit's skill. True when the order may be queued.
bool MayCast(UnitServer* self, std::uint32_t source) {
    Spell* spell = self->SpellOfSource(source);
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
        carrier = SpellCarrier(source);
        if (!carrier) return;
        spell = carrier->spell;
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
        UnitVitals* vitals = &unit->Vitals();
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
        unit->companion->owner->MarkChanged(NetField::Vitals);
        unit->MarkChanged(NetField::Vitals);
    }
    world->CastSpell(spell, unit, unit->localPosition, action.target, at, 0);
    if (spell->GetPrototype()->castType != 1) {
        float loudness = stats.conspicuity[kSenseHearing].current * kCastNoiseScale;
        world->aiMap.events.Noise(this, loudness, AIBrain::kCallDelay, unit->localPosition);
    }
    action.source = kNoSource;
}

void UnitServer::OrderTurn(const Vector3& direction, float time) {
    action2.Reset();
    action2.kind = UnitOrderKind::Turn;
    action2.point = direction;
    action2.param = std::bit_cast<int>(time);
}

void UnitServer::OrderMove(const Vector3& point, int run, int walk) {
    const World* world = OwningWorld();
    action2.Reset();
    action2.kind = UnitOrderKind::Move;
    action2.point = point;
    action2.source = static_cast<std::uint8_t>(walk) != 0 ? 1 : 0;
    action2.modifier = static_cast<std::uint8_t>(run);
    if (!unit->player) return;
    if (point.z == kNoPointZ) return;
    if (world->paused == 0) {
        action2.param = 1;
        return;
    }
    PlanMove(point, walk, run);
}

void UnitServer::OrderAttack(MapObject* target, int location, int calledShot, AckKind planArg) {
    if (!target || target == unit) return;
    Unit* victim = UnitOrNull(target);
    if (victim && location < 6) {
        UnitServer* controller = victim->ServerController();
        if (controller->stats.slots[location].kind != 3) location = 6;
    }
    action2.Reset();
    action2.kind = UnitOrderKind::Attack;
    action2.Retarget(target);
    action2.source = static_cast<std::uint32_t>(location);
    action2.modifier = static_cast<std::uint8_t>(calledShot);
    if (TooHighToReach(&action2)) {
        TellParty(unit, AckKind::OutOfReach);
        action2.Reset();
        return;
    }
    if (!PlanOrderRoute(&action2, planArg)) action2.Reset();
}

void UnitServer::OrderCastAt(std::uint32_t source, const Vector3& point, int run) {
    Vector3 at;
    at = point;
    if (source & kQuickItemSource) {
        // A potion acts at once (the original's test names wands too, then wants a potion).
        Item* potion = SpellCarrier(source);
        if (potion && potion->GetCategory() == QuickItemCategory::Potion) {
            WorldServer* world = ServerWorld();
            if (!potion->count || !potion->spell) return;
            world->CastSpell(potion->spell, unit, unit->localPosition, nullptr, point, 0);
            world->UseItemUp(unit, potion);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, AckKind::SpellTooComplex);
        return;
    }
    action2.Reset();
    action2.kind = UnitOrderKind::CastAt;
    action2.source = source;
    action2.point = at;
    action2.modifier = static_cast<std::uint8_t>(run);
    if (!PlanOrderRoute(&action2, CanCast(&action2) ? AckKind::Cast : AckKind::None)) action2.Reset();
}

void UnitServer::OrderCast(std::uint32_t source, MapObject* target, int run) {
    if (source & kQuickItemSource) {
        Item* potion = SpellCarrier(source);
        if (potion && potion->GetCategory() == QuickItemCategory::Potion) {
            WorldServer* world = ServerWorld();
            if (!potion->count || !potion->spell) return;
            world->CastSpell(potion->spell, unit, unit->localPosition, target, target->localPosition, 0);
            world->UseItemUp(unit, potion);
            return;
        }
    }
    if (!MayCast(this, source)) {
        TellParty(unit, AckKind::SpellTooComplex);
        return;
    }
    action2.Reset();
    action2.kind = UnitOrderKind::CastOn;
    action2.Retarget(target);
    action2.source = source;
    action2.point = {};
    action2.modifier = static_cast<std::uint8_t>(run);
    if (!PlanOrderRoute(&action2, CanCast(&action2) ? AckKind::Cast : AckKind::None)) action2.Reset();
}

void UnitServer::StopQueued() {
    if (action.kind == UnitOrderKind::Stop) return;
    action2.Reset();
    action2.kind = UnitOrderKind::Stop;
}
