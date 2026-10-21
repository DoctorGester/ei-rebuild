// The unit AI brain (005D0000-005DC000): the per-tick mode selection and the actions.
#include "unit_ai.h"

#include "ai_alarm.h"
#include "combat.h"
#include "game_data.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <bit>
#include <cstring>
#include "originals_library.h"
#include "originals_core.h"
#include "originals_ai.h"
#include "originals_units.h"
#include "originals_world.h"
#include "world.h"
#include "world_data.h"
#include "lib/crt_iostream.h"
#include "lib/mfc_array.h"
#include "game_arrays.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {
constexpr float kDecayScale = 30.0f;                                    // 0073BAF0
constexpr float kOne = 1.0f;                                            // 0073B84C
constexpr float kDecayDivisor = std::bit_cast<float>(0x3D042108u);      // 0073FFC0 (1 / 31)
constexpr float kArrivedDistanceSq = 1.0f;                              // 0073FEF0

constexpr int kControllerStateInactive = 5;
constexpr int kVoiceAlarm = 8;


// CEngineObject::Release through the object's vtable, then clear the slot (inlined everywhere).
void ReleaseRef(CEngineObject*& ref) {
    if (ref) {
        ref->Release();
        ref = nullptr;
    }
}

// Virtual slot 55 (0xDC) of units and objects: non-zero when dead / gone.
int IsGone(CEngineObject* object) { return ObjectSlots(object)->IsDead(); }

// The game's ostream (MSVC 6 iostream): operator<< for strings, chars and manipulators.
struct GameOstream : libcrt::Ostream {
    GameOstream* Write(const char* text) { PutText(text); return this; }                      // 006FC31D
    GameOstream* Put(int c) { libcrt::Ostream::PutChar(static_cast<char>(c)); return this; }  // 006FC0B0 (operator<<(char))
    GameOstream* Apply(libcrt::Manipulator manipulator) {                                                   // 00434FE0
        libcrt::Ostream::Apply(manipulator);
        return this;
    }
};
GameOstream*& ConsoleStream() { return ei::GameData<GameOstream*>(0x007C54E8); }   // the console's ostream*


// 0052CEC0: DestructElements of counted references (gamearrays::ObjectRefs').
template <typename T>
void DestructRefs(T** refs, int count) {
    gamearrays::ObjectRefs::DestructElements(reinterpret_cast<ScriptObjectRef*>(refs), count);
}

// 006E79A0: the game's memmove.


// The mode names of the "new motivation" message (the original pushes these addresses).
const char* ModeName(int type) {
    switch (type) {
    case kModeGuard: return "Guard";
    case kModePatrol: return "Patrol";
    case kModeSentry: return "Sentry";
    case kModeSuspection: return "Suspection";
    case kModeAggression: return "Aggression";
    case kModeCorpseWatcher: return "CorpseWatcher";
    case kModePlayer: return "Player";
    case kModeFear: return "Fear";
    case kModeTamed: return "Tamed";
    case kModeFollow: return "Follow";
    default: return nullptr;    // 9 and anything else: the name stays empty
    }
}
// 00791E20: "Unit %i new motivation %s with power of %i".
const char* MotivationFormat() { return "Unit %i new motivation %s with power of %i"; }
} // namespace

// --- Forwarders ----------------------------------------------------------------------------------

void AIMode::Release() { RefObjectRelease((void*)this); }
int AIMode::Evaluate(AIBrain* brain) { return Virtuals()->Evaluate(brain); }
void AIMode::Execute(AIBrain* brain) { Virtuals()->Execute(brain); }
void AIMode::Enter(AIBrain* brain) { Virtuals()->Enter(brain); }
void AIMode::Leave(AIBrain* brain) { Virtuals()->Leave(brain); }
int AIMode::GetType() { return Virtuals()->GetType(); }
void AIMode::OnObjectDeleted(CEngineObject* object) { Virtuals()->OnObjectDeleted(object); }

// AIRef (0052B030, 0052A8F0, 0050E220, 0050E290): counted_ref.cpp.

void __fastcall LooksDestroyAt(AILookArray* looks) { mfc::impl::DestructHeader<mfc::impl::AiLookArrayClass>(looks); }

AIRef* AIBrain::PopCorpse(AIRef* out) { return CorpsesPopHead(&memory.corpses, nullptr, out); }
void AIBrain::ChooseTarget(int* partAndObject, void* candidates, const Vector3* from, int a, int b) {
    // 005D0030 reads `a` as a byte.
    attention.Choose(this, partAndObject, static_cast<AIObjectList*>(candidates), from, static_cast<std::uint8_t>(a) != 0, b);
}

// --- The corpse list (005D8D50) ---------------------------------------------------------------

// RemoveHead of the corpse list returning the element as a counted reference (A-W12). The original
// copies the head's object into a counted local, unlinks the node, destroys the element in place
// (0052CEC0 inlined: released, cleared), frees the node (the last one frees every block), then
// copies the local into `out` and drops the local.
AIRef* __fastcall CorpsesPopHead(CorpseList* list, void*, AIRef* out) {
    CorpseList::Node* old = list->head;
    CEngineObject* value = old->data;
    if (value) ++value->refCount_;
    list->head = old->next;
    if (list->head) list->head->prev = nullptr;
    else list->tail = nullptr;
    if (old->data) old->data->Release();
    old->data = nullptr;
    old->next = list->free;
    list->free = old;
    if (--list->count == 0) list->RemoveAll();   // the list is empty: nothing left to destroy
    out->object = nullptr;
    if (value) {
        ++value->refCount_;
        out->object = value;
    }
    if (value) value->Release();
    return out;
}
EI_REWRITTEN(0x005D8D50, CorpsesPopHead);

// --- Think ---------------------------------------------------------------------------------------

void AIBrain::Think() {
    // An alarm counting down since the brain became alerted.
    if (alerted && alarmCountdown > 0) {
        if (--alarmCountdown == 0 && alarm != 0) {
            TheServerWorld()->InvokeAlarm(alarm, &owner->unit->position.x, 0);
            owner->voice.Raise(kVoiceAlarm);
        }
    }
    decay = (decay * kDecayScale + kOne) * kDecayDivisor;
    memory.Update(owner);

    CUnit* unit = owner->unit;
    memory.seesEnemy = 0;
    for (auto* node = memory.visible.head; node;) {
        CUnit* other = node->data;
        node = node->next;
        if (unit->IsHostileTo(reinterpret_cast<CUnit*>(other)) && IsGone(other) == 0) {
            memory.seesEnemy = 1;
            break;
        }
    }

    if (owner && owner->state == kControllerStateInactive) return;
    if (memory.seesEnemy && !alerted && (alertMode == 0 || alertMode == 2)) alerted = 1;

    // Choose the mode: the one in charge keeps it with a bonus of 100.
    AIMode* best = nullptr;
    int bestPriority = 1;
    if (current) {
        int priority = current->Evaluate(this) + 100;
        if (priority > 1) {
            best = current;
            bestPriority = priority;
        }
    }
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode == current) continue;
        int priority = mode->Evaluate(this);
        if (priority > bestPriority) {
            bestPriority = priority;
            best = mode;
        }
    }

    if (best != current) {
        if (best) {
            mfc::CString text;
            mfc::CString name;
            if (const char* modeName = ModeName(best->GetType())) name = modeName;
            text.Format(MotivationFormat(), owner->unit->id_, static_cast<const char*>(name),
                        bestPriority);
            GameOstream* console = ConsoleStream();
            console->Write(text)->Put('\n')->Apply(libcrt::FlushManipulator);
            int type = best->GetType();
            if (type >= kModeSuspection && (type <= kModeCorpseWatcher || type == kModeFear) && alertMode == 2 && !alerted)
                alerted = 1;
        }
        if (current) current->Leave(this);
        if (best) best->Enter(this);
        AIMode* old = current;
        current = best;
        if (best) ++best->refs;
        if (old) old->Release();
    }

    if (current) {
        current->Execute(this);
    } else {
        ReleaseRef(target);
        action = kActionNone;
    }
    ReleaseRef(seen);

    // The corpses seen this tick are forgotten (RemoveAll with element destruction).
    for (auto* node = memory.corpses.head; node; node = node->next) DestructRefs(&node->data, 1);
    memory.corpses.count = 0;
    memory.corpses.free = nullptr;
    memory.corpses.tail = nullptr;
    memory.corpses.head = nullptr;
    mfc::PlexFreeDataChain(memory.corpses.blocks);
    memory.corpses.blocks = nullptr;

    switch (action) {
    case kActionNone:
        ReleaseRef(target);
        owner->Idle();
        return;
    case kActionWalk:
        ReleaseRef(target);
        if (owner->action.kind == 1 || owner->action2.kind == 1 || owner->action.kind == 2 || owner->action2.kind == 2) return;
        action = kActionNone;
        return;
    case kActionMove:
        ReleaseRef(target);
        UpdateMove();
        return;
    case kActionAttack:
        UpdateAttack();
        return;
    case kActionCast:
        if (owner->action.kind == 4 || owner->action.kind == 5 || owner->action2.kind == 4 || owner->action2.kind == 5) return;
        if (target && owner->orderStopped) memory.reports.Report(target, 0xF);
        ReleaseRef(target);
        action = kActionNone;
        return;
    case kActionUse:
        ReleaseRef(target);
        if (owner->action.kind == 6 || owner->action2.kind == 6) return;
        action = kActionNone;
        return;
    case kActionFollow:
        UpdateFollow();
        return;
    default:
        return;
    }
}
EI_REWRITTEN(0x005D53B0, AIBrain::Think);

// --- Mode lookups --------------------------------------------------------------------------------

AIMode* AIBrain::FindMode(int type) {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == type) return mode;
    }
    return nullptr;
}
EI_REWRITTEN(0x005D5DD0, AIBrain::FindMode);

AIMode* AIBrain::FindPatrolMode() {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModePatrol) return mode;
    }
    return nullptr;
}
EI_REWRITTEN(0x005D5E30, AIBrain::FindPatrolMode);

AIMode* AIBrain::FindPlayerMode() {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModePlayer) return mode;
    }
    return nullptr;
}
EI_REWRITTEN(0x005D5E60, AIBrain::FindPlayerMode);

CUnit* AIBrain::PlayerModeLeader() {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModePlayer) return mode ? static_cast<PlayerMode*>(mode)->leader : nullptr;
    }
    return nullptr;
}
EI_REWRITTEN(0x005D5EE0, AIBrain::PlayerModeLeader);

CEngineObject* AIBrain::LeaderOrFollowed() {
    CEngineObject* leader = nullptr;
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModePlayer) {
            if (mode) leader = static_cast<PlayerMode*>(mode)->leader;
            break;
        }
    }
    if (!leader && action == kActionFollow) return target;
    return leader;
}
EI_REWRITTEN(0x005D5E90, AIBrain::LeaderOrFollowed);

CEngineObject* AIBrain::AggressionEnemy() {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModeAggression) {
            if (mode && current == mode) return static_cast<AggressionMode*>(mode)->enemy;
            return nullptr;
        }
    }
    return nullptr;
}
EI_REWRITTEN(0x005D5F20, AIBrain::AggressionEnemy);

// --- Positions -----------------------------------------------------------------------------------

bool AIBrain::IsNearPath(const Vector3* point, float radius) {
    float radiusSq = radius * radius;       // FSTP float
    {
        // Kept on the FPU stack in the original (double holds the 24-bit results exactly).
        double dx = position.x - point->x;
        double dy = position.y - point->y;
        EI_NAN_AUDIT(dy, dx, radiusSq);
        if (LengthSquared2DUnrounded(dy, dx) < radiusSq) return true;
    }
    // The unit has not moved -> not near.
    EI_NAN_AUDIT(position.x, previousPosition.x, position.y, previousPosition.y);
    if (position.x == previousPosition.x && position.y == previousPosition.y) return false;
    float pointX = point->x;
    float pointY = point->y;
    float segX = previousPosition.x - position.x;     // FSTP float [EBP+0xC]
    float segY = previousPosition.y - position.y;     // FSTP float [EBP+8]
    float t = static_cast<float>(((pointX - position.x) * segX + (pointY - position.y) * segY) /
                                          LengthSquared2DUnrounded<double>(segY, segX));
    // Clamp to [0, 1] with the original's selects: a NaN t gives 1.
    float clamped = 0.0f >= t ? 0.0f : static_cast<float>(t);
    float clampedStored = clamped;                     // FLD/FSTP through memory
    float u = 1.0f >= clampedStored ? static_cast<float>(clampedStored) : 1.0f;
    double ex = position.x - (pointX - u * segX);
    double ey = position.y - (pointY - u * segY);
    EI_NAN_AUDIT(ey, ex);
    return LengthSquared2DUnrounded(ey, ex) < radiusSq;
}
EI_REWRITTEN(0x005D50E0, AIBrain::IsNearPath);

std::uint32_t AIBrain::SetOwner(CUnitServer* newOwner) {
    if (newOwner == owner) return reinterpret_cast<std::uint32_t>(newOwner);
    owner = newOwner;
    constexpr std::uint32_t kUnset = 0xC4A42000u;      // -1313.0
    std::memcpy(&position.x, &kUnset, 4);
    std::memcpy(&previousPosition.x, &kUnset, 4);
    std::memset(&position.y, 0, 4);
    std::memset(&previousPosition.y, 0, 4);
    std::memset(&position.z, 0, 4);
    std::memset(&previousPosition.z, 0, 4);
    return kUnset;
}
EI_REWRITTEN(0x005D5080, AIBrain::SetOwner);

void AIBrain::OnObjectDeleted(CEngineObject* object) {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        mode->OnObjectDeleted(object);
    }
    if (target == object && target) ReleaseRef(target);
    if (seen == object && seen) ReleaseRef(seen);
    memory.Forget(object);
}
EI_REWRITTEN(0x005D58D0, AIBrain::OnObjectDeleted);

// --- Actions -------------------------------------------------------------------------------------

void AIBrain::UpdateMove() {
    CUnitServer* o = owner;
    if (o->action.kind == 1 || o->action2.kind == 1 || o->action.kind == 2 || o->action2.kind == 2) return;
    if (o->orderStopped) {
        CUnit* unit = o->unit;
        double dy = actionPoint.y - unit->position.y;     // kept on the FPU stack
        double dx = actionPoint.x - unit->position.x;
        // TEST AH,0x41; JNZ: continue below when <= or unordered.
        if (LengthSquared2DUnrounded(dx, dy) > kArrivedDistanceSq) {
            if (target) reinterpret_cast<AIRef*>(&target)->Reset();
            target = nullptr;
            action = kActionNone;
            return;
        }
    }
    if (actionTime == kNoTime) {
        if (looks.size == 0) {
            if (target) {
                reinterpret_cast<AIRef*>(&target)->Reset();
                target = nullptr;
            }
            action = kActionNone;
            return;
        }
        actionTime = kLookStarted;
        AILook* look = looks.data;
        double angle = look->angle;
        Vector3 direction;
        direction.z = 0.0f;
        direction.x = static_cast<float>(x87::fcos(angle));
        direction.y = static_cast<float>(x87::fsin(angle));
        o->OrderTurn(&direction.x, std::bit_cast<std::uint32_t>(look->time));   // the float's bits as the time (sic)
        return;
    }
    if (actionTime == kLookStarted) {
        if (looks.data->stopFirst) o->StopQueued();
        float end = static_cast<float>(CurrentWorldTick()) + looks.data->wait;     // FILD + FADD, FSTP float
        actionTime = x87::fistp(end);
        int remaining = looks.size - 1;
        if (remaining != 0) libcrt::Memmove(looks.data, looks.data + 1, static_cast<unsigned>(remaining) << 4);
        --looks.size;
        return;
    }
    if (CurrentWorldTick() > actionTime) actionTime = kNoTime;
}
EI_REWRITTEN(0x005D7520, AIBrain::UpdateMove);

void AIBrain::UpdateAttack() {
    if (!target) {
        action = kActionNone;
        return;
    }
    CUnitServer* o = owner;
    bool giveUp = false;
    if (o->unit->player != nullptr) {      // 005301A0
        if (IsGone(target) != 0) {
            action = kActionNone;
            return;
        }
    } else if (IsGone(target) != 0) {
        giveUp = true;
    } else if (target->GetClassId() == kClassUnit && memory.IsEnemy(owner->unit, target) == 0) {
        giveUp = true;
    }
    if (giveUp) {
        if (IsGone(target) == 0) memory.Notice(&static_cast<CMapObject*>(target)->position, 1000, -2);
        action = kActionNone;
        return;
    }
    o = owner;
    if (o->action.kind == 3 && o->action.target == target) return;
    if (o->action2.kind == 3 && o->action2.target == target) return;
    if (o->action.kind == 4 || o->action.kind == 5) return;
    if (o->action2.kind == 4 || o->action2.kind == 5) return;
    action = kActionNone;
}
EI_REWRITTEN(0x005D76A0, AIBrain::UpdateAttack);

void AIBrain::StopAction() {
    ReleaseRef(target);
    action = kActionNone;
}
EI_REWRITTEN(0x005D7790, AIBrain::StopAction);

namespace {
// Holds `object` in `ref` with a counted reference (the inlined assignment of the original).
void HoldRef(CEngineObject*& ref, CEngineObject* object) {
    if (object == ref) return;
    if (ref) ref->Release();
    ref = nullptr;
    if (object) ++object->refCount_;
    ref = object;
}

} // namespace

void AIBrain::AttackTarget(CEngineObject* object) {
    action = kActionAttack;
    HoldRef(target, object);
    int flags = 0x1C;
    if (owner->unit->player) {
        AIOrder order{};
        order.kind = 3;
        order.what = 6;
        order.object = nullptr;
        order.flag = 0;
        if (object) {
            order.object = object;
            ++object->refCount_;
        }
        if (owner->CanReachAndPlan(AsUnitAction(&order))) flags = -1;
        if (order.object) order.object->Release();
        order.object = nullptr;
    }
    owner->OrderAttack(target, 6, 0, flags);
}
EI_REWRITTEN(0x005D7B50, AIBrain::AttackTarget);

void AIBrain::AttackWith(CEngineObject* object, int how, int unknown) {
    if (owner->unit->player == nullptr && object->GetClassId() == kClassUnit && memory.IsEnemy(owner->unit, object) == 0) return;
    action = kActionAttack;
    HoldRef(target, object);
    owner->OrderAttack(target, how, unknown, 2);
}
EI_REWRITTEN(0x005D7C60, AIBrain::AttackWith);

void AIBrain::UseOn(CEngineObject* object, int kind, int a, int b) {
    if (!object) return;
    action = kActionUse;
    ReleaseRef(target);
    switch (kind) {
    case 0:
        owner->OrderUse(object, a, b);
        return;
    case 1:
        if (object->GetClassId() == kClassUnit) owner->OrderSteal(object, b);
        return;
    case 2:
        if (object->GetClassId() == kClassUnit) owner->OrderTame(object, b);
        return;
    case 0x18A95:
        if (object->GetClassId() == kClassUnit) owner->OrderSearchBody(object, b);
        return;
    default:
        return;
    }
}
EI_REWRITTEN(0x005D7CE0, AIBrain::UseOn);

void AIBrain::Walk(const Vector3* point, int run, int unknown) {
    action = kActionWalk;
    owner->OrderMove(&point->x, run, unknown, 1000000.0f);    // 0x49742400
}
EI_REWRITTEN(0x005D7AE0, AIBrain::Walk);

void AIBrain::TurnTowards(const Vector3* direction, float time) {
    action = kActionWalk;
    owner->OrderTurn(&direction->x, std::bit_cast<std::uint32_t>(time));   // the float's bits as the time (sic)
}
EI_REWRITTEN(0x005D7B30, AIBrain::TurnTowards);

void AIBrain::CastAt(int spell, const Vector3* point, int run) {
    if (!owner->IsScrollSource(spell)) action = kActionCast;
    actionTime = spell;
    owner->OrderCastAt(spell, &point->x, run);
}
EI_REWRITTEN(0x005D7DA0, AIBrain::CastAt);

void AIBrain::Follow(CEngineObject* object) {
    action = kActionFollow;
    HoldRef(target, object);
}
EI_REWRITTEN(0x005D7E40, AIBrain::Follow);

namespace {
const std::uint32_t* Diplomacy() { return ei::GamePointer<const std::uint32_t>(0x007C2BC4); }
} // namespace

void AIBrain::SetSeen(CEngineObject* object) {
    HoldRef(seen, object);
    if (object) {
        bool allied = false;
        if (seen && seen->GetClassId() == kClassUnit) {
            std::uint32_t bit = 1u << (static_cast<CUnit*>(seen)->side & 31);
            allied = (Diplomacy()[owner->unit->side] & bit) != 0;
        }
        if (!allied) CallAllies(&static_cast<CMapObject*>(object)->position);
    } else {
        CallAllies(&owner->unit->position);
    }
    if (!alerted && alertMode == 1) alerted = 1;
    if (object && object->GetClassId() == kClassUnit) memory.ForceSee(owner->unit, static_cast<CUnit*>(object));
}
EI_REWRITTEN(0x005D7E90, AIBrain::SetSeen);
