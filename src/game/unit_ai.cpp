// The unit AI brain (005D0000-005DC000): the per-tick mode selection and the actions.
#include "unit_ai.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <bit>
#include <cstring>

// Keep the original's order of float operations (/fp:fast would reassociate constants).
#pragma float_control(precise, on)
#pragma warning(disable : 4740) // naked forwarders

namespace {
constexpr float kDecayScale = 30.0f;                                    // 0073BAF0
constexpr float kOne = 1.0f;                                            // 0073B84C
constexpr float kDecayDivisor = std::bit_cast<float>(0x3D042108u);      // 0073FFC0 (1 / 31)
constexpr float kArrivedDistanceSq = 1.0f;                              // 0073FEF0

constexpr int kControllerStateInactive = 5;
constexpr int kVoiceAlarm = 8;
constexpr std::uintptr_t kWorld = 0x007AFF60;
constexpr std::uintptr_t kGameTime = 0x007AFFC4;
constexpr std::uintptr_t kConsole = 0x007C54E8;     // the console's ostream*

int GameTime() { return *reinterpret_cast<const int*>(kGameTime); }

// CEngineObject::Release through the object's vtable, then clear the slot (inlined everywhere).
void ReleaseRef(CEngineObject*& ref) {
    if (ref) {
        ref->Release();
        ref = nullptr;
    }
}

// Virtual slot 55 (0xDC) of units and objects: non-zero when dead / gone.
int IsGone(CEngineObject* object) { return ei::VCall<int>(object, 0xDC / 4); }

// The game's ostream (MSVC 6 iostream): operator<< for strings, chars and manipulators.
struct GameOstream {
    GameOstream* Write(const char* text);           // 006FC31D
    GameOstream* Put(int c);                        // 006FC0B0 (operator<<(char))
    GameOstream* Apply(void* manipulator);          // 00434FE0
};
__declspec(naked) GameOstream* GameOstream::Write(const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) GameOstream* GameOstream::Put(int) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) GameOstream* GameOstream::Apply(void*) EI_ORIGINAL(0x00434FE0)
constexpr std::uintptr_t kFlushManipulator = 0x00435000;

// The world (007AFF60): 00593FC0 raises an alarm of `kind` at `position`.
struct AIWorld {
    void RaiseAlarm(int kind, const Vector3* position, int unused);
};
__declspec(naked) void AIWorld::RaiseAlarm(int, const Vector3*, int) EI_ORIGINAL(0x00593FC0)

// 0052CEC0: DestructElements of counted references (stdcall).
__declspec(naked) void __stdcall DestructRefs(CEngineObject**, int) EI_ORIGINAL(0x0052CEC0)

// 006E79A0: the game's memmove.
__declspec(naked) void* __cdecl GameMemmove(void*, const void*, unsigned) EI_ORIGINAL(0x006E79A0)

// The controller's voice (controller + 0xB10): 005AEA60 says remark `what`.
struct AIVoice {
    void Say(int what);
};
__declspec(naked) void AIVoice::Say(int) EI_ORIGINAL(0x005AEA60)

// The mode names of the "new motivation" message (the original pushes these addresses).
const char* ModeName(int type) {
    switch (type) {
    case kModeGuard: return reinterpret_cast<const char*>(0x0078D354);          // "Guard"
    case kModePatrol: return reinterpret_cast<const char*>(0x00791E8C);         // "Patrol"
    case kModeSentry: return reinterpret_cast<const char*>(0x0078D0CC);         // "Sentry"
    case kModeSuspection: return reinterpret_cast<const char*>(0x00791E80);     // "Suspection"
    case kModeAggression: return reinterpret_cast<const char*>(0x00791E74);     // "Aggression"
    case kModeCorpseWatcher: return reinterpret_cast<const char*>(0x00791E64);  // "CorpseWatcher"
    case kModePlayer: return reinterpret_cast<const char*>(0x00791E54);         // "Player"
    case kModeFear: return reinterpret_cast<const char*>(0x00791E5C);           // "Fear"
    case kModeTamed: return reinterpret_cast<const char*>(0x00791E4C);          // "Tamed"
    case kModeFollow: return reinterpret_cast<const char*>(0x0078D458);         // "Follow"
    default: return nullptr;    // 9 and anything else: the name stays empty
    }
}
// 00791E20: "Unit %i new motivation %s with power of %i".
const char* MotivationFormat() { return reinterpret_cast<const char*>(0x00791E20); }
} // namespace

// --- Forwarders ----------------------------------------------------------------------------------

__declspec(naked) void UnitController::Idle() EI_ORIGINAL(0x00555180)
__declspec(naked) void UnitController::StopQueued() EI_ORIGINAL(0x00555200)
__declspec(naked) void UnitController::Turn(const Vector3*, float) EI_ORIGINAL(0x005546B0)
void UnitController::Say(int what) {
    reinterpret_cast<AIVoice*>(reinterpret_cast<char*>(this) + 0xB10)->Say(what);
}

__declspec(naked) void AIMode::Release() EI_ORIGINAL(0x0040A3A0)
int AIMode::Evaluate(AIBrain* brain) { return ei::VCall<int>(this, 4, brain); }
void AIMode::Execute(AIBrain* brain) { ei::VCall<void>(this, 5, brain); }
void AIMode::Enter(AIBrain* brain) { ei::VCall<void>(this, 6, brain); }
void AIMode::Leave(AIBrain* brain) { ei::VCall<void>(this, 7, brain); }
int AIMode::GetType() { return ei::VCall<int>(this, 8); }
void AIMode::OnObjectDeleted(CEngineObject* object) { ei::VCall<void>(this, 10, object); }

__declspec(naked) void AIRef::Reset() EI_ORIGINAL(0x0052B030)
__declspec(naked) void AIRef::Assign(CEngineObject*) EI_ORIGINAL(0x0052A8F0)
__declspec(naked) void AIRef::Load(AIStream*, void*) EI_ORIGINAL(0x0050E220)
__declspec(naked) void AIRef::Save(AIStream*) EI_ORIGINAL(0x0050E290)

__declspec(naked) void AIBrain::UpdateFollow() EI_ORIGINAL(0x005D6F90)

// The perception memory at +0xD0, the report list at +0x164 and the corpse list at +0x148 are
// separate objects; these forwarders pass the sub-object as `this`.
namespace {
struct AIMemory {
    void Update(UnitController* owner);                 // 005CB700
    void Notice(const Vector3* point, int level, int kind); // 005CBBE0
    int IsEnemy(CUnit* self, CEngineObject* other);     // 005CB7F0
    void Forget(CEngineObject* object);                 // 005CC020
};
__declspec(naked) void AIMemory::Update(UnitController*) EI_ORIGINAL(0x005CB700)
__declspec(naked) void AIMemory::Notice(const Vector3*, int, int) EI_ORIGINAL(0x005CBBE0)
__declspec(naked) int AIMemory::IsEnemy(CUnit*, CEngineObject*) EI_ORIGINAL(0x005CB7F0)
__declspec(naked) void AIMemory::Forget(CEngineObject*) EI_ORIGINAL(0x005CC020)

struct AIReports {
    void Add(CEngineObject* object, int what);          // 005CC620
};
__declspec(naked) void AIReports::Add(CEngineObject*, int) EI_ORIGINAL(0x005CC620)

struct AICorpses {
    AIRef* PopHead(AIRef* out);                         // 005D8D50
};
__declspec(naked) AIRef* AICorpses::PopHead(AIRef*) EI_ORIGINAL(0x005D8D50)

AIMemory* MemoryOf(AIBrain* brain) { return reinterpret_cast<AIMemory*>(brain->memory); }
} // namespace

void AIBrain::Perceive(UnitController* who) { MemoryOf(this)->Update(who); }
void AIBrain::Notice(const Vector3* point, int level, int kind) { MemoryOf(this)->Notice(point, level, kind); }
int AIBrain::IsEnemy(CUnit* self, CEngineObject* other) { return MemoryOf(this)->IsEnemy(self, other); }
void AIBrain::Forget(CEngineObject* object) { MemoryOf(this)->Forget(object); }
void AIBrain::Report(CEngineObject* object, int what) {
    reinterpret_cast<AIReports*>(reports)->Add(object, what);
}
AIRef* AIBrain::PopCorpse(AIRef* out) { return reinterpret_cast<AICorpses*>(&corpses)->PopHead(out); }

// --- Think ---------------------------------------------------------------------------------------

void AIBrain::Think() {
    // An alarm counting down since the brain became alerted.
    if (alerted && alarmCountdown > 0) {
        if (--alarmCountdown == 0 && alarm != 0) {
            reinterpret_cast<AIWorld*>(kWorld)->RaiseAlarm(alarm, reinterpret_cast<Vector3*>(&owner->unit->positionX), 0);
            owner->Say(kVoiceAlarm);
        }
    }
    decay = (decay * kDecayScale + kOne) * kDecayDivisor;
    Perceive(owner);

    CUnit* unit = owner->unit;
    seesEnemy = 0;
    for (auto* node = visible.head; node;) {
        CEngineObject* other = node->data;
        node = node->next;
        if (unit->IsHostileTo(reinterpret_cast<CUnit*>(other)) && IsGone(other) == 0) {
            seesEnemy = 1;
            break;
        }
    }

    if (owner && owner->state == kControllerStateInactive) return;
    if (seesEnemy && !alerted && (alertMode == 0 || alertMode == 2)) alerted = 1;

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
            GameOstream* console = *reinterpret_cast<GameOstream**>(kConsole);
            console->Write(text)->Put('\n')->Apply(reinterpret_cast<void*>(kFlushManipulator));
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
    for (auto* node = corpses.head; node; node = node->next) DestructRefs(&node->data, 1);
    corpses.count = 0;
    corpses.free = nullptr;
    corpses.tail = nullptr;
    corpses.head = nullptr;
    mfc::PlexFreeDataChain(corpses.blocks);
    corpses.blocks = nullptr;

    switch (action) {
    case kActionNone:
        ReleaseRef(target);
        owner->Idle();
        return;
    case kActionWalk:
        ReleaseRef(target);
        if (owner->command == 1 || owner->nextCommand == 1 || owner->command == 2 || owner->nextCommand == 2) return;
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
        if (owner->command == 4 || owner->command == 5 || owner->nextCommand == 4 || owner->nextCommand == 5) return;
        if (target && owner->inCombat) Report(target, 0xF);
        ReleaseRef(target);
        action = kActionNone;
        return;
    case kActionUse:
        ReleaseRef(target);
        if (owner->command == 6 || owner->nextCommand == 6) return;
        action = kActionNone;
        return;
    case kActionFollow:
        UpdateFollow();
        return;
    default:
        return;
    }
}
EI_REPLACE_METHOD(0x005D53B0, AIBrain::Think);

// --- Mode lookups --------------------------------------------------------------------------------

AIMode* AIBrain::FindMode(int type) {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == type) return mode;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x005D5DD0, AIBrain::FindMode);

AIMode* AIBrain::FindPatrolMode() {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModePatrol) return mode;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x005D5E30, AIBrain::FindPatrolMode);

AIMode* AIBrain::FindPlayerMode() {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModePlayer) return mode;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x005D5E60, AIBrain::FindPlayerMode);

CEngineObject* AIBrain::PlayerModeLeader() {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        if (mode->GetType() == kModePlayer) return mode ? static_cast<PlayerMode*>(mode)->leader : nullptr;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x005D5EE0, AIBrain::PlayerModeLeader);

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
EI_REPLACE_METHOD(0x005D5E90, AIBrain::LeaderOrFollowed);

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
EI_REPLACE_METHOD(0x005D5F20, AIBrain::AggressionEnemy);

// --- Positions -----------------------------------------------------------------------------------

bool AIBrain::IsNearPath(const Vector3* point, float radius) {
    float radiusSq = radius * radius;       // FSTP float
    {
        // Kept on the FPU stack in the original (double holds the 24-bit results exactly).
        double dx = position.x - point->x;
        double dy = position.y - point->y;
        // FCOMP; TEST AH,1: "less or unordered".
        if (!(dy * dy + dx * dx >= radiusSq)) return true;
    }
    // TEST AH,0x40 twice: the unit has not moved (equal or unordered) -> not near.
    if (!(position.x < previousPosition.x) && !(position.x > previousPosition.x) && !(position.y < previousPosition.y) &&
        !(position.y > previousPosition.y))
        return false;
    float pointX = point->x;
    float pointY = point->y;
    volatile float segX = previousPosition.x - position.x;     // FSTP float [EBP+0xC]
    volatile float segY = previousPosition.y - position.y;     // FSTP float [EBP+8]
    volatile float t = ((pointX - position.x) * segX + (pointY - position.y) * segY) / (segY * segY + segX * segX);
    // Clamp to [0, 1] with the original's FCOMP/mask selects (C0: "less or unordered"): t is kept
    // when 0 < t or unordered, else 0; then 1 is taken when 1 < t or unordered.
    float clamped = !(0.0f >= t) ? static_cast<float>(t) : 0.0f;
    volatile float clampedStored = clamped;                     // FLD/FSTP through memory
    float u = !(1.0f >= clampedStored) ? 1.0f : static_cast<float>(clampedStored);
    double ex = position.x - (pointX - u * segX);
    double ey = position.y - (pointY - u * segY);
    return !(ey * ey + ex * ex >= radiusSq);
}
EI_REPLACE_METHOD(0x005D50E0, AIBrain::IsNearPath);

std::uint32_t AIBrain::SetOwner(UnitController* newOwner) {
    if (newOwner == owner) return reinterpret_cast<std::uint32_t>(newOwner);
    owner = newOwner;
    constexpr std::uint32_t kUnset = 0xC4A42000u;      // -1312.0
    std::memcpy(&position.x, &kUnset, 4);
    std::memcpy(&previousPosition.x, &kUnset, 4);
    std::memset(&position.y, 0, 4);
    std::memset(&previousPosition.y, 0, 4);
    std::memset(&position.z, 0, 4);
    std::memset(&previousPosition.z, 0, 4);
    return kUnset;
}
EI_REPLACE_METHOD(0x005D5080, AIBrain::SetOwner);

void AIBrain::OnObjectDeleted(CEngineObject* object) {
    for (auto* node = modes.head; node;) {
        AIMode* mode = node->data;
        node = node->next;
        mode->OnObjectDeleted(object);
    }
    if (target == object && target) ReleaseRef(target);
    if (seen == object && seen) ReleaseRef(seen);
    Forget(object);
}
EI_REPLACE_METHOD(0x005D58D0, AIBrain::OnObjectDeleted);

// --- Actions -------------------------------------------------------------------------------------

void AIBrain::UpdateMove() {
    UnitController* o = owner;
    if (o->command == 1 || o->nextCommand == 1 || o->command == 2 || o->nextCommand == 2) return;
    if (o->inCombat) {
        CUnit* unit = o->unit;
        double dy = actionPoint.y - unit->positionY;     // kept on the FPU stack
        double dx = actionPoint.x - unit->positionX;
        // TEST AH,0x41; JNZ: continue below when <= or unordered.
        if (dx * dx + dy * dy > kArrivedDistanceSq) {
            if (target) reinterpret_cast<AIRef*>(&target)->Reset();
            target = nullptr;
            action = kActionNone;
            return;
        }
    }
    if (actionTime == kNoTime) {
        if (lookCount == 0) {
            if (target) {
                reinterpret_cast<AIRef*>(&target)->Reset();
                target = nullptr;
            }
            action = kActionNone;
            return;
        }
        actionTime = kLookStarted;
        AILook* look = looks;
        double angle = look->angle;
        Vector3 direction;
        direction.z = 0.0f;
        direction.x = static_cast<float>(x87::fcos(angle));
        direction.y = static_cast<float>(x87::fsin(angle));
        o->Turn(&direction, look->time);
        return;
    }
    if (actionTime == kLookStarted) {
        if (looks->stopFirst) o->StopQueued();
        volatile float end = static_cast<float>(GameTime()) + looks->wait;     // FILD + FADD, FSTP float
        actionTime = x87::fistp(end);
        int remaining = lookCount - 1;
        if (remaining != 0) GameMemmove(looks, looks + 1, static_cast<unsigned>(remaining) << 4);
        --lookCount;
        return;
    }
    if (GameTime() > actionTime) actionTime = kNoTime;
}
EI_REPLACE_METHOD(0x005D7520, AIBrain::UpdateMove);

void AIBrain::UpdateAttack() {
    if (!target) {
        action = kActionNone;
        return;
    }
    UnitController* o = owner;
    bool giveUp = false;
    if (o->unit->player != nullptr) {      // 005301A0
        if (IsGone(target) != 0) {
            action = kActionNone;
            return;
        }
    } else if (IsGone(target) != 0) {
        giveUp = true;
    } else if (target->GetClassId() == kClassUnit && IsEnemy(owner->unit, target) == 0) {
        giveUp = true;
    }
    if (giveUp) {
        if (IsGone(target) == 0) Notice(reinterpret_cast<Vector3*>(reinterpret_cast<char*>(target) + 0x1C), 1000, -2);
        action = kActionNone;
        return;
    }
    o = owner;
    if (o->command == 3 && o->commandTarget == target) return;
    if (o->nextCommand == 3 && o->nextTarget == target) return;
    if (o->command == 4 || o->command == 5) return;
    if (o->nextCommand == 4 || o->nextCommand == 5) return;
    action = kActionNone;
}
EI_REPLACE_METHOD(0x005D76A0, AIBrain::UpdateAttack);

void AIBrain::StopAction() {
    ReleaseRef(target);
    action = kActionNone;
}
EI_REPLACE_METHOD(0x005D7790, AIBrain::StopAction);
