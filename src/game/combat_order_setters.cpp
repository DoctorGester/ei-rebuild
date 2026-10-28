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
constexpr int kStateStopped = 6;        // CUnitServer::state after StopAction
constexpr int kStyleWalk = 2;
constexpr int kStyleRun = 3;
// CEngineObject::prototypeId_ (not type_) of a unit that cannot be tamed (0x32, the value type_ has for
// heroes; the original compares prototypeId_).
constexpr std::uint32_t kUntameable = 0x32;
constexpr int kMessageMoveStyle = 9;    // CPlayerServer::SendObject0B code

// Sources of a kind 6 order (CUnitServer::UseSkill reads them).

// The planner's shout codes per order (its second argument).
constexpr int kShoutSteal = 6;
constexpr int kShoutTame = 5;
constexpr int kShoutUse = 5;
constexpr int kShoutSearchBody = 4;

constexpr std::uint32_t kEmoteKeyBit = 0x80000000u;   // FigureAnimation::key: an emote (kind 8)
constexpr std::uint32_t kFlagShared = 0x20000000u;    // CEngineObject::flags_: on the world's shared list
constexpr int kStateActing = 5;                       // CUnitServer::state

CPlayerServer* PlayerConnection(CUnit* unit) { return unit->player->Server(); }

// The shared tail of the kind 6 setters: the queued order (reset first) becomes a use of `target` with
// `source`, `spot` and `run`, and is planned; reset again when the planner refuses.
void QueueUse(CUnitServer* self, CMapObject* target, std::uint32_t source, int spot, int run, int shout) {
    UnitAction& order = self->action2;
    order.Reset();
    order.kind = kOrderUse;
    order.Retarget(target);
    order.modifier = static_cast<std::uint8_t>(run);
    order.source = source;
    order.param = spot;
    if (!self->PlanOrderRoute(&order, shout)) order.Reset();
}
} // namespace

bool CUnitServer::FitsMoveMode(int mode) {
    int stored = mode == kStyleRun ? kStyleWalk : mode;
    if (stored == moveMode.mode) return true;
    CAIMap* map = &g_serverWorld->aiMap_;
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

void CUnitServer::SetMoveStyle(int style) {
    boredTicks = 0;
    unit->moveMode2 = style;
    if (FitsMoveMode(style)) {
        if (style == kStyleRun && stats.MovementState(1) != 0) return;
        if (unit->player) PlayerConnection(unit)->SendObject0B(unit, kMessageMoveStyle);
    }
    if (style != kStyleWalk && style != kStyleRun) action.modifier = 0;
}

void CUnitServer::SetWantedWeapon(int slot) {
    boredTicks = 0;
    unit->pendingWeapon = slot;
}

void CUnitServer::OrderSteal(CMapObject* target, int run) {
    if (!target || target->IsDead() != 0 || target == unit) return;
    QueueUse(this, target, kSourceSteal, 0, run, kShoutSteal);
}

void CUnitServer::OrderTame(CMapObject* target, int run) {
    if (!target || target->IsDead() != 0 || target == unit) return;
    auto* other = static_cast<CUnit*>(target);
    if (other->player == unit->player || other->prototypeId_ == kUntameable) return;
    QueueUse(this, target, kSourceTame, 0, run, kShoutTame);
}

void CUnitServer::OrderUse(CMapObject* target, int spot, int run) {
    if (!target) return;
    QueueUse(this, target, kSourceUse, spot, run, kShoutUse);
}

void CUnitServer::OrderSearchBody(CMapObject* target, int run) {
    if (!target || target->IsDead() == 0 || target == unit) return;
    QueueUse(this, target, kSourceSearchBody, 0, run, kShoutSearchBody);
}

bool CUnitServer::TargetBusy(CMapObject* target) {
    if (target->flags_ & kFlagShared) return !g_serverWorld->IsObjectShared(target->id_, unit->id_);
    AIBrain* ai = &brain;
    for (auto& node : ai->memory.nearby) {
        auto* other = node;
        if (!other || other->IsDead() != 0) continue;
        auto* controller = other->ServerController();
        const UnitAction& order = controller->action;
        if (order.kind == kOrderUse && order.source == kSourceSearchBody && order.target == target &&
            (controller->state == kStateActing || controller->searchingTarget))
            return true;
    }
    return false;
}

void CUnitServer::PlayAnimation(const char* name) {
    const FigureAnimation* entry;
    {
        std::string figure = unit->name;
        auto* world = WorldContext();
        entry = (world->MapObjectManager())->FindFigureAnimation(figure, name);
    }
    if (!entry) return;
    action2.Reset();
    // The entry's index byte is read together with the three bytes after it (one 4-byte read).
    int index;
    std::memcpy(&index, &entry->index, sizeof(index));
    action2.param = index;
    action2.modifier = 0;
    action2.kind = (entry->key & kEmoteKeyBit) ? kOrderEmote : kOrderAnimation;
}

void CUnitServer::StopAction() {
    state = kStateStopped;
    unit->UpdateSizeClass();
    if (effectObject) {
        g_serverWorld->RemoveTimedObject(effectObject->id_);
        CEngineObject* old = effectObject;
        effectObject = nullptr;
        if (old) old->Release();
    }
    OrderPlanReset(&currentPlan);   // SetSize(0, -1) instead of the free, like the original
    StandStill();
    action.Reset();
    action2.Reset();
    // Idle, inlined: the current order is never of kind 0 here, the queued one is 9.
    if (action.kind != kOrderIdle && action2.kind != kOrderAnimation && action2.kind != kOrderEmote) DropQueuedOrder();
}

BOOL CUnitServer::HasOtherArmour() {
    for (int slot = 3; slot < 7; ++slot)
        if (unit->armor[slot]) return 1;
    return 0;
}

BOOL CUnitServer::HasMainArmour() {
    for (int slot = 0; slot < 3; ++slot)
        if (unit->armor[slot]) return 1;
    return 0;
}

FigureAnimation* CBaseManager::FindFigureAnimation(const std::string& figure, const char* name) {
    AnimationDatabase* database;
    {
        std::string file = figure;
        file += ".adb";
        database = static_cast<AnimationDatabase*>(GetFile(file, nullptr));
    }
    if (!database) {
        game::DebugLog("Can't find animation database file for \"%s\".\n", figure);
        return nullptr;
    }
    for (int i = 0; i < static_cast<int>(database->count); ++i)
        if (text::CompareIgnoringCase(database->animations[i].name, name) == 0) return &database->animations[i];
    return nullptr;
}
