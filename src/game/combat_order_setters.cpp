// Combat orders, the rest of the setters (A-U5): movement style, wanted weapon, the skill and use
// orders, playing an animation as an order, dropping everything, and two armour queries. See
// combat.h and docs/notes/combat.md.
#include "combat.h"

#include "ai_map.h"
#include "base_manager.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "player_messages.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>

namespace {
constexpr int kOrderUse = 6;            // UnitAction kinds
constexpr int kOrderAnimation = 7;
constexpr int kOrderEmote = 8;
constexpr int kStateStopped = 6;        // CUnitServer::state after StopAction
constexpr int kStyleWalk = 2;
constexpr int kStyleRun = 3;
// CEngineObject::prototypeId_ (+0x18, not type_) of a unit that cannot be tamed (0x32, the value type_
// has for heroes; the original compares +0x18).
constexpr std::uint32_t kUntameable = 0x32;
constexpr int kMessageMoveStyle = 9;    // CPlayerServer::SendObject0B code

// Sources of a kind 6 order (CUnitServer::UseSkill reads them).
constexpr std::uint32_t kSourceUse = 0;
constexpr std::uint32_t kSourceSteal = 1;
constexpr std::uint32_t kSourceTame = 2;
constexpr std::uint32_t kSourceSearchBody = 0x18A95;

// The planner's shout codes per order (0054E600's second argument).
constexpr int kShoutSteal = 6;
constexpr int kShoutTame = 5;
constexpr int kShoutUse = 5;
constexpr int kShoutSearchBody = 4;

constexpr std::uint32_t kEmoteKeyBit = 0x80000000u;   // FigureAnimation::key: an emote (kind 8)
constexpr std::uint32_t kFlagShared = 0x20000000u;    // CEngineObject::flags_: on the world's shared list
constexpr int kStateActing = 5;                       // CUnitServer::state

CPlayerServer* PlayerConnection(CUnit* unit) { return unit->player->Server(); }

// The shared tail of the kind 6 setters: the queued order (reset first, inlined 0054B230) becomes
// a use of `target` with `source`, +0x1C `spot` and +0x20 `run`, and is planned; reset again when the
// planner refuses.
void QueueUse(CUnitServer* self, CEngineObject* target, std::uint32_t source, int spot, int run, int shout) {
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
    CAIMap* map = TheServerWorld()->AIMap();
    int saved = unit->moveMode;
    map->SectorMap()->Remove(unit);
    unit->moveMode = mode;
    unit->UpdateSizeClass();
    bool fits = map->IsUnitCellPassable(unit);
    unit->moveMode = saved;
    unit->UpdateSizeClass();
    map->SectorMap()->Add(unit);
    return fits;
}
EI_REWRITTEN(0x0054A5A0, CUnitServer::FitsMoveMode);

void CUnitServer::SetMoveStyle(int style) {
    boredTicks = 0;
    unit->moveMode2 = style;
    if (FitsMoveMode(style)) {
        if (style == kStyleRun && stats.MovementState(1) != 0) return;
        if (unit->player) PlayerConnection(unit)->SendObject0B(unit, kMessageMoveStyle);
    }
    if (style != kStyleWalk && style != kStyleRun) action.modifier = 0;
}
EI_REPLACE_METHOD(0x00555270, CUnitServer::SetMoveStyle);

void CUnitServer::SetWantedWeapon(int slot) {
    boredTicks = 0;
    unit->pendingWeapon = slot;
}
EI_REPLACE_METHOD(0x005552E0, CUnitServer::SetWantedWeapon);

void CUnitServer::OrderSteal(CEngineObject* target, int run) {
    if (!target || ObjectSlots(target)->IsDead() != 0 || target == unit) return;
    QueueUse(this, target, kSourceSteal, 0, run, kShoutSteal);
}
EI_REPLACE_METHOD(0x005553B0, CUnitServer::OrderSteal);

void CUnitServer::OrderTame(CEngineObject* target, int run) {
    if (!target || ObjectSlots(target)->IsDead() != 0 || target == unit) return;
    auto* other = static_cast<CUnit*>(target);
    if (other->player == unit->player || other->prototypeId_ == kUntameable) return;
    QueueUse(this, target, kSourceTame, 0, run, kShoutTame);
}
EI_REPLACE_METHOD(0x00555480, CUnitServer::OrderTame);

void CUnitServer::OrderUse(CEngineObject* target, int spot, int run) {
    if (!target) return;
    QueueUse(this, target, kSourceUse, spot, run, kShoutUse);
}
EI_REPLACE_METHOD(0x00555570, CUnitServer::OrderUse);

void CUnitServer::OrderSearchBody(CEngineObject* target, int run) {
    if (!target || ObjectSlots(target)->IsDead() == 0 || target == unit) return;
    QueueUse(this, target, kSourceSearchBody, 0, run, kShoutSearchBody);
}
EI_REPLACE_METHOD(0x00555630, CUnitServer::OrderSearchBody);

bool CUnitServer::TargetBusy(CEngineObject* target) {
    if (target->flags_ & kFlagShared) return !TheServerWorld()->IsObjectShared(target->id_, unit->id_);
    AIBrain* ai = &brain;
    for (auto* node = ai->memory.nearby.head; node; node = node->next) {
        auto* other = node->data;
        if (!other || ObjectSlots(other)->IsDead() != 0) continue;
        auto* controller = reinterpret_cast<CUnitServer*>(other->controller);
        const UnitAction& order = controller->action;
        if (order.kind == kOrderUse && order.source == kSourceSearchBody && order.target == target &&
            (controller->state == kStateActing || controller->searchingTarget))
            return true;
    }
    return false;
}
EI_REPLACE_METHOD(0x0054C760, CUnitServer::TargetBusy);

void CUnitServer::PlayAnimation(const char* name) {
    const FigureAnimation* entry;
    {
        mfc::CString figure(unit->name);
        auto* world = static_cast<CWorld*>(ManagerContext());
        entry = (world->MapObjectManager())->FindFigureAnimation(figure, name);
    }
    if (!entry) return;
    action2.Reset();
    // The entry's index byte is read as a dword together with the three bytes after it.
    int index;
    std::memcpy(&index, &entry->index, sizeof(index));
    action2.param = index;
    action2.modifier = 0;
    action2.kind = (entry->key & kEmoteKeyBit) ? kOrderEmote : kOrderAnimation;
}
EI_REWRITTEN(0x00555700, CUnitServer::PlayAnimation);

void CUnitServer::StopAction() {
    state = kStateStopped;
    unit->UpdateSizeClass();
    if (effectObject) {
        TheServerWorld()->RemoveTimedObject(effectObject->id_);
        CEngineObject* old = effectObject;
        effectObject = nullptr;
        if (old) old->Release();
    }
    OrderPlanReset(&currentPlan);   // inlined in the original (SetSize(0, -1) instead of the free)
    StandStill();              // inlined in the original
    action.Reset();
    action2.Reset();
    // Idle (00555180), inlined: the current order is never of kind 0 here, the queued one is 9.
    if (action.kind != 0 && action2.kind != kOrderAnimation && action2.kind != kOrderEmote) DropQueuedOrder();
}
EI_REWRITTEN(0x00555840, CUnitServer::StopAction);

BOOL CUnitServer::HasOtherArmour() {
    for (int slot = 3; slot < 7; ++slot)
        if (unit->armor[slot]) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x005567A0, CUnitServer::HasOtherArmour);

BOOL CUnitServer::HasMainArmour() {
    for (int slot = 0; slot < 3; ++slot)
        if (unit->armor[slot]) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x005567E0, CUnitServer::HasMainArmour);

FigureAnimation* CBaseManager::FindFigureAnimation(const mfc::CString& figure, const char* name) {
    AnimationDatabase* database;
    {
        mfc::CString file(figure);
        file += ".adb";
        database = static_cast<AnimationDatabase*>(GetFile(file, nullptr));
    }
    if (!database) {
        game::DebugLog("Can't find animation database file for \"%s\".\n", static_cast<const char*>(figure));
        return nullptr;
    }
    for (int i = 0; i < static_cast<int>(database->count); ++i)
        if (libcrt::Stricmp(database->animations[i].name, name) == 0) return &database->animations[i];
    return nullptr;
}
EI_REWRITTEN(0x00512F60, CBaseManager::FindFigureAnimation);
