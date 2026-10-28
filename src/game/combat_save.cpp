// CUnitServer's Load and Save virtuals. See combat.h and docs/notes/combat.md ("Rewritten").
#include "ai_alarm.h"
#include "ai_map.h"
#include "base_manager.h"
#include "combat.h"

#include "map_object.h"
#include "object.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include "game_singletons.h"

namespace {
// Fields of CUnitServer that combat.h leaves unnamed (offsets from the controller).

constexpr int kVersionPlanDirection = 0x48;   // plan +0x38 and the orders' +0x18 byte
constexpr int kVersionFixedMaximum = 0x50;    // older saves take stats +0x6FC from the server object
constexpr int kVersion180 = 0x44;
constexpr int kVersionUnused = 0x48;
constexpr int kVersion18E = 0x4A;
constexpr int kVersion8E4 = 0x4C;
constexpr int kVersion938 = 0x54;
constexpr int kVersionPendingHit = 0x69;
constexpr int kVersion93C = 0x73;
constexpr int kSavedConstant = 2;             // written after +0x180, read back and dropped

template <typename T>
void WriteCopy(CStream* stream, T value) {
    stream->Write(&value, sizeof(T));
}

int IdOrNone(const CEngineObject* object) { return object ? object->id_ : -1; }

// Assigns a counted reference, releasing the previous object (two copies in the game).
template <typename T>
void AssignCounted(T*& slot, T* object) {
    T* previous = slot;
    slot = object;
    if (object) ++object->refCount_;
    if (previous) previous->Release();
}

// A counted reference by id (load resolves through `context`); CBaseManager::FindObject,
// through its game address.

// Resolves an id read from the stream in `manager` (-1: none).
CEngineObject* ReadObject(CStream* stream, const CBaseManager* manager) {
    int id;
    stream->Read(&id, 4);
    return id == -1 ? nullptr : manager->FindObject(id);
}
CItem* ReadItem(CStream* stream, const CBaseManager* items) {
    int id;
    stream->Read(&id, 4);
    return id == -1 ? nullptr : FindItem(items, id);
}
CUnit* ReadUnit(CStream* stream, const CBaseManager* objects) {
    int id;
    stream->Read(&id, 4);
    return id == -1 ? nullptr : UnitWithId(objects, id);
}

void LoadOrder(CStream* stream, UnitAction* order) {
    stream->Read(&order->kind, 4);
    stream->Read(&order->source, 4);
    order->target.Load(stream, &g_serverWorld.Get());
    stream->Read(&order->point, 0xC);
    stream->Read(&order->param, 4);
    stream->Read(&order->modifier, 1);
    if (stream->version_ >= kVersionPlanDirection) stream->Read(&order->pointIsUseSpot, 1);
}

void SaveOrder(CStream* stream, UnitAction* order) {
    stream->Write(&order->kind, 4);
    stream->Write(&order->source, 4);
    order->target.Save(stream);
    stream->Write(&order->point, 0xC);
    WriteCopy(stream, order->param);
    WriteCopy(stream, order->modifier);
    WriteCopy(stream, order->pointIsUseSpot);
}

// The plan object's fields after its list (plan + 0x18 ..).
void LoadPlanFields(CStream* stream, OrderPlan* plan) {
    stream->Read(&plan->startTime, 4);
    stream->Read(&plan->endTime, 4);
    stream->Read(&plan->targetId, 4);
    stream->Read(&plan->hitLocation, 4);
    stream->Read(&plan->rangeSq, 4);
    stream->Read(&plan->isCast, 1);
    stream->Read(&plan->notSkill, 1);
    stream->Read(&plan->castKind, 4);
    if (stream->version_ >= kVersionPlanDirection) stream->Read(&plan->direction, 0xC);
}

void SavePlanFields(CStream* stream, OrderPlan* plan) {
    WriteCopy(stream, plan->startTime);
    WriteCopy(stream, plan->endTime);
    WriteCopy(stream, plan->targetId);
    stream->Write(&plan->hitLocation, 4);
    stream->Write(&plan->rangeSq, 4);   // the float's bits
    WriteCopy(stream, plan->isCast);
    WriteCopy(stream, plan->notSkill);
    stream->Write(&plan->castKind, 4);
    stream->Write(&plan->direction, 0xC);
}

// The item manager (the world server's), which resolves the armour spells' item ids.
CItemManager* ItemManager() { return g_serverWorld->itemManager_; }

} // namespace

// --- Step lists and the armour spell list ----------------------------------------------------

void ArmourSpell::Save(CStream* stream) const {
    WriteCopy(stream, IdOrNone(item));
    WriteCopy(stream, countdown);
}

void LoadArmourSpells(std::list<ArmourSpell>* list, CStream* stream) {
    int count = 0;
    stream->Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        auto node = list->emplace(list->end());
        // The entry takes a blank one (no item, countdown 0; `worn` is the original's stack byte).
        CEngineObject* old = (*node).item;
        (*node).item = nullptr;
        if (old) old->Release();
        (*node).countdown = 0;
        (*node).worn = 0;
    }
    for (auto node = list->begin(); node != list->end();) {
        auto current = node;
        ++node;
        AssignCounted(current->item, ReadItem(stream, ItemManager()));
        stream->Read(&current->countdown, 4);
    }
}

void CUnitServer::Load(CStream* stream) {
    aiPresence = nullptr;
    stream->Read(&frozen, 1);
    stream->Read(&animationHeld, 1);
    queue.Load(stream);
    motion.LoadPath(stream);
    currentPlan.animation.Load(stream);
    LoadPlanFields(stream, &currentPlan);
    stream->Read(&keepHitDirection, 1);
    stream->Read(&stoodBlocked, 1);
    stream->Read(&motionChanged, 1);
    stream->Read(&movingAnimation, 1);
    savedQueue.Load(stream);
    stream->Read(&hitDirection, 4);
    stream->Read(&state, 4);
    stream->Read(&moveMode, 8);
    stream->Read(&moveMode2, 8);
    stream->Read(&nextAttackTime, 4);
    stream->Read(&interceptTime, 4);
    stream->Read(&sneakWait, 4);
    LoadOrder(stream, &action);
    LoadOrder(stream, &action2);
    stats.LoadFrom(stream);
    if (stream->version_ < kVersionFixedMaximum)
        std::memcpy(&stats.fixedMaxHitPoints, &unit->ServerCompanion()->vitals.maxHitPoints, 4);
    for (int i = 0; i < 4; ++i) stream->Read(&moveFactors[i], 4);
    stream->Read(&tameStage, 4);
    stream->Read(&ownSide, 4);
    stream->Read(&tamerSide, 4);
    stream->Read(&speedClass, 4);
    stream->Read(&claimedHostile, 4);
    brain.Load(stream);
    voice.Load(stream);
    stream->Read(&orderStopped, 1);
    stream->Read(&deathReported, 1);
    stream->Read(&sinkSpeed, 4);

    CBaseManager* effects = WorldContext()->TimedObjectManager();
    AssignCounted(effectObject, ReadObject(stream, effects));
    LoadArmourSpells(&armourSpells, stream);
    stream->Read(&followsLeader, 1);
    AssignCounted(followedLeader, ReadUnit(stream, g_serverWorld->objectManager_));

    if (stream->version_ < kVersion180) return;
    stream->Read(&actingCheckTime, 4);
    if (stream->version_ < kVersionUnused) return;
    int unused;
    stream->Read(&unused, 4);
    if (stream->version_ < kVersion18E) return;
    stream->Read(&searchingTarget, 1);
    if (stream->version_ < kVersion8E4) return;
    stream->Read(&protection, sizeof(protection));
    if (stream->version_ < kVersion938) return;
    stream->Read(&boredTicks, 4);
    if (stream->version_ < kVersionPendingHit) return;
    stream->Read(&pendingHit, 0x3C);
    if (stream->version_ < kVersion93C) return;
    WriteCopy(stream, waitFrame);    // sic: written, not read
}

void CUnitServer::Save(CStream* stream) {
    WriteCopy(stream, frozen);
    WriteCopy(stream, animationHeld);
    queue.Save(stream);
    motion.SavePath(stream);
    currentPlan.animation.Save(stream);
    SavePlanFields(stream, &currentPlan);
    WriteCopy(stream, keepHitDirection);
    WriteCopy(stream, stoodBlocked);
    WriteCopy(stream, motionChanged);
    WriteCopy(stream, movingAnimation);
    savedQueue.Save(stream);
    WriteCopy(stream, hitDirection);
    stream->Write(&state, 4);
    stream->Write(&moveMode, 8);
    stream->Write(&moveMode2, 8);
    WriteCopy(stream, nextAttackTime);
    WriteCopy(stream, interceptTime);
    WriteCopy(stream, sneakWait);
    SaveOrder(stream, &action);
    SaveOrder(stream, &action2);
    stats.SaveTo(stream);
    for (int i = 0; i < 4; ++i) WriteCopy(stream, moveFactors[i]);
    stream->Write(&tameStage, 4);
    WriteCopy(stream, ownSide);
    WriteCopy(stream, tamerSide);
    WriteCopy(stream, speedClass);
    WriteCopy(stream, claimedHostile);
    brain.Save(stream);
    voice.Save(stream);
    WriteCopy(stream, orderStopped);
    WriteCopy(stream, deathReported);
    stream->Write(&sinkSpeed, 4);   // the float's bits
    WriteCopy(stream, IdOrNone(effectObject));
    WriteCopy(stream, std::ssize(armourSpells));
    for (auto node = armourSpells.begin(); node != armourSpells.end();) {
        auto current = node;
        ++node;
        current->Save(stream);
    }
    WriteCopy(stream, followsLeader);
    WriteCopy(stream, IdOrNone(followedLeader));
    WriteCopy(stream, actingCheckTime);
    WriteCopy(stream, kSavedConstant);
    WriteCopy(stream, searchingTarget);
    stream->Write(&protection, sizeof(protection));
    WriteCopy(stream, boredTicks);
    stream->Write(&pendingHit, 0x3C);
    WriteCopy(stream, waitFrame);
}
