// Combat orders, the rest of the setters: movement style, wanted weapon, the skill and use orders, playing an
// animation as an order, dropping everything, and two armour queries. See combat.h and docs/notes/combat.md.
#include "ai_map.h"
#include "base_manager.h"
#include "combat.h"
#include "game_singletons.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "player_messages.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {
constexpr int kStyleWalk = 2;
constexpr int kStyleRun = 3;
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

bool UnitServer::FitsMoveMode(int mode) {
    int stored = mode == kStyleRun ? kStyleWalk : mode;
    if (stored == moveMode.mode) return true;
    AIMap* map = &g_serverWorld.aiMap;
    int saved = unit->moveMode;
    map->sectorMap.Remove(unit);
    unit->moveMode = mode;
    unit->UpdateSizeClass();
    bool fits = map->IsUnitCellPassable(unit);
    unit->moveMode = saved;
    unit->UpdateSizeClass();
    map->sectorMap.Add(unit);
    return fits;
}

void UnitServer::SetMoveStyle(int style) {
    boredTicks = 0;
    unit->moveMode2 = style;
    if (FitsMoveMode(style)) {
        if (style == kStyleRun && stats.MovementState(1) != 0) return;
        if (unit->player) unit->player->Server()->SendObject0B(unit, AckKind::ChangePosition);
    }
    if (style != kStyleWalk && style != kStyleRun) action.modifier = 0;
}

void UnitServer::SetWantedWeapon(int slot) {
    boredTicks = 0;
    unit->pendingWeapon = slot;
}

void UnitServer::OrderSteal(MapObject* target, int run) {
    if (!target || target->IsDead() != 0 || target == unit) return;
    QueueUse(this, target, UseSource::Steal, 0, run, AckKind::Steal);
}

void UnitServer::OrderTame(MapObject* target, int run) {
    if (!target || target->IsDead() != 0 || target == unit) return;
    auto* other = static_cast<Unit*>(target);
    if (other->player == unit->player || other->prototypeId == kUntameable) return;
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

void UnitServer::PlayAnimation(const char* name) {
    const FigureAnimation* entry;
    {
        std::string figure = unit->name;
        auto* world = WorldContext();
        entry = (world->GetMapObjectManager())->FindFigureAnimation(figure, name);
    }
    if (!entry) return;
    action2.Reset();
    // The entry's index byte is read together with the three bytes after it (one 4-byte read).
    int index;
    std::memcpy(&index, &entry->index, sizeof(index));
    action2.param = index;
    action2.modifier = 0;
    action2.kind = (entry->key & kEmoteKeyBit) ? UnitOrderKind::Emote : UnitOrderKind::Animation;
}

void UnitServer::StopAction() {
    state = UnitState::Stopped;
    unit->UpdateSizeClass();
    if (effectObject) {
        g_serverWorld.RemoveTimedObject(effectObject->id);
        effectObject.Reset();
    }
    OrderPlanReset(&currentPlan);   // SetSize(0, -1) instead of the free, like the original
    StandStill();
    action.Reset();
    action2.Reset();
    // Idle, inlined: the current order is never of kind 0 here, the queued one is 9.
    if (action.kind != UnitOrderKind::Idle && action2.kind != UnitOrderKind::Animation && action2.kind != UnitOrderKind::Emote) DropQueuedOrder();
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
