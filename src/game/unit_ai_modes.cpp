// Unit AI modes (005D0000-005DC000): the simple virtual slots of Guard, Patrol, Sentry, Follow,
// Suspection, Aggression, CorpseWatcher, Fear and Player. The registered addresses are the vtable
// targets, so every mode object of the game runs these.
#include "unit_ai.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <bit>
#include <cstring>

#pragma float_control(precise, on)
#pragma warning(disable : 4740) // naked forwarders

namespace {
constexpr int kPriority = 50;
constexpr int kNoPriority = -1000;
constexpr float kZero = 0.0f;                                           // 0073B858
// 0073CCB0 (a double, 0.4 / 2^32) and 0073CCA8 (0.8f): the new alertness decay.
constexpr double kDecayRandomScale = std::bit_cast<double>(0x3DD99999C019999Aull);
constexpr float kDecayBase = std::bit_cast<float>(0x3F4CCCCDu);
constexpr std::uintptr_t kWorld = 0x007AFF60;
// Diplomacy: one mask of allied sides per side (007C2BC4).
const std::uint32_t* Diplomacy() { return reinterpret_cast<const std::uint32_t*>(0x007C2BC4); }

IsaacRandom* GameRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

void ReleaseRef(CEngineObject*& ref) {
    if (ref) {
        ref->Release();
        ref = nullptr;
    }
}

int IsGone(CEngineObject* object) { return ei::VCall<int>(object, 0xDC / 4); }

// Copies a float through FLD/FSTP as the original does (a signalling NaN would be quietened).
void FpuCopy(float* dst, const float* src) {
    __asm {
        mov eax, src
        mov ecx, dst
        fld dword ptr [eax]
        fstp dword ptr [ecx]
    }
}

template <typename T>
void ReadField(AIStream* stream, T* field, unsigned size = sizeof(T)) { stream->Read(field, size); }

// The original's Save functions copy each scalar into a stack slot before writing it.
template <typename T>
void WriteCopy(AIStream* stream, const T& value, unsigned size = sizeof(T)) {
    T copy = value;
    stream->Write(&copy, size);
}

float GlobalFloat(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
} // namespace

// --- Shared slots --------------------------------------------------------------------------------

int __fastcall aimode::Priority50(AIMode*, void*, AIBrain*) { return kPriority; }
EI_REPLACE(0x005D0A80, aimode::Priority50);

void __fastcall aimode::EnterIdle(AIMode*, void*, AIBrain* brain) {
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
    std::uint32_t roll = GameRandom()->Next();
    volatile std::int64_t wide = roll;             // FILD qword of the zero-extended value
    brain->decay = static_cast<float>(static_cast<double>(wide) * kDecayRandomScale + kDecayBase);
    brain->owner->busy = 0;
}
EI_REPLACE(0x005D0AF0, aimode::EnterIdle);

void __fastcall aimode::ClearAction(AIMode*, void*, AIBrain* brain) {
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
}
EI_REPLACE(0x005D0A50, aimode::ClearAction);

// --- Guard ---------------------------------------------------------------------------------------

void GuardMode::Set(const Vector3* p, float newRadius, float newWander) {
    std::memcpy(&point, p, sizeof(Vector3));
    failures = 0;
    // FCOMP 0; TEST AH,1: below zero (or NaN) takes the default.
    if (!(newRadius >= kZero))
        std::memcpy(&radius, reinterpret_cast<const void*>(0x007C540C), 4);
    else
        std::memcpy(&radius, &newRadius, 4);
    if (!(newWander >= kZero))
        std::memcpy(&wander, reinterpret_cast<const void*>(0x007C53FC), 4);
    else
        std::memcpy(&wander, &newWander, 4);
}
EI_REPLACE_METHOD(0x005D0B70, GuardMode::Set);

void GuardMode::Load(AIStream* stream) {
    ReadField(stream, &radius);
    ReadField(stream, &wander);
    ReadField(stream, &point);
    if (stream->version >= 0x66) ReadField(stream, &failures);
}
EI_REPLACE_METHOD(0x005D0EB0, GuardMode::Load);

void GuardMode::Save(AIStream* stream) {
    WriteCopy(stream, radius);
    WriteCopy(stream, wander);
    stream->Write(&point, sizeof(Vector3));
    WriteCopy(stream, failures);
}
EI_REPLACE_METHOD(0x005D0F00, GuardMode::Save);

// --- Patrol --------------------------------------------------------------------------------------

__declspec(naked) void PatrolMode::AddPoint(const Vector3*) EI_ORIGINAL(0x005D12B0)

int PatrolMode::Evaluate(AIBrain* brain) {
    if (pointCount != 0) return kPriority;
    // 00791DEC: "Unit %i has no patrol points, switching to guard" (passed by address, as the original).
    game::FatalError(reinterpret_cast<const char*>(0x00791DEC), brain->owner->unit->id_);
    AddPoint(reinterpret_cast<const Vector3*>(&brain->owner->unit->positionX));
    return kNoPriority;
}
EI_REPLACE_METHOD(0x005D16B0, PatrolMode::Evaluate);

PatrolPoint* PatrolMode::LastPoint() {
    if (pointCount == 0) return nullptr;
    return points + (pointCount - 1);
}
EI_REPLACE_METHOD(0x005D1690, PatrolMode::LastPoint);

// --- Sentry --------------------------------------------------------------------------------------

void SentryMode::Set(const Vector3* p) { std::memcpy(&point, p, sizeof(Vector3)); }
EI_REPLACE_METHOD(0x005D1B50, SentryMode::Set);

void SentryMode::Load(AIStream* stream) { stream->Read(&point, sizeof(Vector3)); }
EI_REPLACE_METHOD(0x005D1DA0, SentryMode::Load);

void SentryMode::Save(AIStream* stream) { stream->Write(&point, sizeof(Vector3)); }
EI_REPLACE_METHOD(0x005D1DC0, SentryMode::Save);

// --- Follow --------------------------------------------------------------------------------------

void FollowMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone) return;
    CEngineObject* who = leader;
    if (!who) return;
    brain->action = AIBrain::kActionFollow;
    if (who == brain->target) return;
    if (brain->target) brain->target->Release();
    brain->target = nullptr;
    ++who->refCount_;
    brain->target = who;
}
EI_REPLACE_METHOD(0x005D1E10, FollowMode::Execute);

void FollowMode::OnObjectDeleted(CEngineObject* object) {
    if (leader == object && leader) ReleaseRef(leader);
}
EI_REPLACE_METHOD(0x005D1DE0, FollowMode::OnObjectDeleted);

void FollowMode::Save(AIStream* stream) { reinterpret_cast<AIRef*>(&leader)->Save(stream); }
EI_REPLACE_METHOD(0x005D1E80, FollowMode::Save);

// --- Suspection ----------------------------------------------------------------------------------

int SuspectionMode::Evaluate(AIBrain* brain) {
    if (CEngineObject* seen = brain->seen) {
        bool allied = false;
        if (seen->GetClassId() == kClassUnit) {
            std::uint32_t bit = 1u << (static_cast<CUnit*>(brain->seen)->side & 31);
            allied = (Diplomacy()[brain->owner->unit->side] & bit) != 0;
        }
        if (!allied) {
            Vector3 where;
            std::memcpy(&where, reinterpret_cast<const char*>(brain->seen) + 0x1C, sizeof(Vector3));   // position
            brain->Notice(&where, 1000, -2);
        }
    }
    // TEST AH,0x40: equal or unordered.
    auto same = [](float a, float b) { return !(a < b) && !(a > b); };
    if (same(brain->noticedX, point.x) && same(brain->noticedY, point.y)) {
        int value = level;
        if (!(value < brain->noticedLevel)) value = brain->noticedLevel;
        level = value;
        return value;
    }
    level = brain->noticedLevel;
    FpuCopy(&point.x, &brain->noticedX);
    FpuCopy(&point.y, &brain->noticedY);
    point.z = 0.0f;
    return level;
}
EI_REPLACE_METHOD(0x005D1E90, SuspectionMode::Evaluate);

void SuspectionMode::Leave(AIBrain* brain) {
    brain->owner->busy = 0;
    brain->owner->Say(3);
}
EI_REPLACE_METHOD(0x005D2310, SuspectionMode::Leave);

void SuspectionMode::Load(AIStream* stream) {
    stream->Read(&point, sizeof(Vector3));
    stream->Read(&level, 4);
}
EI_REPLACE_METHOD(0x005D2330, SuspectionMode::Load);

void SuspectionMode::Save(AIStream* stream) {
    stream->Write(&point, sizeof(Vector3));
    WriteCopy(stream, level);
}
EI_REPLACE_METHOD(0x005D2360, SuspectionMode::Save);

// --- Aggression ----------------------------------------------------------------------------------

void AggressionMode::Enter(AIBrain* brain) {
    brain->owner->busy = 1;
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
    brain->owner->Say(6);
}
EI_REPLACE_METHOD(0x005D26D0, AggressionMode::Enter);

void AggressionMode::Leave(AIBrain* brain) {
    if (!enemy || IsGone(enemy) != 0) {
        brain->noticedLevel = 0;
        brain->noticedKind = 0;
    }
    ReleaseRef(enemy);
    brain->owner->busy = 0;
    brain->owner->Say(4);
}
EI_REPLACE_METHOD(0x005D2710, AggressionMode::Leave);

void AggressionMode::Save(AIStream* stream) {
    reinterpret_cast<AIRef*>(&enemy)->Save(stream);
    stream->Write(&fixed, 1);
}
EI_REPLACE_METHOD(0x005D27A0, AggressionMode::Save);

// --- CorpseWatcher -------------------------------------------------------------------------------

int CorpseWatcherMode::Evaluate(AIBrain* brain) {
    if (brain->corpses.count != 0) {
        AIRef corpse;
        AIRef* popped = brain->PopCorpse(&corpse);
        brain->Notice(reinterpret_cast<const Vector3*>(reinterpret_cast<char*>(popped->object) + 0x1C), 800, -3);
        if (corpse.object) corpse.object->Release();
    }
    return kNoPriority;
}
EI_REPLACE_METHOD(0x005D27D0, CorpseWatcherMode::Evaluate);

// --- Fear ----------------------------------------------------------------------------------------

void FearMode::Enter(AIBrain* brain) {
    brain->owner->busy = 1;
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
}
EI_REPLACE_METHOD(0x005D3650, FearMode::Enter);

void FearMode::Leave(AIBrain* brain) { brain->owner->busy = 0; }
EI_REPLACE_METHOD(0x005D3680, FearMode::Leave);

void FearMode::Load(AIStream* stream) {
    ReadField(stream, &distance);
    ReadField(stream, &unknown_0C);
    ReadField(stream, &unknown_10);
    if (stream->version >= 0x53) {
        ReadField(stream, &flag);
        if (stream->version >= 0x70) ReadField(stream, &unknown_1C);
    }
}
EI_REPLACE_METHOD(0x005D3690, FearMode::Load);

void FearMode::Save(AIStream* stream) {
    WriteCopy(stream, distance);
    WriteCopy(stream, unknown_0C);
    stream->Write(&unknown_10, sizeof(Vector3));
    WriteCopy(stream, flag);
    stream->Write(&unknown_1C, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x005D36F0, FearMode::Save);

// --- Player --------------------------------------------------------------------------------------

void PlayerMode::OnObjectDeleted(CEngineObject* object) {
    if (target == object && target) ReleaseRef(target);
    if (leader == object && leader) ReleaseRef(leader);
}
EI_REPLACE_METHOD(0x005D37A0, PlayerMode::OnObjectDeleted);

void PlayerMode::Save(AIStream* stream) {
    reinterpret_cast<AIRef*>(&target)->Save(stream);
    reinterpret_cast<AIRef*>(&leader)->Save(stream);
    stream->Write(&point, sizeof(Vector3));
    stream->Write(&state, 4);
}
EI_REPLACE_METHOD(0x005D4DB0, PlayerMode::Save);
