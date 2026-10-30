// UnitServer's Load and Save virtuals. See combat.h and docs/notes/combat.md ("Rewritten").
#include "ai_alarm.h"
#include "ai_map.h"
#include "base_manager.h"
#include "combat.h"
#include "item_manager.h"

#include "map_object.h"
#include "object.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include "game_singletons.h"
#include "save_versions.h"

namespace {
// Fields of UnitServer that combat.h leaves unnamed (offsets from the controller).

constexpr int kSavedConstant = 2;             // written after +0x180, read back and dropped

int IdOrNone(const EngineObject* object) { return object ? object->id : -1; }

// Resolves an id read from the stream in `manager` (-1: none).
EngineObject* ReadObject(ByteStream* stream, const BaseManager* manager) {
    int id;
    stream->ReadValue(id);
    return id == -1 ? nullptr : manager->FindObject(id);
}
Item* ReadItem(ByteStream* stream, const BaseManager* items) {
    int id;
    stream->ReadValue(id);
    return id == -1 ? nullptr : FindItem(items, id);
}
Unit* ReadUnit(ByteStream* stream, const BaseManager* objects) {
    int id;
    stream->ReadValue(id);
    return id == -1 ? nullptr : UnitWithId(objects, id);
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
    SaveRef(order->target, stream);
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
    stream->ReadValue(plan->notSkill);
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
    stream->WriteValue(plan->notSkill);
    stream->WriteValue(plan->castKind);
    stream->WriteValue(plan->direction);
}

} // namespace

// --- Step lists and the armour spell list ----------------------------------------------------

void ArmourSpell::Save(ByteStream* stream) const {
    stream->WriteValue(IdOrNone(item));
    stream->WriteValue(countdown);
}

void LoadArmourSpells(std::vector<ArmourSpell>* list, ByteStream* stream) {
    int count = 0;
    stream->ReadValue(count);
    // Blank entries (no item, countdown 0; `worn` is the original's stack byte).
    if (count > 0) list->resize(list->size() + count, ArmourSpell{{}, 0, 0});
    for (ArmourSpell& entry : *list) {
        entry.item.Assign(ReadItem(stream, g_serverWorld.itemManager));
        stream->ReadValue(entry.countdown);
    }
}

void UnitServer::Load(ByteStream* stream) {
    aiPresence = nullptr;
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
    stats.LoadFrom(stream);
    if (stream->version < kVersionFixedMaxHitPoints)
        stats.fixedMaxHitPoints = unit->ServerCompanion()->vitals.maxHitPoints;
    for (int i = 0; i < 4; ++i) stream->ReadValue(moveFactors[i]);
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

    BaseManager* effects = WorldContext()->TimedObjectManager();
    effectObject.Assign(ReadObject(stream, effects));
    LoadArmourSpells(&armourSpells, stream);
    stream->ReadValue(followsLeader);
    followedLeader.Assign(ReadUnit(stream, g_serverWorld.objectManager));

    if (stream->version < kVersionActingCheckTime) return;
    stream->ReadValue(actingCheckTime);
    if (stream->version < kVersionUnusedDword) return;
    int unused;
    stream->ReadValue(unused);
    if (stream->version < kVersionSearchingTarget) return;
    stream->ReadValue(searchingTarget);
    if (stream->version < kVersionProtection) return;
    stream->ReadValue(protection);
    if (stream->version < kVersionBoredTicks) return;
    stream->ReadValue(boredTicks);
    if (stream->version < kVersionPendingHit) return;
    stream->ReadValue(pendingHit);
    if (stream->version < kVersionWaitFrame) return;
    stream->WriteValue(waitFrame);    // sic: written, not read
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
    stats.SaveTo(stream);
    for (int i = 0; i < 4; ++i) stream->WriteValue(moveFactors[i]);
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
    stream->WriteValue(IdOrNone(effectObject));
    stream->WriteValue(static_cast<std::int32_t>(std::ssize(armourSpells)));
    for (const ArmourSpell& entry : armourSpells) entry.Save(stream);
    stream->WriteValue(followsLeader);
    stream->WriteValue(IdOrNone(followedLeader));
    stream->WriteValue(actingCheckTime);
    stream->WriteValue(kSavedConstant);
    stream->WriteValue(searchingTarget);
    stream->WriteValue(protection);
    stream->WriteValue(boredTicks);
    stream->WriteValue(pendingHit);
    stream->WriteValue(waitFrame);
}
