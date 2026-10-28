// Unit AI modes: the simple virtuals of Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher,
// Fear and Player.
#include "combat.h"
#include "game_data.h"
#include "unit_ai.h"
#include "at_exit_functions.h"

#include "player_messages.h"
#include "random.h"
#include "script.h"
#include "world.h"

#include "lib/crt.h"
#include "ei/x87.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include "game_singletons.h"
#include "lib/mfc.h"
#include "lib/nan_audit.h"
#include "logic_settings.h"
#include "lib/static_object.h"

namespace {
constexpr int kPriority = 50;
constexpr int kNoPriority = -1000;
constexpr float kZero = 0.0f;
// (a double, 0.4 / 2^32) (0.8f): the new alertness decay.
constexpr double kDecayRandomScale = std::bit_cast<double>(0x3DD99999C019999Aull);
constexpr float kDecayBase = std::bit_cast<float>(0x3F4CCCCDu);
const std::uint32_t* Diplomacy() { return GameDiplomacy()->allied_; }

// The server side of a unit's player (CPlayer::connection_). Its refreshPending_ == 2 is what the modes take
// as the player controlling the unit directly.
inline const CPlayerServer* PlayerServerOf(const CPlayer* player) {
    return player->Server();
}

template <typename T>
void ReleaseRef(AIRefTo<T>& ref) {
    if (ref) {
        ref->Release();
        ref = nullptr;
    }
}

int IsGone(CMapObject* object) { return object->IsDead(); }

template <typename T>
void ReadField(CStream* stream, T* field, unsigned size = sizeof(T)) { stream->Read(field, size); }

// Each scalar is copied into a stack slot before it is written.
template <typename T>
void WriteCopy(CStream* stream, const T& value, unsigned size = sizeof(T)) {
    T copy = value;
    stream->Write(&copy, size);
}

constexpr float kMoveTime = 1000000.0f;
constexpr float kNearX = 35.0f;
constexpr float kNearY = 80.0f;
constexpr float kHalf = 0.5f;
constexpr float kNoWander = -1.0f;
constexpr float kStandRadius = 5.0f;
constexpr float kArrivedSq = std::bit_cast<float>(0x3E4CCCCDu);         // (0.2)
constexpr float kCloseSq = 25.0f;
constexpr std::uint32_t kUnsetBits = 0xC4A42000u;                       // -1313: no position yet
constexpr int kRemarkIdle = 1;

void MaybeRemark(AIBrain* brain, int what) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(what);
}

void StartMove(AIBrain* brain, AIMoveOrder* order) {
    if (brain->target) {
        brain->target.Reset();
        brain->target = nullptr;
    }
    brain->action = AIBrain::kActionMove;
    brain->actionTime = AIBrain::kNoTime;
    std::memcpy(&brain->move.point, &order->point, sizeof(Vector3));
    brain->move.looks = order->looks;
    brain->owner->OrderMove(&brain->move.point, 0, 0, kMoveTime);
}

void SetPositions(AIBrain* brain, const Vector3* where, float nearX, float nearY) {
    brain->nearX = nearX;
    brain->nearY = nearY;
    std::memcpy(&brain->previousPosition, &brain->position, sizeof(Vector3));
    std::memcpy(&brain->position, where, sizeof(Vector3));
    std::uint32_t previous;
    std::memcpy(&previous, &brain->previousPosition.x, 4);
    if (previous == kUnsetBits) std::memcpy(&brain->previousPosition, where, sizeof(Vector3));
}

// The static candidate list of Aggression::Evaluate (guard bit 0 of g_candidatesGuard): a CPtrList built on
// first use and destroyed at exit.
constinit lib::StaticObject<AIObjectList> g_candidates = /*007C54C8*/ {};
AIObjectList& Candidates() { return *g_candidates; }
std::uint8_t g_candidatesGuard = /*007C54E4*/ {};
void DestroyCandidates() { g_candidates.Destroy(); }

} // namespace

// --- Shared slots --------------------------------------------------------------------------------

int AIMode::Evaluate(AIBrain*) { return kPriority; }

void AIMode::Enter(AIBrain* brain) {
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
    std::uint32_t roll = IsaacRandom::Global().Next();
    volatile std::int64_t wide = roll;             // the zero-extended value
    brain->decay = static_cast<float>(static_cast<double>(wide) * kDecayRandomScale + kDecayBase);
    brain->owner->SetBusy(0);
}

void AIActionMode::Enter(AIBrain* brain) {
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
}

// --- Guard ---------------------------------------------------------------------------------------

void GuardMode::Set(const Vector3* p, float newRadius, float newWander) {
    std::memcpy(&point, p, sizeof(Vector3));
    failures = 0;
    if (newRadius >= kZero)
        std::memcpy(&radius, &newRadius, 4);
    else
        std::memcpy(&radius, &GameLogicSettings().guardRadius, 4);
    if (newWander >= kZero)
        std::memcpy(&wander, &newWander, 4);
    else
        std::memcpy(&wander, &GameLogicSettings().guardStayTime, 4);
}

void GuardMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, kRemarkIdle);
    brain->attention.LookAround(brain, 0);
    if (brain->action != AIBrain::kActionNone) return;
    // 35 + radius / 80 + radius, computed before the positions are copied.
    float nearX = kNearX + radius;
    float nearY = kNearY + radius;
    SetPositions(brain, &point, nearX, nearY);

    AIMoveOrder order;
    order.Clear();
    if (!brain->FindStandPoint(&point, radius, &order.point)) {
        if (++failures <= 100) {
            return;
        }
        if (!brain->FindStandPoint(&brain->owner->unit->localPosition_, radius, &order.point)) {
            return;
        }
    }
    failures = 0;
    float dx = order.point.x - point.x;
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
}

void GuardMode::Load(CStream* stream) {
    ReadField(stream, &radius);
    ReadField(stream, &wander);
    ReadField(stream, &point);
    if (stream->version_ >= 0x66) ReadField(stream, &failures);
}

void GuardMode::Save(CStream* stream) {
    WriteCopy(stream, radius);
    WriteCopy(stream, wander);
    stream->Write(&point, sizeof(Vector3));
    WriteCopy(stream, failures);
}

// --- Patrol --------------------------------------------------------------------------------------

void PatrolMode::AddPoint(const Vector3* a0) { AppendPoint(a0); }

int PatrolMode::Evaluate(AIBrain* brain) {
    if (std::ssize(points) != 0) return kPriority;
    game::FatalError("Unit %i has no patrol points, switching to guard", brain->owner->unit->id_);
    AddPoint(&brain->owner->unit->localPosition_);
    return kNoPriority;
}

void PatrolMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, kRemarkIdle);
    brain->attention.LookAround(brain, 0);
    if (std::ssize(points) == 0 || brain->action != AIBrain::kActionNone) return;
    if (++current >= std::ssize(points)) current = 0;
    PatrolPoint* next = points.data() + current;
    AIMoveOrder order;
    std::memcpy(&order.point, &next->position, sizeof(Vector3));
    order.looks = next->looks;
    if (order.looks.empty()) {
        Vector3 none{0.0f, 0.0f, 0.0f};
        brain->PlanLooks(&order, &none, kNoWander, false);
    }
    StartMove(brain, &order);
    SetPositions(brain, &order.point, kNearX, kNearY);
}

PatrolPoint* PatrolMode::LastPoint() {
    if (std::ssize(points) == 0) return nullptr;
    return points.data() + (std::ssize(points) - 1);
}

void PatrolMode::Load(CStream* stream) {
    int count;
    ReadField(stream, &count);
    points.resize(count);
    for (int i = 0; i < std::ssize(points); ++i) {
        PatrolPoint& point = points[i];
        ReadField(stream, &point.position);
        int lookCount;
        ReadField(stream, &lookCount);
        point.looks.resize(lookCount);
        for (int j = 0; j < std::ssize(point.looks); ++j) {
            AILook& look = point.looks[j];
            ReadField(stream, &look.angle);
            ReadField(stream, &look.time);
            ReadField(stream, &look.wait);
            if (stream->version_ >= 0x44) ReadField(stream, &look.stopFirst);
        }
    }
    ReadField(stream, &current);
}

void PatrolMode::Save(CStream* stream) {
    WriteCopy(stream, std::ssize(points));
    for (int i = 0; i < std::ssize(points); ++i) {
        PatrolPoint& point = points[i];
        stream->Write(&point.position, sizeof(Vector3));
        WriteCopy(stream, std::ssize(point.looks));
        for (int j = 0; j < std::ssize(point.looks); ++j) {
            const AILook& look = point.looks[j];
            // Each float goes through a stack copy as a dword (integer moves).
            WriteCopy(stream, std::bit_cast<std::uint32_t>(look.angle));
            WriteCopy(stream, std::bit_cast<std::uint32_t>(look.time));
            WriteCopy(stream, std::bit_cast<std::uint32_t>(look.wait));
            WriteCopy(stream, look.stopFirst);
        }
    }
    WriteCopy(stream, current);
}

// --- Sentry --------------------------------------------------------------------------------------

void SentryMode::Set(const Vector3* p) { std::memcpy(&point, p, sizeof(Vector3)); }

void SentryMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, kRemarkIdle);
    brain->attention.LookAround(brain, 0);
    if (brain->action != AIBrain::kActionNone) return;
    AIMoveOrder order;
    order.looks.clear();
    std::memcpy(&order.point, &point, sizeof(Vector3));
    bool farLook = IsaacRandom::Global().Next() % 11 == 0;
    Vector3 none{0.0f, 0.0f, 0.0f};
    // the wander radii of a far and a near look-around.
    float wanderRadius = farLook ? GameLogicSettings().guardStayTime : GameLogicSettings().sentryStayTime;
    brain->PlanLooks(&order, &none, wanderRadius, farLook);
    StartMove(brain, &order);
    SetPositions(brain, &order.point, kNearX, kNearY);
}

void SentryMode::Load(CStream* stream) { stream->Read(&point, sizeof(Vector3)); }

void SentryMode::Save(CStream* stream) { stream->Write(&point, sizeof(Vector3)); }

// --- Follow --------------------------------------------------------------------------------------

void FollowMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone) return;
    CMapObject* who = leader;
    if (!who) return;
    brain->action = AIBrain::kActionFollow;
    if (who == brain->target) return;
    if (brain->target) brain->target->Release();
    brain->target = nullptr;
    ++who->refCount_;
    brain->target = who;
}

void FollowMode::OnObjectDeleted(CEngineObject* object) {
    if (leader == object && leader) leader.Reset();
}

void FollowMode::Load(CStream* stream) { leader.Load(stream, TheServerWorld()); }

void FollowMode::Save(CStream* stream) { leader.Save(stream); }

// --- Suspection ----------------------------------------------------------------------------------

int SuspectionMode::Evaluate(AIBrain* brain) {
    if (CEngineObject* seen = brain->seen) {
        bool allied = false;
        if (CUnit* seenUnit = UnitOrNull(seen)) {
            std::uint32_t bit = 1u << (seenUnit->side & 31);
            allied = (Diplomacy()[brain->owner->unit->side] & bit) != 0;
        }
        if (!allied) {
            Vector3 where;
            std::memcpy(&where, &static_cast<const CMapObject*>(brain->seen.object)->localPosition_, sizeof(Vector3));
            brain->memory.Notice(&where, 1000, -2);
        }
    }
    EI_NAN_AUDIT(brain->memory.noticedX, point.x, brain->memory.noticedY, point.y);
    if (brain->memory.noticedX == point.x && brain->memory.noticedY == point.y) {
        int value = level;
        if (!(value < brain->memory.noticedLevel)) value = brain->memory.noticedLevel;
        level = value;
        return value;
    }
    level = brain->memory.noticedLevel;
    std::memcpy(&point.x, &brain->memory.noticedX, sizeof(float));
    std::memcpy(&point.y, &brain->memory.noticedY, sizeof(float));
    point.z = 0.0f;
    return level;
}

void SuspectionMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone) {
        Vector3 going;
        std::memcpy(&going, &brain->move.point, sizeof(Vector3));
        double dy = point.y - going.y;      // kept on the FPU stack
        double dx = point.x - going.x;
        // A new move only when the distance is greater.
        EI_NAN_AUDIT(dx, dy);
        if (LengthSquared2DUnrounded(dx, dy) <= kArrivedSq) {
            const float* unit = &brain->owner->unit->localPosition_.x;
            double ux = unit[0] - point.x;
            double uy = unit[1] - point.y;
            double uz = unit[2] - point.z;
            EI_NAN_AUDIT(ux, uy, uz);
            if (LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz) < kCloseSq) level -= 5;
            return;
        }
    }
    AIMoveOrder order;
    order.Clear();
    if (brain->action == AIBrain::kActionNone) {
        if (brain->owner->orderStopped) level -= 50;
        if (!brain->FindStandPoint(&point, kStandRadius, &order.point)) {
            level -= 5;
            return;
        }
    } else {
        std::memcpy(&order.point, &point, sizeof(Vector3));
    }
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, &none, kNoWander, false);
    brain->target.Assign(nullptr);
    brain->action = AIBrain::kActionMove;
    brain->actionTime = AIBrain::kNoTime;
    std::memcpy(&brain->move.point, &order.point, sizeof(Vector3));
    brain->move.looks = order.looks;
    brain->owner->OrderMove(&brain->move.point, 0, 0, kMoveTime);
    level = brain->memory.noticedLevel;
}

void SuspectionMode::Enter(AIBrain* brain) {
    brain->owner->SetBusy(1);
    AIMoveOrder order;
    order.point = Vector3{0.0f, 0.0f, 0.0f};
    order.looks.clear();
    std::memcpy(&order.point, &point, sizeof(Vector3));
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, &none, kNoWander, false);
    StartMove(brain, &order);
    brain->owner->voice.Raise(5);
}

void SuspectionMode::Leave(AIBrain* brain) {
    brain->owner->SetBusy(0);
    brain->owner->voice.Raise(3);
}

int AggressionMode::Evaluate(AIBrain* brain) {
    chosen.object = nullptr;
    if (fixed) {
        if (enemy && IsGone(enemy) != 0) {
            enemy.Assign(nullptr);
            return kNoPriority;
        }
        if (!enemy) {
            if (CMapObject* seen = brain->seen) {
                bool allied = false;
                if (CUnit* seenUnit = UnitOrNull(seen)) {
                    std::uint32_t bit = 1u << (seenUnit->side & 31);
                    allied = (Diplomacy()[brain->owner->unit->side] & bit) != 0;
                }
                if (!allied && seen != brain->owner->unit && !brain->memory.reports.Contains(seen))
                    enemy.Assign(seen);
            }
        }
        if (!enemy) return kNoPriority;
        if (IsGone(enemy) == 0 && enemy->GetClassId() == kClassUnit) {
            if (brain->memory.IsEnemy(brain->owner->unit, enemy) == 0) return kNoPriority;
            chosen.part = -1;
            chosen.object = static_cast<CUnit*>(enemy.object);   // a unit (tested above)
            return chosen.object ? 10000 : kNoPriority;
        }
        if (enemy) {
            enemy.Reset();
            enemy = nullptr;
        }
        return kNoPriority;
    }
    if (!brain->memory.seesEnemy) return kNoPriority;
    std::uint8_t& guard = g_candidatesGuard;
    if (!(guard & 1)) {
        guard |= 1;
        g_candidates.Construct();
        std::atexit(DestroyCandidates);
    }
    CUnit* self = brain->owner->unit;
    brain->memory.FindEnemies(self, &Candidates());
    brain->ChooseTarget(&chosen, &Candidates(), &self->localPosition_, 0, 0);
    CUnit* best = chosen.object;
    if (best != enemy) {
        if (enemy) enemy->Release();
        enemy = nullptr;
        if (best) ++best->refCount_;
        enemy = best;
    }
    return chosen.object ? 10000 : kNoPriority;
}

void AggressionMode::Execute(AIBrain* brain) {
    if (IsaacRandom::Global().Next() % 1001 == 1) brain->owner->voice.Raise(2);
    if (!chosen.object) return;
    brain->CallAllies(&chosen.object->localPosition_);
    AITargetChoice attack{-1, nullptr};     // what the brain is attacking now
    brain->attention.CurrentAttack(brain, &attack);
    if (attack.object == chosen.object && attack.part == chosen.part) return;
    if (!chosen.object) return;
    attack.object = nullptr;
    attack.part = -1;
    brain->attention.CurrentAttack(brain, &attack);
    CUnit* object = chosen.object;
    if (object == attack.object && chosen.part == attack.part) return;
    int part = chosen.part;
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

void SuspectionMode::Load(CStream* stream) {
    stream->Read(&point, sizeof(Vector3));
    stream->Read(&level, 4);
}

void SuspectionMode::Save(CStream* stream) {
    stream->Write(&point, sizeof(Vector3));
    WriteCopy(stream, level);
}

// --- Aggression ----------------------------------------------------------------------------------

void AggressionMode::Enter(AIBrain* brain) {
    brain->owner->SetBusy(1);
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
    brain->owner->voice.Raise(6);
}

void AggressionMode::Leave(AIBrain* brain) {
    if (!enemy || IsGone(enemy) != 0) {
        brain->memory.noticedLevel = 0;
        brain->memory.noticedKind = 0;
    }
    enemy.Reset();
    brain->owner->SetBusy(0);
    brain->owner->voice.Raise(4);
}

void AggressionMode::Load(CStream* stream) {
    enemy.Load(stream, TheServerWorld());
    stream->Read(&fixed, 1);
}

void AggressionMode::Save(CStream* stream) {
    enemy.Save(stream);
    stream->Write(&fixed, 1);
}

// --- CorpseWatcher -------------------------------------------------------------------------------

int CorpseWatcherMode::Evaluate(AIBrain* brain) {
    if (!brain->memory.corpses.empty()) {
        AIRefTo<CUnit> corpse;
        AIRefTo<CUnit>* popped = brain->PopCorpse(&corpse);
        brain->memory.Notice(&popped->object->localPosition_, 800, -3);
        if (corpse.object) corpse.object->Release();
    }
    return kNoPriority;
}

// --- Fear ----------------------------------------------------------------------------------------

void FearMode::Enter(AIBrain* brain) {
    brain->owner->SetBusy(1);
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
}

void FearMode::Leave(AIBrain* brain) { brain->owner->SetBusy(0); }

void FearMode::Load(CStream* stream) {
    ReadField(stream, &distance);
    ReadField(stream, &spread);
    ReadField(stream, &shelter);
    if (stream->version_ >= 0x53) {
        ReadField(stream, &flag);
        if (stream->version_ >= 0x70) ReadField(stream, &fleePoint);
    }
}

void FearMode::Save(CStream* stream) {
    WriteCopy(stream, distance);
    WriteCopy(stream, spread);
    stream->Write(&shelter, sizeof(Vector3));
    WriteCopy(stream, flag);
    stream->Write(&fleePoint, sizeof(Vector3));
}

// --- Player --------------------------------------------------------------------------------------

void PlayerMode::Execute(AIBrain* brain) {
    CUnit* unit = brain->owner->unit;
    bool direct = false;
    if (unit->player) direct = PlayerServerOf(unit->player)->refreshPending_ == 2;
    if (brain->seen && unit->fightAtWill != 1) brain->owner->Shout(0x20);

    switch (state) {
    case kStateFollow: {
        if (!leader && brain->action == AIBrain::kActionFollow) {
            brain->target.Assign(nullptr);
            brain->action = AIBrain::kActionNone;
        }
        int action = brain->action;
        if (action == AIBrain::kActionAttack && !target) {
            UpdateCombat(brain);
            return;
        }
        if (action != AIBrain::kActionNone && (!leader || action != AIBrain::kActionFollow)) return;
        CMapObject* who = leader;
        if (!who) {
            UpdateCombat(brain);
            return;
        }
        if (!direct) {
            brain->action = AIBrain::kActionFollow;
            if (who != brain->target) {
                brain->target.Reset();
                if (who) ++who->refCount_;
                brain->target = who;
            }
        }
        if (!leader) {
            UpdateCombat(brain);
            return;
        }
        // While the leader itself carries out a command (its Player mode in state 1), only follow.
        PlayerMode* leaderMode = leader->ServerController()->Brain()->FindPlayerMode();
        if (leaderMode && leaderMode->state == kStateCommand) return;
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
                brain->target.Reset();
                brain->action = AIBrain::kActionNone;
            } else if (!direct && !StaysWithParty(brain, true)) {
                brain->owner->Shout(0x21);
                std::memset(&point.z, 0, 4);
                brain->target.Reset();
                brain->action = AIBrain::kActionNone;
            }
        } else if (direct || StaysWithParty(brain, false)) {
            std::memcpy(&point.z, &kOneBits, 4);
            Vector3 to;
            std::memcpy(&to.x, &point.x, 4);
            std::memcpy(&to.y, &point.y, 4);
            std::memcpy(&to.z, &kUnsetBits, 4);
            brain->action = AIBrain::kActionWalk;
            brain->owner->OrderMove(&to, 0, 0, kMoveTime);
        }
        if (state == kStateMoveTo) UpdateCombat(brain);
        return;
    }
    default:
        break;
    }
    state = kStateFollow;
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
}

namespace {
bool DirectControl(const AIBrain* brain) {
    const CPlayer* player = brain->owner->unit->player;
    return player && PlayerServerOf(player)->refreshPending_ == 2;
}

// Common start of CommandWalk / CommandGoTo: the point (moved by `offset` when the way is clear),
// the run flag, and the references dropped.
void PrepareWalk(PlayerMode* self, AIBrain* brain, const Vector3* to, int run, const Vector3* offset) {
    std::memcpy(&self->point, to, sizeof(Vector3));
    Vector3 shifted;
    shifted.x = to->x + offset->x;
    shifted.y = to->y + offset->y;
    shifted.z = to->z + offset->z;
    if (self->PathClear(brain, to, &shifted)) {
        self->point.x = offset->x + self->point.x;
        self->point.y = offset->y + self->point.y;
        self->point.z = offset->z + self->point.z;
    }
    self->run = static_cast<std::uint8_t>(run);
    if (!DirectControl(brain)) ReleaseRef(self->leader);
    self->target.Reset();
}
} // namespace

void PlayerMode::CommandWalk(AIBrain* brain, const Vector3* to, int unknown, int runArg, const Vector3* offset) {
    state = kStateCommand;
    PrepareWalk(this, brain, to, runArg, offset);
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(&point, run, unknown, kMoveTime);
}

void PlayerMode::CommandGoTo(AIBrain* brain, const Vector3* to, int unknown, int runArg, const Vector3* offset) {
    state = kStateMoveTo;
    PrepareWalk(this, brain, to, runArg, offset);
    if (!DirectControl(brain) && !StaysWithParty(brain, true)) {
        std::memset(&point.z, 0, 4);
        brain->owner->PlanMove(&point, unknown, 0, kMoveTime);
        return;
    }
    constexpr std::uint32_t kOneBits = 0x3F800000u;
    std::memcpy(&point.z, &kOneBits, 4);
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(&point, run, unknown, kMoveTime);
}

void PlayerMode::CommandAttack(AIBrain* brain, CMapObject* object, int how, int unknown) {
    if (object == brain->owner->unit) return;
    state = kStateFollow;
    if (object != target) {
        if (target) target->Release();
        target = nullptr;
        if (object) ++object->refCount_;
        target = object;
    }
    if (brain->owner->unit->player == nullptr && object->GetClassId() == kClassUnit &&
        brain->memory.IsEnemy(brain->owner->unit, object) == 0)
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

void PlayerMode::CommandUse(AIBrain* brain, CMapObject* object, int kind, int a, int b) {
    state = kStateFollow;
    brain->UseOn(object, kind, a, b);
}

void PlayerMode::CommandCast(AIBrain* brain, int spell, CMapObject* object, int) {
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

void PlayerMode::CommandCastAt(AIBrain* brain, int spell, const Vector3* at, int) {
    std::uint8_t running = run;
    state = kStateFollow;
    if (!brain->owner->IsScrollSource(spell)) brain->action = AIBrain::kActionCast;
    brain->actionTime = spell;
    brain->owner->OrderCastAt(spell, at, running);
}

namespace {
PlayerMode* PlayerModeOf(CUnit* unit) { return unit->ServerController()->Brain()->FindPlayerMode(); }
} // namespace

void PlayerMode::SetLeader(AIBrain* brain, CUnit* newLeader) {
    CUnit* self = brain->owner->unit;
    if (newLeader == self) {
        if (leader) {
            leader.Reset();
            leader = nullptr;
        }
        return;
    }
    // Following a unit that (through its leaders) follows this one: this unit stops following.
    if (newLeader) {
        CUnit* unit = newLeader;
        for (;;) {
            PlayerMode* mode = PlayerModeOf(unit);
            CUnit* next = mode ? mode->leader.object : nullptr;
            if (!next) break;
            if (next == self) {
                AIBrain* own = next->ServerController()->Brain();
                if (PlayerMode* ownMode = PlayerModeOf(next)) ownMode->SetLeader(own, nullptr);
                break;
            }
            unit = next;
        }
    }
    if (brain->action == AIBrain::kActionWalk) {
        state = kStateFollow;
        brain->target.Reset();
        brain->action = AIBrain::kActionNone;
    }
    if (newLeader != leader) {
        if (leader) leader->Release();
        leader = nullptr;
        if (newLeader) ++newLeader->refCount_;
        leader = newLeader;
    }
}

void PlayerMode::OnObjectDeleted(CEngineObject* object) {
    if (target == object && target) target.Reset();
    if (leader == object && leader) ReleaseRef(leader);
}

void PlayerMode::Load(CStream* stream) {
    constexpr int kGroupFirst = 0x46, kGroupEnd = 0x5E;
    target.Load(stream, TheServerWorld());
    leader.Load(stream, TheServerWorld());
    stream->Read(&point, sizeof(Vector3));
    stream->Read(&state, 4);
    if (stream->version_ >= kGroupFirst && stream->version_ < kGroupEnd) {
        ScriptGroup group;
        group.Load(stream);
    }
}

void PlayerMode::Save(CStream* stream) {
    target.Save(stream);
    leader.Save(stream);
    stream->Write(&point, sizeof(Vector3));
    stream->Write(&state, 4);
}
