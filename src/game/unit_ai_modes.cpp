// Unit AI modes (005D0000-005DC000): the simple virtual slots of Guard, Patrol, Sentry, Follow,
// Suspection, Aggression, CorpseWatcher, Fear and Player. The registered addresses are the vtable
// targets, so every mode object of the game runs these.
#include "unit_ai.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>
#include "originals_library.h"

#pragma float_control(precise, on)
#pragma warning(disable : 4740) // naked forwarders

namespace {
constexpr int kPriority = 50;
constexpr int kNoPriority = -1000;
constexpr float kZero = 0.0f;                                           // 0073B858
// 0073CCB0 (a double, 0.4 / 2^32) and 0073CCA8 (0.8f): the new alertness decay.
constexpr double kDecayRandomScale = std::bit_cast<double>(0x3DD99999C019999Aull);
constexpr float kDecayBase = std::bit_cast<float>(0x3F4CCCCDu);
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

constexpr float kMoveTime = 1000000.0f;                                 // 0x49742400
constexpr float kNearX = 35.0f;                                         // 0073FEF8
constexpr float kNearY = 80.0f;                                         // 0073FEF4
constexpr float kHalf = 0.5f;                                           // 0073B86C
constexpr float kNoWander = -1.0f;                                      // 0xBF800000
constexpr float kStandRadius = 5.0f;                                    // 0x40A00000
constexpr float kArrivedSq = std::bit_cast<float>(0x3E4CCCCDu);         // 0073BAD4 (0.2)
constexpr float kCloseSq = 25.0f;                                       // 0073E124
constexpr std::uint32_t kUnsetBits = 0xC4A42000u;                       // -1312: no position yet
constexpr int kRemarkIdle = 1;

// The idle remark every mode's Execute starts with (1 in 1001).
void MaybeRemark(AIBrain* brain, int what) {
    if (GameRandom()->Next() % 1001 == 1) brain->owner->Say(what);
}

// An order on the stack: the looks array is constructed and given its derived vtable.
void ConstructOrder(AIMoveOrder* order) {
    order->looks.Construct();
    order->looks.vtable = reinterpret_cast<void*>(kLookArrayVtable);
}

// The inlined destructor of the order's looks array.
void DestroyLooks(AIMoveOrder* order) {
    order->looks.vtable = reinterpret_cast<void*>(kLookArrayBaseVtable);
    if (order->looks.data) gamecrt::operator_delete(order->looks.data);
}

// Common tail of the moves: drop the target, action 2 to the order's point with its looks.
void StartMove(AIBrain* brain, AIMoveOrder* order) {
    if (brain->target) {
        reinterpret_cast<AIRef*>(&brain->target)->Reset();
        brain->target = nullptr;
    }
    brain->action = AIBrain::kActionMove;
    brain->actionTime = AIBrain::kNoTime;
    std::memcpy(&brain->actionPoint, &order->point, sizeof(Vector3));
    brain->looks.Copy(&order->looks);
    brain->owner->MoveTo(&brain->actionPoint, 0, 0, kMoveTime);
}

// The brain's remembered positions after choosing a new place to be.
void SetPositions(AIBrain* brain, const Vector3* where, float nearX, float nearY) {
    brain->unknown_44 = nearX;
    brain->unknown_48 = nearY;
    std::memcpy(&brain->previousPosition, &brain->position, sizeof(Vector3));
    std::memcpy(&brain->position, where, sizeof(Vector3));
    std::uint32_t previous;
    std::memcpy(&previous, &brain->previousPosition.x, 4);
    if (previous == kUnsetBits) std::memcpy(&brain->previousPosition, where, sizeof(Vector3));
}

// The static candidate list of Aggression::Evaluate (007C54C8, guard bit 0 of 007C54E4).
struct CandidateList {
    void Construct(int blockSize);                  // 004C96A0
};
__declspec(naked) void CandidateList::Construct(int) EI_ORIGINAL(0x004C96A0)
constexpr std::uintptr_t kCandidates = 0x007C54C8;
constexpr std::uintptr_t kCandidatesGuard = 0x007C54E4;
constexpr std::uintptr_t kCandidatesDestructor = 0x005D2570;

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

void GuardMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, kRemarkIdle);
    brain->LookAround(0);
    if (brain->action != AIBrain::kActionNone) return;
    // 35 + radius / 80 + radius, computed before the positions are copied.
    float nearX = kNearX + radius;
    float nearY = kNearY + radius;
    SetPositions(brain, &point, nearX, nearY);

    AIMoveOrder order;
    ConstructOrder(&order);
    order.Reset();
    if (!brain->FindStandPoint(&point, radius, &order.point)) {
        if (++failures <= 100) {
            DestroyLooks(&order);
            return;
        }
        if (!brain->FindStandPoint(reinterpret_cast<const Vector3*>(&brain->owner->unit->positionX), radius, &order.point)) {
            order.looks.Destroy();
            return;
        }
    }
    failures = 0;
    // Look away from the guarded point, scaled to half the radius.
    volatile float dx = order.point.x - point.x;    // FSTP float each
    volatile float dy = order.point.y - point.y;
    volatile float dz = order.point.z - point.z;
    Vector3 direction;
    direction.x = dx;
    direction.y = dy;
    direction.z = dz;
    // FCOMP 0; TEST AH,0x40: a radius equal to 0 (or unordered) leaves the direction unscaled.
    if (radius < kZero || radius > kZero) {
        double scale = x87::fsqrt(static_cast<double>(dz * dz + dy * dy) + static_cast<double>(dx * dx)) / radius * kHalf;
        direction.x = static_cast<float>(dx * scale);
        direction.y = static_cast<float>(direction.y * scale);
        direction.z = static_cast<float>(direction.z * scale);
    }
    bool far = GameRandom()->Next() % 11 == 0;
    brain->PlanLooks(&order, &direction, wander, far);
    StartMove(brain, &order);
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D0BE0, GuardMode::Execute);

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

void PatrolMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, kRemarkIdle);
    brain->LookAround(0);
    if (pointCount == 0 || brain->action != AIBrain::kActionNone) return;
    if (++current >= pointCount) current = 0;
    PatrolPoint* next = points + current;
    AIMoveOrder order;
    std::memcpy(&order.point, &next->position, sizeof(Vector3));
    ConstructOrder(&order);
    order.looks.SetSize(next->looks.size, -1);
    for (int i = 0; i < next->looks.size; ++i) {
        AILook look;
        next->looks.GetAt(&look, i);
        std::memcpy(&order.looks.data[i], &look, sizeof(AILook));
    }
    if (order.looks.size <= 0) {
        Vector3 none{0.0f, 0.0f, 0.0f};
        brain->PlanLooks(&order, &none, kNoWander, false);
    }
    StartMove(brain, &order);
    SetPositions(brain, &order.point, kNearX, kNearY);
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D1700, PatrolMode::Execute);

PatrolPoint* PatrolMode::LastPoint() {
    if (pointCount == 0) return nullptr;
    return points + (pointCount - 1);
}
EI_REPLACE_METHOD(0x005D1690, PatrolMode::LastPoint);

// --- Sentry --------------------------------------------------------------------------------------

void SentryMode::Set(const Vector3* p) { std::memcpy(&point, p, sizeof(Vector3)); }
EI_REPLACE_METHOD(0x005D1B50, SentryMode::Set);

void SentryMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, kRemarkIdle);
    brain->LookAround(0);
    if (brain->action != AIBrain::kActionNone) return;
    AIMoveOrder order;
    ConstructOrder(&order);
    order.looks.SetSize(0, -1);
    std::memcpy(&order.point, &point, sizeof(Vector3));
    bool far = GameRandom()->Next() % 11 == 0;
    Vector3 none{0.0f, 0.0f, 0.0f};
    // 007C53FC / 007C5400: the wander radii of a far and a near look-around.
    float wanderRadius = far ? GlobalFloat(0x007C53FC) : GlobalFloat(0x007C5400);
    brain->PlanLooks(&order, &none, wanderRadius, far);
    StartMove(brain, &order);
    SetPositions(brain, &order.point, kNearX, kNearY);
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D1B70, SentryMode::Execute);

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

void SuspectionMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone) {
        Vector3 going;
        std::memcpy(&going, &brain->actionPoint, sizeof(Vector3));
        double dy = point.y - going.y;      // kept on the FPU stack
        double dx = point.x - going.x;
        // TEST AH,0x41: jump to a new move only when the distance is greater.
        if (!(dx * dx + dy * dy > kArrivedSq)) {
            // Still going to this point: near it, the interest fades.
            const float* unit = &brain->owner->unit->positionX;
            double ux = unit[0] - point.x;
            double uy = unit[1] - point.y;
            double uz = unit[2] - point.z;
            if (!(uz * uz + uy * uy + ux * ux >= kCloseSq)) level -= 5;
            return;
        }
    }
    AIMoveOrder order;
    ConstructOrder(&order);
    order.Reset();
    if (brain->action == AIBrain::kActionNone) {
        if (brain->owner->inCombat) level -= 50;
        if (!brain->FindStandPoint(&point, kStandRadius, &order.point)) {
            level -= 5;
            order.looks.Destroy();
            return;
        }
    } else {
        std::memcpy(&order.point, &point, sizeof(Vector3));
    }
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, &none, kNoWander, false);
    reinterpret_cast<AIRef*>(&brain->target)->Assign(nullptr);
    brain->action = AIBrain::kActionMove;
    brain->actionTime = AIBrain::kNoTime;
    std::memcpy(&brain->actionPoint, &order.point, sizeof(Vector3));
    brain->looks.Copy(&order.looks);
    brain->owner->MoveTo(&brain->actionPoint, 0, 0, kMoveTime);
    level = brain->noticedLevel;
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D1F80, SuspectionMode::Execute);

void SuspectionMode::Enter(AIBrain* brain) {
    brain->owner->busy = 1;
    AIMoveOrder order;
    ConstructOrder(&order);
    order.point = Vector3{0.0f, 0.0f, 0.0f};
    order.looks.SetSize(0, -1);
    std::memcpy(&order.point, &point, sizeof(Vector3));
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, &none, kNoWander, false);
    StartMove(brain, &order);
    brain->owner->Say(5);
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D21A0, SuspectionMode::Enter);

void SuspectionMode::Leave(AIBrain* brain) {
    brain->owner->busy = 0;
    brain->owner->Say(3);
}
EI_REPLACE_METHOD(0x005D2310, SuspectionMode::Leave);

int AggressionMode::Evaluate(AIBrain* brain) {
    chosen = nullptr;
    if (fixed) {
        if (enemy && IsGone(enemy) != 0) {
            reinterpret_cast<AIRef*>(&enemy)->Assign(nullptr);
            return kNoPriority;
        }
        if (!enemy) {
            if (CEngineObject* seen = brain->seen) {
                bool allied = false;
                if (seen->GetClassId() == kClassUnit) {
                    std::uint32_t bit = 1u << (static_cast<CUnit*>(brain->seen)->side & 31);
                    allied = (Diplomacy()[brain->owner->unit->side] & bit) != 0;
                }
                if (!allied && seen != brain->owner->unit && !brain->IsReported(seen))
                    reinterpret_cast<AIRef*>(&enemy)->Assign(seen);
            }
        }
        if (!enemy) return kNoPriority;
        if (IsGone(enemy) == 0 && enemy->GetClassId() == kClassUnit) {
            if (brain->IsEnemy(brain->owner->unit, enemy) == 0) return kNoPriority;
            enemyPart = -1;
            chosen = enemy;
            return chosen ? 10000 : kNoPriority;
        }
        if (enemy) {
            reinterpret_cast<AIRef*>(&enemy)->Reset();
            enemy = nullptr;
        }
        return kNoPriority;
    }
    if (!brain->seesEnemy) return kNoPriority;
    auto* guard = reinterpret_cast<std::uint8_t*>(kCandidatesGuard);
    if (!(*guard & 1)) {
        *guard |= 1;
        reinterpret_cast<CandidateList*>(kCandidates)->Construct(10);
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(kCandidatesDestructor));
    }
    CUnit* self = brain->owner->unit;
    brain->CollectEnemies(self, reinterpret_cast<void*>(kCandidates));
    brain->ChooseTarget(&enemyPart, reinterpret_cast<void*>(kCandidates), reinterpret_cast<const Vector3*>(&self->positionX), 0, 0);
    CEngineObject* best = chosen;
    if (best != enemy) {
        if (enemy) enemy->Release();
        enemy = nullptr;
        if (best) ++best->refCount_;
        enemy = best;
    }
    return chosen ? 10000 : kNoPriority;
}
EI_REPLACE_METHOD(0x005D2390, AggressionMode::Evaluate);

void AggressionMode::Execute(AIBrain* brain) {
    if (GameRandom()->Next() % 1001 == 1) brain->owner->Say(2);
    if (!chosen) return;
    brain->CallAllies(reinterpret_cast<const Vector3*>(reinterpret_cast<char*>(chosen) + 0x1C));
    int attack[2] = {-1, 0};        // {part, object} the brain is attacking now
    brain->CurrentAttack(attack);
    if (reinterpret_cast<CEngineObject*>(attack[1]) == chosen && attack[0] == enemyPart) return;
    if (!chosen) return;
    attack[1] = 0;
    attack[0] = -1;
    brain->CurrentAttack(attack);
    CEngineObject* object = chosen;
    if (object == reinterpret_cast<CEngineObject*>(attack[1]) && enemyPart == attack[0]) return;
    int part = enemyPart;
    if (part == -1) {
        brain->AttackTarget(object);
        return;
    }
    if (!brain->owner->CanCast(part)) brain->action = AIBrain::kActionCast;
    if (object != brain->target) {
        if (brain->target) brain->target->Release();
        brain->target = nullptr;
        if (object) ++object->refCount_;
        brain->target = object;
    }
    brain->actionTime = part;
    brain->owner->Cast(part, object, 0);
}
EI_REPLACE_METHOD(0x005D2580, AggressionMode::Execute);

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

__declspec(naked) void PlayerMode::UpdateCombat(AIBrain*) EI_ORIGINAL(0x005D4170)
__declspec(naked) bool PlayerMode::StaysWithParty(AIBrain*, bool) EI_ORIGINAL(0x005D4860)
__declspec(naked) void UnitController::Shout(int) EI_ORIGINAL(0x00552780)

void PlayerMode::Execute(AIBrain* brain) {
    CUnit* unit = brain->owner->unit;
    // The player controls this unit directly (player + 0x20, +0x5120 == 2).
    bool direct = false;
    if (unit->player) direct = *reinterpret_cast<const int*>(*reinterpret_cast<char* const*>(unit->player + 0x20) + 0x5120) == 2;
    if (brain->seen && unit->unknown_438 != 1) brain->owner->Shout(0x20);

    switch (state) {
    case kStateFollow: {
        if (!leader && brain->action == AIBrain::kActionFollow) {
            reinterpret_cast<AIRef*>(&brain->target)->Assign(nullptr);
            brain->action = AIBrain::kActionNone;
        }
        int action = brain->action;
        if (action == AIBrain::kActionAttack && !target) {
            UpdateCombat(brain);
            return;
        }
        if (action != AIBrain::kActionNone && (!leader || action != AIBrain::kActionFollow)) return;
        CEngineObject* who = leader;
        if (!who) {
            UpdateCombat(brain);
            return;
        }
        if (!direct) {
            brain->action = AIBrain::kActionFollow;
            if (who != brain->target) {
                reinterpret_cast<AIRef*>(&brain->target)->Reset();
                if (who) ++who->refCount_;
                brain->target = who;
            }
        }
        if (!leader) {
            UpdateCombat(brain);
            return;
        }
        // While the leader itself carries out a command (its Player mode in state 1), only follow.
        auto* leaderBrain = reinterpret_cast<AIBrain*>(reinterpret_cast<char*>(static_cast<CUnit*>(leader)->controller) + 0x980);
        for (auto* node = leaderBrain->modes.head; node;) {
            AIMode* mode = node->data;     // 005D9830 (GetNext)
            node = node->next;
            if (mode->GetType() == kModePlayer) {
                if (mode && static_cast<PlayerMode*>(mode)->state == kStateCommand) return;
                UpdateCombat(brain);
                return;
            }
        }
        UpdateCombat(brain);
        return;
    }
    case kStateCommand:
        if (brain->action != AIBrain::kActionNone) return;
        break;
    case kStateMoveTo: {
        constexpr std::uint32_t kOneBits = 0x3F800000u;
        std::uint32_t walking;
        std::memcpy(&walking, &point.z, 4);
        if (walking == kOneBits) {
            if (brain->action == AIBrain::kActionNone) {
                state = kStateFollow;
                ReleaseRef(brain->target);
                brain->action = AIBrain::kActionNone;
            } else if (!direct && !StaysWithParty(brain, true)) {
                brain->owner->Shout(0x21);
                std::memset(&point.z, 0, 4);
                ReleaseRef(brain->target);
                brain->action = AIBrain::kActionNone;
            }
        } else if (direct || StaysWithParty(brain, false)) {
            std::memcpy(&point.z, &kOneBits, 4);
            Vector3 to;
            std::memcpy(&to.x, &point.x, 4);
            std::memcpy(&to.y, &point.y, 4);
            std::memcpy(&to.z, &kUnsetBits, 4);
            brain->action = AIBrain::kActionWalk;
            brain->owner->MoveTo(&to, 0, 0, kMoveTime);
        }
        if (state == kStateMoveTo) UpdateCombat(brain);
        return;
    }
    default:
        break;
    }
    state = kStateFollow;
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
}
EI_REPLACE_METHOD(0x005D4A90, PlayerMode::Execute);

__declspec(naked) bool PlayerMode::PathClear(AIBrain*, const Vector3*, const Vector3*) EI_ORIGINAL(0x005D37E0)

namespace {
// Does the player control the unit directly (player + 0x20, +0x5120 == 2)?
bool DirectControl(const AIBrain* brain) {
    const char* player = brain->owner->unit->player;
    return player && *reinterpret_cast<const int*>(*reinterpret_cast<char* const*>(player + 0x20) + 0x5120) == 2;
}

// Common start of CommandWalk / CommandGoTo: the point (moved by `offset` when the way is clear),
// the run flag, and the references dropped.
void PrepareWalk(PlayerMode* self, AIBrain* brain, const Vector3* to, int run, const Vector3* offset) {
    std::memcpy(&self->point, to, sizeof(Vector3));
    Vector3 shifted;
    shifted.x = to->x + offset->x;      // FSTP float each
    shifted.y = to->y + offset->y;
    shifted.z = to->z + offset->z;
    if (self->PathClear(brain, to, &shifted)) {
        self->point.x = offset->x + self->point.x;
        self->point.y = offset->y + self->point.y;
        self->point.z = offset->z + self->point.z;
    }
    self->run = static_cast<std::uint8_t>(run);
    if (!DirectControl(brain)) ReleaseRef(self->leader);
    ReleaseRef(self->target);
}
} // namespace

void PlayerMode::CommandWalk(AIBrain* brain, const Vector3* to, int unknown, int runArg, const Vector3* offset) {
    state = kStateCommand;
    PrepareWalk(this, brain, to, runArg, offset);
    brain->action = AIBrain::kActionWalk;
    brain->owner->MoveTo(&point, run, unknown, kMoveTime);
}
EI_REPLACE_METHOD(0x005D39E0, PlayerMode::CommandWalk);

void PlayerMode::CommandGoTo(AIBrain* brain, const Vector3* to, int unknown, int runArg, const Vector3* offset) {
    state = kStateMoveTo;
    PrepareWalk(this, brain, to, runArg, offset);
    if (!DirectControl(brain) && !StaysWithParty(brain, true)) {
        std::memset(&point.z, 0, 4);
        brain->owner->TurnTo(&point, unknown, 0, kMoveTime);
        return;
    }
    constexpr std::uint32_t kOneBits = 0x3F800000u;
    std::memcpy(&point.z, &kOneBits, 4);
    brain->action = AIBrain::kActionWalk;
    brain->owner->MoveTo(&point, run, unknown, kMoveTime);
}
EI_REPLACE_METHOD(0x005D3AE0, PlayerMode::CommandGoTo);

void PlayerMode::CommandAttack(AIBrain* brain, CEngineObject* object, int how, int unknown) {
    if (object == brain->owner->unit) return;
    state = kStateFollow;
    if (object != target) {
        if (target) target->Release();
        target = nullptr;
        if (object) ++object->refCount_;
        target = object;
    }
    if (brain->owner->unit->player == nullptr && object->GetClassId() == kClassUnit &&
        brain->IsEnemy(brain->owner->unit, object) == 0)
        return;
    brain->action = AIBrain::kActionAttack;
    if (object != brain->target) {
        if (brain->target) brain->target->Release();
        brain->target = nullptr;
        if (object) ++object->refCount_;
        brain->target = object;
    }
    brain->owner->Attack(brain->target, how, unknown, 2);
}
EI_REPLACE_METHOD(0x005D3C30, PlayerMode::CommandAttack);

void PlayerMode::CommandUse(AIBrain* brain, CEngineObject* object, int kind, int a, int b) {
    state = kStateFollow;
    brain->UseOn(object, kind, a, b);
}
EI_REPLACE_METHOD(0x005D3CF0, PlayerMode::CommandUse);

void PlayerMode::CommandCast(AIBrain* brain, int spell, CEngineObject* object, int) {
    std::uint8_t running = run;
    state = kStateFollow;
    if (!brain->owner->CanCast(spell)) brain->action = AIBrain::kActionCast;
    if (object != brain->target) {
        if (brain->target) brain->target->Release();
        brain->target = nullptr;
        if (object) ++object->refCount_;
        brain->target = object;
    }
    brain->actionTime = spell;
    brain->owner->Cast(spell, object, running);
}
EI_REPLACE_METHOD(0x005D3D20, PlayerMode::CommandCast);

void PlayerMode::CommandCastAt(AIBrain* brain, int spell, const Vector3* at, int) {
    std::uint8_t running = run;
    state = kStateFollow;
    if (!brain->owner->CanCast(spell)) brain->action = AIBrain::kActionCast;
    brain->actionTime = spell;
    brain->owner->CastAt(spell, at, running);
}
EI_REPLACE_METHOD(0x005D3D90, PlayerMode::CommandCastAt);

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
