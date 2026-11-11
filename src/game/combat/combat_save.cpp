// UnitServer's Load and Save virtuals. See combat.h and docs/notes/combat.md ("Order lifecycle").
#include "game/ai/ai_alarm.h"
#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "game/combat/combat.h"
#include "game/items/item_manager.h"

#include "game/objects/map_object.h"
#include "game/world/hoover.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"

#include <cstdint>
#include <cstring>
#include "game/app/singletons.h"
#include "game/saves/save_versions.h"

namespace {
constexpr int kSavedConstant = 2;             // written between actingCheckTime and searchingTarget, read back and dropped

// Resolves an id read from the stream in `manager` (kNoObjectId: none).
EngineObject* ReadObject(ByteStream* stream, const BaseManager* manager) {
    const int id = stream->ReadId();
    return id == kNoObjectId ? nullptr : manager->FindObject(id);
}
Item* ReadItem(ByteStream* stream, const BaseManager* items) {
    const int id = stream->ReadId();
    return id == kNoObjectId ? nullptr : items->FindObjectAs<Item>(id);
}
Unit* ReadUnit(ByteStream* stream, const BaseManager* objects) {
    const int id = stream->ReadId();
    return id == kNoObjectId ? nullptr : UnitWithId(objects, id);
}

void LoadOrder(ByteStream* stream, UnitAction* order) {
    stream->ReadValue(order->kind);
    stream->ReadValue(order->source);
    LoadRef(order->target, stream, &g_serverWorld);
    stream->ReadValue(order->point);
    stream->ReadValue(order->param);
    stream->ReadValue(order->modifier);
    if (stream->version >= kVersionPlanDirection) stream->ReadValue(order->pointIsUseSpot);
}

void SaveOrder(ByteStream* stream, UnitAction* order) {
    stream->WriteValue(order->kind);
    stream->WriteValue(order->source);
    stream->WriteId(order->target);
    stream->WriteValue(order->point);
    stream->WriteValue(order->param);
    stream->WriteValue(order->modifier);
    stream->WriteValue(order->pointIsUseSpot);
}

// The plan object's fields after its list (plan + 0x18 ..).
void LoadPlanFields(ByteStream* stream, OrderPlan* plan) {
    stream->ReadValue(plan->startTime);
    stream->ReadValue(plan->endTime);
    stream->ReadValue(plan->targetId);
    stream->ReadValue(plan->hitLocation);
    stream->ReadValue(plan->rangeSq);
    stream->ReadValue(plan->isCast);
    stream->ReadValue(plan->calledShot);
    stream->ReadValue(plan->castKind);
    if (stream->version >= kVersionPlanDirection) stream->ReadValue(plan->direction);
}

void SavePlanFields(ByteStream* stream, OrderPlan* plan) {
    stream->WriteValue(plan->startTime);
    stream->WriteValue(plan->endTime);
    stream->WriteValue(plan->targetId);
    stream->WriteValue(plan->hitLocation);
    stream->WriteValue(plan->rangeSq);   // the float's bits
    stream->WriteValue(plan->isCast);
    stream->WriteValue(plan->calledShot);
    stream->WriteValue(plan->castKind);
    stream->WriteValue(plan->direction);
}

} // namespace

// --- Step lists and the armour spell list ----------------------------------------------------

void ArmourSpell::Load(ByteStream* stream) {
    item.Assign(ReadItem(stream, g_serverWorld.itemManager.get()));
    stream->ReadValue(countdown);
}

void ArmourSpell::Save(ByteStream* stream) const {
    stream->WriteId(item);
    stream->WriteValue(countdown);
}

void UnitServer::Load(ByteStream* stream) {
    stream->ReadValue(frozen);
    stream->ReadValue(animationHeld);
    queue.Load(stream);
    motion.LoadPath(stream);
    currentPlan.animation.Load(stream);
    LoadPlanFields(stream, &currentPlan);
    stream->ReadValue(keepHitDirection);
    stream->ReadValue(stoodBlocked);
    stream->ReadValue(motionChanged);
    stream->ReadValue(movingAnimation);
    savedQueue.Load(stream);
    stream->ReadValue(hitDirection);
    stream->ReadValue(state);
    stream->ReadValue(moveMode);
    stream->ReadValue(moveMode2);
    stream->ReadValue(nextAttackTime);
    stream->ReadValue(interceptTime);
    stream->ReadValue(sneakWait);
    LoadOrder(stream, &action);
    LoadOrder(stream, &action2);
    stats.Load(stream);
    if (stream->version < kVersionFixedMaxHitPoints)
        stats.fixedMaxHitPoints = unit->Vitals().maxHitPoints;
    for (float& factor : moveFactors) stream->ReadValue(factor);
    stream->ReadValue(tameStage);
    stream->ReadValue(ownSide);
    stream->ReadValue(tamerSide);
    stream->ReadValue(speedClass);
    stream->ReadValue(claimedHostile);
    brain.Load(stream);
    voice.Load(stream);
    stream->ReadValue(orderStopped);
    stream->ReadValue(deathReported);
    stream->ReadValue(sinkSpeed);

    BaseManager* effects = GetManagerContext()->TimedObjectManager();
    effectObject.Assign(ReadObject(stream, effects));
    // Blank entries (no item, countdown 0; `worn` was the original's stack byte, ours 0), replacing the list.
    armourSpells.assign(stream->ReadCount(), ArmourSpell{{}, 0, 0});
    for (ArmourSpell& entry : armourSpells) entry.Load(stream);
    stream->ReadValue(followsLeader);
    followedLeader.Assign(ReadUnit(stream, g_serverWorld.objectManager.get()));

    if (stream->version < kVersionActingCheckTime) return;
    stream->ReadValue(actingCheckTime);
    if (stream->version < kVersionUnusedDword) return;
    stream->Skip(sizeof(kSavedConstant));
    if (stream->version < kVersionSearchingTarget) return;
    stream->ReadValue(searchingTarget);
    if (stream->version < kVersionProtection) return;
    stream->ReadValue(protection);
    if (stream->version < kVersionBoredTicks) return;
    stream->ReadValue(boredTicks);
    if (stream->version < kVersionPendingHit) return;
    stream->ReadValue(pendingHit);
    if (stream->version < kVersionWaitFrame) return;
    stream->ReadValue(waitFrame);
}

void UnitServer::Save(ByteStream* stream) {
    stream->WriteValue(frozen);
    stream->WriteValue(animationHeld);
    queue.Save(stream);
    motion.SavePath(stream);
    currentPlan.animation.Save(stream);
    SavePlanFields(stream, &currentPlan);
    stream->WriteValue(keepHitDirection);
    stream->WriteValue(stoodBlocked);
    stream->WriteValue(motionChanged);
    stream->WriteValue(movingAnimation);
    savedQueue.Save(stream);
    stream->WriteValue(hitDirection);
    stream->WriteValue(state);
    stream->WriteValue(moveMode);
    stream->WriteValue(moveMode2);
    stream->WriteValue(nextAttackTime);
    stream->WriteValue(interceptTime);
    stream->WriteValue(sneakWait);
    SaveOrder(stream, &action);
    SaveOrder(stream, &action2);
    stats.Save(stream);
    for (float factor : moveFactors) stream->WriteValue(factor);
    stream->WriteValue(tameStage);
    stream->WriteValue(ownSide);
    stream->WriteValue(tamerSide);
    stream->WriteValue(speedClass);
    stream->WriteValue(claimedHostile);
    brain.Save(stream);
    voice.Save(stream);
    stream->WriteValue(orderStopped);
    stream->WriteValue(deathReported);
    stream->WriteValue(sinkSpeed);   // the float's bits
    stream->WriteId(effectObject);
    stream->WriteCount(armourSpells.size());
    for (const ArmourSpell& entry : armourSpells) entry.Save(stream);
    stream->WriteValue(followsLeader);
    stream->WriteId(followedLeader);
    stream->WriteValue(actingCheckTime);
    stream->WriteValue(kSavedConstant);
    stream->WriteValue(searchingTarget);
    stream->WriteValue(protection);
    stream->WriteValue(boredTicks);
    stream->WriteValue(pendingHit);
    stream->WriteValue(waitFrame);
}
