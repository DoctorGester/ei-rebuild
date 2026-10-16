// CUnitServer's Load and Save slots (vtable 0073EF38 slots 9 and 10). See combat.h and
// docs/notes/combat.md ("Rewritten").
#include "ai_alarm.h"
#include "ai_map.h"
#include "combat.h"

#include "map_object.h"
#include "object.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>
#include "originals_core.h"
#include "originals_library.h"
#include "originals_units.h"

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

// The map objects' manager (007AFFA0, the world server's +0x40).
void* UnitManager() { return WorldObjectManager(); }

template <typename T>
void WriteCopy(CStream* stream, T value) {
    stream->Write(&value, sizeof(T));
}

int IdOrNone(const CEngineObject* object) { return object ? object->id_ : -1; }

// 00556C60 / 00556C40 (two copies): assigns a counted reference, releasing the previous object.
void AssignCounted(CEngineObject*& slot, CEngineObject* object) {
    CEngineObject* previous = slot;
    slot = object;
    if (object) ++object->refCount_;
    if (previous) previous->Release();
}
} // namespace

namespace {
// 00544EF0 / 00544F60: the plan's own AnimationList part (its fields +0x18.. follow here).
__declspec(naked) void __fastcall PlanLoad(void*, void*, CStream*) EI_ORIGINAL(0x00544EF0)
__declspec(naked) void __fastcall PlanSave(void*, void*, CStream*) EI_ORIGINAL(0x00544F60)
// 0050E220 / 0050E290: a counted reference by id (load resolves through `context`).
// 00403F40 CBaseManager::FindObject (base_manager.cpp), through its game address.

// Resolves an id read from the stream in `manager` (-1: none).
CEngineObject* ReadObject(CStream* stream, void* manager) {
    int id;
    stream->Read(&id, 4);
    return id == -1 ? nullptr : ManagerFindObject(manager, nullptr, id);
}

void LoadOrder(CStream* stream, UnitAction* order) {
    stream->Read(&order->kind, 4);
    stream->Read(&order->source, 4);
    AIRef::Of(&order->target)->Load(stream, WorldServer());
    stream->Read(&order->point, 0xC);
    stream->Read(&order->param, 4);
    stream->Read(&order->modifier, 1);
    if (stream->version_ >= kVersionPlanDirection) stream->Read(&order->flag18, 1);
}

void SaveOrder(CStream* stream, UnitAction* order) {
    stream->Write(&order->kind, 4);
    stream->Write(&order->source, 4);
    AIRef::Of(&order->target)->Save(stream);
    stream->Write(&order->point, 0xC);
    WriteCopy(stream, order->param);
    WriteCopy(stream, order->modifier);
    WriteCopy(stream, order->flag18);
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
} // namespace

// --- Step lists and the armour spell list (A-U7) ----------------------------------------------------

namespace {
// The item manager (007AFFB4, the world server's +0x54), which resolves the armour spells' item ids.
void* ItemManager() { return WorldServer()->itemManager_; }
} // namespace

void __fastcall LoadStepList(AnimationSteps* steps, CStream* stream) {
    int count = 0;
    stream->Read(&count, 4);
    StepsSetSize(steps, nullptr, count, -1);
    for (int i = 0; i < steps->count; ++i) stream->Read(&steps->steps[i], sizeof(AnimationStep));
}
EI_REPLACE(0x005582A0, LoadStepList);

void __fastcall SaveStepList(const AnimationSteps* steps, CStream* stream) {
    WriteCopy(stream, steps->count);
    for (int i = 0; i < steps->count; ++i) stream->Write(&steps->steps[i], sizeof(AnimationStep));
}
EI_REPLACE(0x00558420, SaveStepList);

void ArmourSpell::Save(CStream* stream) const {
    WriteCopy(stream, IdOrNone(item));
    WriteCopy(stream, countdown);
}
EI_REPLACE_METHOD(0x00545430, ArmourSpell::Save);

void __fastcall LoadArmourSpells(ArmourSpellList* list, CStream* stream) {
    int count = 0;
    stream->Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        auto* node = list->NewNode(list->tail, nullptr);
        // The entry takes a blank one (no item, countdown 0; `worn` is the original's stack byte).
        CEngineObject* old = node->data.item;
        node->data.item = nullptr;
        if (old) old->Release();
        node->data.countdown = 0;
        node->data.worn = 0;
        if (list->tail) list->tail->next = node;
        else list->head = node;
        list->tail = node;
    }
    for (auto* node = list->head; node;) {
        auto* current = node;
        node = node->next;
        AssignCounted(current->data.item, ReadObject(stream, ItemManager()));
        stream->Read(&current->data.countdown, 4);
    }
}
EI_REPLACE(0x005585F0, LoadArmourSpells);

void CUnitServer::LoadServer(CStream* stream) {
    aiPresence = nullptr;
    stream->Read(&frozen, 1);
    stream->Read(&animationHeld, 1);
    LoadStepList(&queue, stream);
    stream->Read(&queue.extra, 4);
    static_cast<AIPath*>(&motion)->LoadPath(stream);
    PlanLoad(&currentPlan, nullptr, stream);
    LoadPlanFields(stream, &currentPlan);
    stream->Read(&keepHitDirection, 1);
    stream->Read(&stoodBlocked, 1);
    stream->Read(&motionChanged, 1);
    stream->Read(&movingAnimation, 1);
    LoadStepList(&savedQueue, stream);
    stream->Read(&savedQueue.extra, 4);
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
        std::memcpy(&stats.fixedMaxHitPoints, &static_cast<CMapObjectServer*>(unit->companion)->vitals.maxHitPoints, 4);
    for (int i = 0; i < 4; ++i) stream->Read(&moveFactors[i], 4);
    stream->Read(&tameStage, 4);
    stream->Read(&ownSide, 4);
    stream->Read(&tamerSide, 4);
    stream->Read(&speedClass, 4);
    stream->Read(&claimedHostile, 4);
    static_cast<AIBrain*>(&brain)->Load(stream);
    static_cast<AIAlarm*>(&voice)->Load(stream);
    stream->Read(&orderStopped, 1);
    stream->Read(&deathReported, 1);
    stream->Read(&sinkSpeed, 4);

    // The context's (the world's) slot 14: the timed objects.
    void* effects = static_cast<CWorld*>(ManagerContext())->TimedObjectManager();
    AssignCounted(effectObject, ReadObject(stream, effects));
    LoadArmourSpells(&armourSpells, stream);
    stream->Read(&followsLeader, 1);
    void* units = UnitManager();
    AssignCounted(followedLeader, ReadObject(stream, units));

    if (stream->version_ < kVersion180) return;
    stream->Read(&actingCheckTime, 4);
    if (stream->version_ < kVersionUnused) return;
    int unused;
    stream->Read(&unused, 4);
    if (stream->version_ < kVersion18E) return;
    stream->Read(&searchingTarget, 1);
    if (stream->version_ < kVersion8E4) return;
    stream->Read(protection, 0x10);
    if (stream->version_ < kVersion938) return;
    stream->Read(&boredTicks, 4);
    if (stream->version_ < kVersionPendingHit) return;
    stream->Read(&pendingHit, 0x3C);
    if (stream->version_ < kVersion93C) return;
    WriteCopy(stream, waitFrame);    // sic: written, not read
}
EI_REPLACE_METHOD(0x00555D10, CUnitServer::LoadServer);

void CUnitServer::SaveServer(CStream* stream) {
    WriteCopy(stream, frozen);
    WriteCopy(stream, animationHeld);
    SaveStepList(&queue, stream);
    WriteCopy(stream, queue.extra);
    motion.SavePath(stream);
    PlanSave(&currentPlan, nullptr, stream);
    SavePlanFields(stream, &currentPlan);
    WriteCopy(stream, keepHitDirection);
    WriteCopy(stream, stoodBlocked);
    WriteCopy(stream, motionChanged);
    WriteCopy(stream, movingAnimation);
    SaveStepList(&savedQueue, stream);
    WriteCopy(stream, savedQueue.extra);
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
    static_cast<AIBrain*>(&brain)->Save(stream);
    static_cast<AIAlarm*>(&voice)->Save(stream);
    WriteCopy(stream, orderStopped);
    WriteCopy(stream, deathReported);
    stream->Write(&sinkSpeed, 4);   // the float's bits
    WriteCopy(stream, IdOrNone(effectObject));
    WriteCopy(stream, armourSpells.count);
    for (auto* node = armourSpells.head; node;) {
        auto* current = node;
        node = node->next;
        current->data.Save(stream);
    }
    WriteCopy(stream, followsLeader);
    WriteCopy(stream, IdOrNone(followedLeader));
    WriteCopy(stream, actingCheckTime);
    WriteCopy(stream, kSavedConstant);
    WriteCopy(stream, searchingTarget);
    stream->Write(protection, 0x10);
    WriteCopy(stream, boredTicks);
    stream->Write(&pendingHit, 0x3C);
    WriteCopy(stream, waitFrame);
}
EI_REPLACE_METHOD(0x005561C0, CUnitServer::SaveServer);
