// Combat orders: the setters of the queued order that the AI and the player's commands use in a
// fight (turn, move, attack, cast, stop) and the end of a cast. See combat.h and docs/notes/combat.md.
#include "game/combat/combat.h"

#include "game/ai/ai_map.h"
#include "game/ai/sector_map.h"
#include "game/ai/unit_ai.h"
#include "game/app/singletons.h"
#include "game/combat/combat_internal.h"
#include "engine/core/base_manager.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/players/player.h"
#include "game/players/player_messages.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

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

// EngineObject::prototypeId (not type) of a unit that cannot be tamed (0x32, the value type has for
// heroes; the original compares prototypeId).
constexpr std::uint32_t kUntameable = 0x32;

constexpr std::uint32_t kEmoteKeyBit = AnimationKeyOf(AnimationKeyFlag::Looped);   // FigureAnimation::key: an emote (kind 8)

// The shared tail of the kind 6 setters: the queued order (reset first) becomes a use of `target` with
// `source`, `spot` and `run`, and is planned; reset again when the planner refuses.
void QueueUse(UnitServer* self, MapObject* target, UseSource source, int spot, int run, AckKind shout) {
    UnitAction& order = self->action2;
    order.Reset();
    order.kind = UnitOrderKind::Use;
    order.Retarget(target);
    order.modifier = static_cast<std::uint8_t>(run);
    order.source = static_cast<std::uint32_t>(source);
    order.param = spot;
    if (!self->PlanOrderRoute(&order, shout)) order.Reset();
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
        float loudness = stats.conspicuity[Sense::Hearing].current * kCastNoiseScale;
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

// --- The rest of the setters -------------------------------------------------------------------

// Combat orders, the rest of the setters: movement style, wanted weapon, the skill and use orders, playing an
// animation as an order, dropping everything, and two armour queries. See combat.h and docs/notes/combat.md.

bool UnitServer::FitsMoveMode(MoveMode mode) {
    MoveMode stored = mode == MoveMode::Run ? MoveMode::Walk : mode;
    if (stored == moveMode.mode) return true;
    AIMap* map = &g_serverWorld.aiMap;
    MoveMode saved = unit->moveMode;
    map->sectorMap.Remove(unit);
    unit->moveMode = mode;
    unit->UpdateSizeClass();
    bool fits = map->IsUnitCellPassable(unit);
    unit->moveMode = saved;
    unit->UpdateSizeClass();
    map->sectorMap.Add(unit);
    return fits;
}

void UnitServer::SetMoveStyle(MoveMode style) {
    boredTicks = 0;
    unit->moveMode2 = style;
    if (FitsMoveMode(style)) {
        if (style == MoveMode::Run && stats.MovementState(1) != 0) return;
        if (unit->player) unit->player->Server()->SendUnitSpeech(unit, AckKind::ChangePosition);
    }
    if (style != MoveMode::Walk && style != MoveMode::Run) action.modifier = 0;
}

void UnitServer::SetWantedWeapon(int slot) {
    boredTicks = 0;
    unit->pendingWeapon = slot;
}

void UnitServer::OrderSteal(MapObject* target, int run) {
    if (!target || target->IsDead() != 0 || target == unit) return;
    QueueUse(this, target, UseSource::Steal, 0, run, AckKind::Steal);
}

// Unreached in the shipped game: the interface sends only the use and steal kinds.
void UnitServer::OrderTame(MapObject* target, int run) {
    if (!target || target->IsDead() != 0 || target == unit) return;
    Unit* other = UnitOrNull(target);
    if (!other || other->player == unit->player || other->prototypeId == kUntameable) return;
    QueueUse(this, target, UseSource::Tame, 0, run, AckKind::UseObject);
}

void UnitServer::OrderUse(MapObject* target, int spot, int run) {
    if (!target) return;
    QueueUse(this, target, UseSource::Use, spot, run, AckKind::UseObject);
}

void UnitServer::OrderSearchBody(MapObject* target, int run) {
    if (!target || target->IsDead() == 0 || target == unit) return;
    QueueUse(this, target, UseSource::SearchBody, 0, run, AckKind::Loot);
}

bool UnitServer::TargetBusy(MapObject* target) {
    if (target->flags & MapObjectFlag::Shared) return !g_serverWorld.IsObjectShared(target->id, unit->id);
    AIBrain* ai = &brain;
    for (auto& node : ai->memory.nearby) {
        Unit* other = node;
        if (!other || other->IsDead() != 0) continue;
        auto* controller = other->ServerController();
        const UnitAction& order = controller->action;
        if (order.kind == UnitOrderKind::Use && order.Skill() == UseSource::SearchBody && order.target == target &&
            (controller->state == UnitState::Acting || controller->searchingTarget))
            return true;
    }
    return false;
}

void UnitServer::PlayAnimation(std::string_view name) {
    const FigureAnimation* entry = OwningWorld()->GetMapObjectManager()->FindFigureAnimation(unit->name, name);
    if (!entry) return;
    action2.Reset();
    action2.param = entry->index;
    action2.modifier = 0;
    action2.kind = (entry->key & kEmoteKeyBit) ? UnitOrderKind::Emote : UnitOrderKind::Animation;
}

void UnitServer::StopAction() {
    state = UnitState::Stopped;
    unit->UpdateSizeClass();
    DropEffectObject();
    currentPlan.Reset();   // SetSize(0, -1) instead of the free, like the original
    StandStill();
    action.Reset();
    action2.Reset();
    Idle();
}

bool UnitServer::HasOtherArmour() {
    for (int slot = 3; slot < 7; ++slot)
        if (unit->armor[slot]) return true;
    return false;
}

bool UnitServer::HasMainArmour() {
    for (int slot = 0; slot < 3; ++slot)
        if (unit->armor[slot]) return true;
    return false;
}
