// Unit AI modes (005D0000-005DC000): the simple virtual slots of Guard, Patrol, Sentry, Follow,
// Suspection, Aggression, CorpseWatcher, Fear and Player. The registered addresses are the vtable
// targets, so every mode object of the game runs these.
#include "combat.h"
#include "game_data.h"
#include "unit_ai.h"
#include "at_exit_functions.h"

#include "player_messages.h"
#include "random.h"
#include "script.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>
#include "originals_library.h"
#include "originals_ai.h"
#include "originals_units.h"
#include "game_singletons.h"
#include "lib/mfc.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"
#include "lib/nan_audit.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {
constexpr int kPriority = 50;
constexpr int kNoPriority = -1000;
constexpr float kZero = 0.0f;                                           // 0073B858
// 0073CCB0 (a double, 0.4 / 2^32) and 0073CCA8 (0.8f): the new alertness decay.
constexpr double kDecayRandomScale = std::bit_cast<double>(0x3DD99999C019999Aull);
constexpr float kDecayBase = std::bit_cast<float>(0x3F4CCCCDu);
// Diplomacy: one mask of allied sides per side (007C2BC4).
const std::uint32_t* Diplomacy() { return ei::GamePointer<const std::uint32_t>(0x007C2BC4); }


// The server side of a unit's player (CPlayer::connection_, a CPlayerServer here; CUnit::player is
// kept as bytes in unit.h). Its refreshPending_ == 2 is what the modes take as the player
// controlling the unit directly.
inline const CPlayerServer* PlayerServerOf(const char* player) {
    return reinterpret_cast<const CPlayerServer*>(reinterpret_cast<const CPlayer*>(player)->connection_);
}

void ReleaseRef(CEngineObject*& ref) {
    if (ref) {
        ref->Release();
        ref = nullptr;
    }
}

int IsGone(CEngineObject* object) { return ObjectSlots(object)->IsDead(); }

template <typename T>
void ReadField(CStream* stream, T* field, unsigned size = sizeof(T)) { stream->Read(field, size); }

// The original's Save functions copy each scalar into a stack slot before writing it.
template <typename T>
void WriteCopy(CStream* stream, const T& value, unsigned size = sizeof(T)) {
    T copy = value;
    stream->Write(&copy, size);
}

float GlobalFloat(std::uintptr_t address) { return ei::GameData<const float>(address); }

constexpr float kMoveTime = 1000000.0f;                                 // 0x49742400
constexpr float kNearX = 35.0f;                                         // 0073FEF8
constexpr float kNearY = 80.0f;                                         // 0073FEF4
constexpr float kHalf = 0.5f;                                           // 0073B86C
constexpr float kNoWander = -1.0f;                                      // 0xBF800000
constexpr float kStandRadius = 5.0f;                                    // 0x40A00000
constexpr float kArrivedSq = std::bit_cast<float>(0x3E4CCCCDu);         // 0073BAD4 (0.2)
constexpr float kCloseSq = 25.0f;                                       // 0073E124
constexpr std::uint32_t kUnsetBits = 0xC4A42000u;                       // -1313: no position yet
constexpr int kRemarkIdle = 1;

// The idle remark every mode's Execute starts with (1 in 1001).
void MaybeRemark(AIBrain* brain, int what) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(what);
}

// An order on the stack: the looks array is constructed and given its derived vtable.
void ConstructOrder(AIMoveOrder* order) {
    LooksInit(&order->looks);
    order->looks.vtable = mfc::impl::AiLookRecordArrayClass::Vtable();
}

// The inlined destructor of the order's looks array.
void DestroyLooks(AIMoveOrder* order) {
    order->looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
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
    brain->looks.Copy(order->looks);
    brain->owner->OrderMove(&brain->actionPoint.x, 0, 0, kMoveTime);
}

// The brain's remembered positions after choosing a new place to be.
void SetPositions(AIBrain* brain, const Vector3* where, float nearX, float nearY) {
    brain->nearX = nearX;
    brain->nearY = nearY;
    std::memcpy(&brain->previousPosition, &brain->position, sizeof(Vector3));
    std::memcpy(&brain->position, where, sizeof(Vector3));
    std::uint32_t previous;
    std::memcpy(&previous, &brain->previousPosition.x, 4);
    if (previous == kUnsetBits) std::memcpy(&brain->previousPosition, where, sizeof(Vector3));
}

// The static candidate list of Aggression::Evaluate (007C54C8, guard bit 0 of 007C54E4): a CPtrList
// built by 004C96A0.
mfc::CList<void*>& Candidates() { return ei::GameData<mfc::CList<void*>>(0x007C54C8); }
std::uint8_t& CandidatesGuard() { return ei::GameData<std::uint8_t>(0x007C54E4); }

} // namespace

// --- Shared slots --------------------------------------------------------------------------------

int AIMode::Priority50(AIBrain*) { return kPriority; }
EI_REPLACE_METHOD(0x005D0A80, AIMode::Priority50);

void AIMode::EnterIdle(AIBrain* brain) {
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
    std::uint32_t roll = IsaacRandom::Global().Next();
    volatile std::int64_t wide = roll;             // FILD qword of the zero-extended value
    brain->decay = static_cast<float>(static_cast<double>(wide) * kDecayRandomScale + kDecayBase);
    brain->owner->SetBusy(0);
}
EI_REPLACE_METHOD(0x005D0AF0, AIMode::EnterIdle);

void AIMode::ClearAction(AIBrain* brain) {
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
}
EI_REPLACE_METHOD(0x005D0A50, AIMode::ClearAction);

// --- Guard ---------------------------------------------------------------------------------------

void GuardMode::Set(const Vector3* p, float newRadius, float newWander) {
    std::memcpy(&point, p, sizeof(Vector3));
    failures = 0;
    // Below zero (or NaN) takes the default.
    if (newRadius >= kZero)
        std::memcpy(&radius, &newRadius, 4);
    else
        std::memcpy(&radius, ei::GamePointer<const void>(0x007C540C), 4);
    if (newWander >= kZero)
        std::memcpy(&wander, &newWander, 4);
    else
        std::memcpy(&wander, ei::GamePointer<const void>(0x007C53FC), 4);
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
    order.Clear();
    if (!brain->FindStandPoint(&point, radius, &order.point)) {
        if (++failures <= 100) {
            DestroyLooks(&order);
            return;
        }
        if (!brain->FindStandPoint(&brain->owner->unit->position, radius, &order.point)) {
            LooksDestroyAt(&order.looks);
            return;
        }
    }
    failures = 0;
    // Look away from the guarded point, scaled to half the radius.
    float dx = order.point.x - point.x;    // FSTP float each
    float dy = order.point.y - point.y;
    float dz = order.point.z - point.z;
    Vector3 direction;
    direction.x = dx;
    direction.y = dy;
    direction.z = dz;
    // A radius of 0 leaves the direction unscaled.
    EI_NAN_AUDIT(radius);
    if (radius != kZero) {
        double scale = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz)) / radius * kHalf;
        direction.x = static_cast<float>(dx * scale);
        direction.y = static_cast<float>(direction.y * scale);
        direction.z = static_cast<float>(direction.z * scale);
    }
    bool farLook = IsaacRandom::Global().Next() % 11 == 0;
    brain->PlanLooks(&order, &direction, wander, farLook);
    StartMove(brain, &order);
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D0BE0, GuardMode::Execute);

void GuardMode::Load(CStream* stream) {
    ReadField(stream, &radius);
    ReadField(stream, &wander);
    ReadField(stream, &point);
    if (stream->version_ >= 0x66) ReadField(stream, &failures);
}
EI_REPLACE_METHOD(0x005D0EB0, GuardMode::Load);

void GuardMode::Save(CStream* stream) {
    WriteCopy(stream, radius);
    WriteCopy(stream, wander);
    stream->Write(&point, sizeof(Vector3));
    WriteCopy(stream, failures);
}
EI_REPLACE_METHOD(0x005D0F00, GuardMode::Save);

// --- Patrol --------------------------------------------------------------------------------------

void PatrolMode::AddPoint(const Vector3* a0) { AppendPoint(a0); }

int PatrolMode::Evaluate(AIBrain* brain) {
    if (points.size != 0) return kPriority;
    // 00791DEC: "Unit %i has no patrol points, switching to guard" (passed by address, as the original).
    game::FatalError("Unit %i has no patrol points, switching to guard", brain->owner->unit->id_);
    AddPoint(&brain->owner->unit->position);
    return kNoPriority;
}
EI_REPLACE_METHOD(0x005D16B0, PatrolMode::Evaluate);

void PatrolMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, kRemarkIdle);
    brain->LookAround(0);
    if (points.size == 0 || brain->action != AIBrain::kActionNone) return;
    if (++current >= points.size) current = 0;
    PatrolPoint* next = points.data + current;
    AIMoveOrder order;
    std::memcpy(&order.point, &next->position, sizeof(Vector3));
    ConstructOrder(&order);
    order.looks.SetSize(next->looks.size, -1);
    for (int i = 0; i < next->looks.size; ++i) {
        AILook look;
        LooksElementAt(&next->looks, nullptr, &look, i);
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
    if (points.size == 0) return nullptr;
    return points.data + (points.size - 1);
}
EI_REPLACE_METHOD(0x005D1690, PatrolMode::LastPoint);

void PatrolMode::Load(CStream* stream) {
    int count;
    ReadField(stream, &count);
    points.SetSize(count, -1);
    for (int i = 0; i < points.size; ++i) {
        PatrolPoint& point = points.data[i];
        ReadField(stream, &point.position);
        int lookCount;
        ReadField(stream, &lookCount);
        point.looks.SetSize(lookCount, -1);
        for (int j = 0; j < point.looks.size; ++j) {
            AILook& look = point.looks.data[j];
            ReadField(stream, &look.angle);
            ReadField(stream, &look.time);
            ReadField(stream, &look.wait);
            if (stream->version_ >= 0x44) ReadField(stream, &look.stopFirst);
        }
    }
    ReadField(stream, &current);
}
EI_REPLACE_METHOD(0x005D1930, PatrolMode::Load);

void PatrolMode::Save(CStream* stream) {
    WriteCopy(stream, points.size);
    for (int i = 0; i < points.size; ++i) {
        PatrolPoint& point = points.data[i];
        stream->Write(&point.position, sizeof(Vector3));
        WriteCopy(stream, point.looks.size);
        for (int j = 0; j < point.looks.size; ++j) {
            const AILook& look = point.looks.data[j];
            // Each float goes through a stack copy as a dword (integer moves in the original).
            WriteCopy(stream, std::bit_cast<std::uint32_t>(look.angle));
            WriteCopy(stream, std::bit_cast<std::uint32_t>(look.time));
            WriteCopy(stream, std::bit_cast<std::uint32_t>(look.wait));
            WriteCopy(stream, look.stopFirst);
        }
    }
    WriteCopy(stream, current);
}
EI_REPLACE_METHOD(0x005D1A40, PatrolMode::Save);

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
    bool farLook = IsaacRandom::Global().Next() % 11 == 0;
    Vector3 none{0.0f, 0.0f, 0.0f};
    // 007C53FC / 007C5400: the wander radii of a far and a near look-around.
    float wanderRadius = farLook ? GlobalFloat(0x007C53FC) : GlobalFloat(0x007C5400);
    brain->PlanLooks(&order, &none, wanderRadius, farLook);
    StartMove(brain, &order);
    SetPositions(brain, &order.point, kNearX, kNearY);
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D1B70, SentryMode::Execute);

void SentryMode::Load(CStream* stream) { stream->Read(&point, sizeof(Vector3)); }
EI_REPLACE_METHOD(0x005D1DA0, SentryMode::Load);

void SentryMode::Save(CStream* stream) { stream->Write(&point, sizeof(Vector3)); }
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

void FollowMode::Load(CStream* stream) { reinterpret_cast<AIRef*>(&leader)->Load(stream, TheServerWorld()); }
EI_REPLACE_METHOD(0x005D1E60, FollowMode::Load);

void FollowMode::Save(CStream* stream) { reinterpret_cast<AIRef*>(&leader)->Save(stream); }
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
            std::memcpy(&where, &static_cast<const CMapObject*>(brain->seen)->position, sizeof(Vector3));
            brain->Notice(&where, 1000, -2);
        }
    }
    EI_NAN_AUDIT(brain->noticedX, point.x, brain->noticedY, point.y);
    if (brain->noticedX == point.x && brain->noticedY == point.y) {
        int value = level;
        if (!(value < brain->noticedLevel)) value = brain->noticedLevel;
        level = value;
        return value;
    }
    level = brain->noticedLevel;
    std::memcpy(&point.x, &brain->noticedX, sizeof(float));
    std::memcpy(&point.y, &brain->noticedY, sizeof(float));
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
        EI_NAN_AUDIT(dx, dy);
        if (LengthSquared2DUnrounded(dx, dy) <= kArrivedSq) {
            // Still going to this point: near it, the interest fades.
            const float* unit = &brain->owner->unit->position.x;
            double ux = unit[0] - point.x;
            double uy = unit[1] - point.y;
            double uz = unit[2] - point.z;
            EI_NAN_AUDIT(ux, uy, uz);
            if (LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz) < kCloseSq) level -= 5;
            return;
        }
    }
    AIMoveOrder order;
    ConstructOrder(&order);
    order.Clear();
    if (brain->action == AIBrain::kActionNone) {
        if (brain->owner->orderStopped) level -= 50;
        if (!brain->FindStandPoint(&point, kStandRadius, &order.point)) {
            level -= 5;
            LooksDestroyAt(&order.looks);
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
    brain->looks.Copy(order.looks);
    brain->owner->OrderMove(&brain->actionPoint.x, 0, 0, kMoveTime);
    level = brain->noticedLevel;
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D1F80, SuspectionMode::Execute);

void SuspectionMode::Enter(AIBrain* brain) {
    brain->owner->SetBusy(1);
    AIMoveOrder order;
    ConstructOrder(&order);
    order.point = Vector3{0.0f, 0.0f, 0.0f};
    order.looks.SetSize(0, -1);
    std::memcpy(&order.point, &point, sizeof(Vector3));
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, &none, kNoWander, false);
    StartMove(brain, &order);
    brain->owner->voice.Raise(5);
    DestroyLooks(&order);
}
EI_REPLACE_METHOD(0x005D21A0, SuspectionMode::Enter);

void SuspectionMode::Leave(AIBrain* brain) {
    brain->owner->SetBusy(0);
    brain->owner->voice.Raise(3);
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
    std::uint8_t& guard = CandidatesGuard();
    if (!(guard & 1)) {
        guard |= 1;
        mfc::impl::ConstructListHeader<mfc::impl::PtrListClass>(&Candidates(), 10);
        GameAtExit(ei::static_init::Exit005D2570);
    }
    CUnit* self = brain->owner->unit;
    brain->CollectEnemies(self, &Candidates());
    brain->ChooseTarget(&enemyPart, &Candidates(), &self->position, 0, 0);
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
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(2);
    if (!chosen) return;
    brain->CallAllies(&static_cast<CMapObject*>(chosen)->position);
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
    if (!brain->owner->IsScrollSource(part)) brain->action = AIBrain::kActionCast;
    if (object != brain->target) {
        if (brain->target) brain->target->Release();
        brain->target = nullptr;
        if (object) ++object->refCount_;
        brain->target = object;
    }
    brain->actionTime = part;
    brain->owner->OrderCast(part, object, 0);
}
EI_REPLACE_METHOD(0x005D2580, AggressionMode::Execute);

void SuspectionMode::Load(CStream* stream) {
    stream->Read(&point, sizeof(Vector3));
    stream->Read(&level, 4);
}
EI_REPLACE_METHOD(0x005D2330, SuspectionMode::Load);

void SuspectionMode::Save(CStream* stream) {
    stream->Write(&point, sizeof(Vector3));
    WriteCopy(stream, level);
}
EI_REPLACE_METHOD(0x005D2360, SuspectionMode::Save);

// --- Aggression ----------------------------------------------------------------------------------

void AggressionMode::Enter(AIBrain* brain) {
    brain->owner->SetBusy(1);
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
    brain->owner->voice.Raise(6);
}
EI_REPLACE_METHOD(0x005D26D0, AggressionMode::Enter);

void AggressionMode::Leave(AIBrain* brain) {
    if (!enemy || IsGone(enemy) != 0) {
        brain->noticedLevel = 0;
        brain->noticedKind = 0;
    }
    ReleaseRef(enemy);
    brain->owner->SetBusy(0);
    brain->owner->voice.Raise(4);
}
EI_REPLACE_METHOD(0x005D2710, AggressionMode::Leave);

void AggressionMode::Load(CStream* stream) {
    reinterpret_cast<AIRef*>(&enemy)->Load(stream, TheServerWorld());
    stream->Read(&fixed, 1);
}
EI_REPLACE_METHOD(0x005D2770, AggressionMode::Load);

void AggressionMode::Save(CStream* stream) {
    reinterpret_cast<AIRef*>(&enemy)->Save(stream);
    stream->Write(&fixed, 1);
}
EI_REPLACE_METHOD(0x005D27A0, AggressionMode::Save);

// --- CorpseWatcher -------------------------------------------------------------------------------

int CorpseWatcherMode::Evaluate(AIBrain* brain) {
    if (brain->corpses.count != 0) {
        AIRef corpse;
        AIRef* popped = brain->PopCorpse(&corpse);
        brain->Notice(&static_cast<CMapObject*>(popped->object)->position, 800, -3);
        if (corpse.object) corpse.object->Release();
    }
    return kNoPriority;
}
EI_REPLACE_METHOD(0x005D27D0, CorpseWatcherMode::Evaluate);

// --- Fear ----------------------------------------------------------------------------------------

void FearMode::Enter(AIBrain* brain) {
    brain->owner->SetBusy(1);
    ReleaseRef(brain->target);
    brain->action = AIBrain::kActionNone;
}
EI_REPLACE_METHOD(0x005D3650, FearMode::Enter);

void FearMode::Leave(AIBrain* brain) { brain->owner->SetBusy(0); }
EI_REPLACE_METHOD(0x005D3680, FearMode::Leave);

void FearMode::Load(CStream* stream) {
    ReadField(stream, &distance);
    ReadField(stream, &spread);
    ReadField(stream, &shelter);
    if (stream->version_ >= 0x53) {
        ReadField(stream, &flag);
        if (stream->version_ >= 0x70) ReadField(stream, &fleePoint);
    }
}
EI_REPLACE_METHOD(0x005D3690, FearMode::Load);

void FearMode::Save(CStream* stream) {
    WriteCopy(stream, distance);
    WriteCopy(stream, spread);
    stream->Write(&shelter, sizeof(Vector3));
    WriteCopy(stream, flag);
    stream->Write(&fleePoint, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x005D36F0, FearMode::Save);

// --- Player --------------------------------------------------------------------------------------

void PlayerMode::Execute(AIBrain* brain) {
    CUnit* unit = brain->owner->unit;
    // The player controls this unit directly.
    bool direct = false;
    if (unit->player) direct = PlayerServerOf(unit->player)->refreshPending_ == 2;
    if (brain->seen && unit->fightAtWill != 1) reinterpret_cast<CUnitServer*>(brain->owner)->Shout(0x20);

    switch (state) {
    case kStateFollow: {
        if (!leader && brain->action == AIBrain::kActionFollow) {
            reinterpret_cast<AIRef*>(&brain->target)->Assign(nullptr);
            brain->action = AIBrain::kActionNone;
        }
        int action = brain->action;
        if (action == AIBrain::kActionAttack && !target) {
            PlayerUpdateCombatOriginal(this, nullptr, brain);
            return;
        }
        if (action != AIBrain::kActionNone && (!leader || action != AIBrain::kActionFollow)) return;
        CEngineObject* who = leader;
        if (!who) {
            PlayerUpdateCombatOriginal(this, nullptr, brain);
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
            PlayerUpdateCombatOriginal(this, nullptr, brain);
            return;
        }
        // While the leader itself carries out a command (its Player mode in state 1), only follow.
        AIBrain* leaderBrain = reinterpret_cast<CUnitServer*>(static_cast<CUnit*>(leader)->controller)->Brain();
        for (auto* node = leaderBrain->modes.head; node;) {
            AIMode* mode = node->data;     // 005D9830 (GetNext)
            node = node->next;
            if (mode->GetType() == kModePlayer) {
                if (mode && static_cast<PlayerMode*>(mode)->state == kStateCommand) return;
                PlayerUpdateCombatOriginal(this, nullptr, brain);
                return;
            }
        }
        PlayerUpdateCombatOriginal(this, nullptr, brain);
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
                reinterpret_cast<CUnitServer*>(brain->owner)->Shout(0x21);
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
            brain->owner->OrderMove(&to.x, 0, 0, kMoveTime);
        }
        if (state == kStateMoveTo) PlayerUpdateCombatOriginal(this, nullptr, brain);
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

namespace {
// Does the player control the unit directly?
bool DirectControl(const AIBrain* brain) {
    const char* player = brain->owner->unit->player;
    return player && PlayerServerOf(player)->refreshPending_ == 2;
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
    brain->owner->OrderMove(&point.x, run, unknown, kMoveTime);
}
EI_REPLACE_METHOD(0x005D39E0, PlayerMode::CommandWalk);

void PlayerMode::CommandGoTo(AIBrain* brain, const Vector3* to, int unknown, int runArg, const Vector3* offset) {
    state = kStateMoveTo;
    PrepareWalk(this, brain, to, runArg, offset);
    if (!DirectControl(brain) && !StaysWithParty(brain, true)) {
        std::memset(&point.z, 0, 4);
        brain->owner->PlanMove(&point.x, unknown, 0, kMoveTime);
        return;
    }
    constexpr std::uint32_t kOneBits = 0x3F800000u;
    std::memcpy(&point.z, &kOneBits, 4);
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(&point.x, run, unknown, kMoveTime);
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
    brain->owner->OrderAttack(brain->target, how, unknown, 2);
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
    if (!brain->owner->IsScrollSource(spell)) brain->action = AIBrain::kActionCast;
    if (object != brain->target) {
        if (brain->target) brain->target->Release();
        brain->target = nullptr;
        if (object) ++object->refCount_;
        brain->target = object;
    }
    brain->actionTime = spell;
    brain->owner->OrderCast(spell, object, running);
}
EI_REPLACE_METHOD(0x005D3D20, PlayerMode::CommandCast);

void PlayerMode::CommandCastAt(AIBrain* brain, int spell, const Vector3* at, int) {
    std::uint8_t running = run;
    state = kStateFollow;
    if (!brain->owner->IsScrollSource(spell)) brain->action = AIBrain::kActionCast;
    brain->actionTime = spell;
    brain->owner->OrderCastAt(spell, &at->x, running);
}
EI_REPLACE_METHOD(0x005D3D90, PlayerMode::CommandCastAt);

namespace {
// The first Player mode (type 6) in the brain of `unit`'s controller, or null.
PlayerMode* PlayerModeOf(CUnit* unit) {
    AIBrain* brain = reinterpret_cast<CUnitServer*>(unit->controller)->Brain();
    for (auto* node = brain->modes.head; node; node = node->next)
        if (node->data->Virtuals()->GetType() == kModePlayer) return static_cast<PlayerMode*>(node->data);
    return nullptr;
}
} // namespace

void PlayerMode::SetLeader(AIBrain* brain, CEngineObject* newLeader) {
    CUnit* self = brain->owner->unit;
    if (newLeader == self) {
        if (leader) {
            reinterpret_cast<AIRef*>(&leader)->Reset();
            leader = nullptr;
        }
        return;
    }
    // Following a unit that (through its leaders) follows this one: this unit stops following.
    if (newLeader) {
        auto* unit = static_cast<CUnit*>(newLeader);
        for (;;) {
            PlayerMode* mode = PlayerModeOf(unit);
            auto* next = mode ? static_cast<CUnit*>(mode->leader) : nullptr;
            if (!next) break;
            if (next == self) {
                AIBrain* own = reinterpret_cast<CUnitServer*>(next->controller)->Brain();
                if (PlayerMode* ownMode = PlayerModeOf(next)) ownMode->SetLeader(own, nullptr);
                break;
            }
            unit = next;
        }
    }
    if (brain->action == AIBrain::kActionWalk) {
        state = kStateFollow;
        ReleaseRef(brain->target);
        brain->action = AIBrain::kActionNone;
    }
    if (newLeader != leader) {
        if (leader) leader->Release();
        leader = nullptr;
        if (newLeader) ++newLeader->refCount_;
        leader = newLeader;
    }
}
EI_REPLACE_METHOD(0x005D3DE0, PlayerMode::SetLeader);

void PlayerMode::OnObjectDeleted(CEngineObject* object) {
    if (target == object && target) ReleaseRef(target);
    if (leader == object && leader) ReleaseRef(leader);
}
EI_REPLACE_METHOD(0x005D37A0, PlayerMode::OnObjectDeleted);

void PlayerMode::Load(CStream* stream) {
    // The save versions that wrote a group of objects after the mode.
    constexpr int kGroupFirst = 0x46, kGroupEnd = 0x5E;
    reinterpret_cast<AIRef*>(&target)->Load(stream, TheServerWorld());
    reinterpret_cast<AIRef*>(&leader)->Load(stream, TheServerWorld());
    stream->Read(&point, sizeof(Vector3));
    stream->Read(&state, 4);
    if (stream->version_ >= kGroupFirst && stream->version_ < kGroupEnd) {
        ScriptGroup group;
        group.vtable = scriptvtable::kRefs;
        group.data = nullptr;
        group.size = 0;
        group.maxSize = 0;
        group.growBy = 0;
        group.Load(reinterpret_cast<ScriptStream*>(stream));
        group.DestructBody();
    }
}
EI_REPLACE_METHOD(0x005D4CD0, PlayerMode::Load);

void PlayerMode::Save(CStream* stream) {
    reinterpret_cast<AIRef*>(&target)->Save(stream);
    reinterpret_cast<AIRef*>(&leader)->Save(stream);
    stream->Write(&point, sizeof(Vector3));
    stream->Write(&state, 4);
}
EI_REPLACE_METHOD(0x005D4DB0, PlayerMode::Save);
