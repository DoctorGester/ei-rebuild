// The unit movement tick (called twice for the controller, CUnitServer): samples the unit's motion order at
// the world's time, starts the next animation of a finished motion, plays a pending fall or death
// animation, then moves the unit to the sampled position on the sector map; a blocked step either waits
// (the blocker is a party member leading, or the unit already waits for it), yields to a slower unit, or
// replans the path. See docs/notes/combat.md ("Movement tick") and docs/notes/ai.md ("How a unit moves").
#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"
#include "combat_plan.h"
#include "core_maths.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "player_messages.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world_data.h"
#include "unit.h"
#include "world.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "game_data.h"
#include "lib/vecmath.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

// --- Float helpers -------------------------------------------------------------------------------

// The exe's constants: the cell size, 0.5, 1.0, the stamina drain step (double).
float CellSize() { return 1.0f /*0073EEE4*/; }
float Half() { return 0.5f /*0073B86C*/; }
float One() { return 1.0f /*0073B84C*/; }
double DrainStep() { return 0.006666666666666666 /*0073F088*/; }

// World units to half-cells: (x, y) *= 1 / (cellSize * 0.5), the factor unrounded.
void ToHalfCells(float* xy) {
    const double factor = One() / (static_cast<double>(CellSize()) * Half());
    xy[0] = static_cast<float>(factor * xy[0]);
    xy[1] = static_cast<float>(factor * xy[1]);
}

// And back: (x, y) *= cellSize * 0.5.
void FromHalfCells(float* xy) {
    const double factor = static_cast<double>(CellSize()) * Half();
    xy[0] = static_cast<float>(factor * xy[0]);
    xy[1] = static_cast<float>(factor * xy[1]);
}

// Whether |a - b|^2, summed as (dz^2 + dy^2) + dx^2 unrounded, is above *limit (false for NaN).
bool FartherThan(const float* a, const float* b, const float* limit) {
    const double dx = static_cast<double>(a[0]) - b[0];
    const double dy = static_cast<double>(a[1]) - b[1];
    const double dz = static_cast<double>(a[2]) - b[2];
    return LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz) > *limit;
}

// Whether `object`'s speed (a virtual, unrounded) compares below *than.
bool SpeedBelow(CMapObject* object, const float* than) {
    EI_NAN_AUDIT(object->MoveSpeed(), *than);
    return object->MoveSpeed() < *than;
}

// The speed rounded to float.
void SpeedOf(CMapObject* object, float* out) { *out = static_cast<float>(object->MoveSpeed()); }

// *value -= *rate * the drain step (double); true when the unrounded result compares below 0.
bool Drain(float* value, const float* rate) {
    const double drained = static_cast<double>(*value) - *rate * DrainStep();
    *value = static_cast<float>(drained);
    EI_NAN_AUDIT(drained);
    return drained < 0.0;
}

// FILD of the world's time, FSTP to float (the time Sample is called with).
float TimeAsFloat(const int* time) { return static_cast<float>(*time); }

// --- Accessors -----------------------------------------------------------------------------------

CUnit* UnitOf(CUnitServer* c) { return c->unit; }
AnimationSteps* Queue(CUnitServer* c) { return &c->queue; }
int& QueueStart(CUnitServer* c) { return c->queue.extra; }
AIPath* MotionPathOf(CUnitServer* c) { return c->MotionPath(); }
// The path's `failed` byte as the game reads it: compared with 1, so read as a byte, not a bool.
std::uint8_t FailedByte(CUnitServer* c) { return c->MotionPath()->failed; }
// The world server (ManagerContextOf) and an object's world (ObjectWorld).
CWorldServer* ServerOf(CUnitServer* c) { return c->ServerWorld(); }
int WorldTime(const CWorld* world) { return world->tick_; }

constexpr int kStateIdle = 0;
constexpr int kStateCasting = 5;
constexpr int kBlockedReplan = 1;
constexpr int kBlockedWait = 2;

// The counted "waiting for" link (followsLeader +0x1D8, followedLeader +0x1DC: the fields the
// chase uses for the leader it follows).
void SetWaitingFor(CUnitServer* c, CUnit* blocker) {
    c->followsLeader = 1;
    CUnit* old = c->followedLeader;
    c->followedLeader = blocker;
    ++blocker->refCount_;
    if (old) old->Release();
}
void ClearWaitingFor(CUnitServer* controller) {
    controller->followsLeader = 0;
    CUnit* old = controller->followedLeader;
    controller->followedLeader = nullptr;
    if (old) old->Release();
}

// The unit is on the map as an obstacle (+0x278) or not: re-registered when that changes.
void SetObstacle(CUnitServer* c, int obstacle) {
    CWorldServer* world = ServerOf(c);
    CUnit* unit = UnitOf(c);
    if (static_cast<int>(unit->obstacle) == obstacle) return;
    world->Sectors()->Remove(unit);
    UnitOf(c)->obstacle = static_cast<std::uint32_t>(obstacle);
    world->Sectors()->Add(UnitOf(c));
    UnitOf(c)->MarkChanged(7);
}

// The motion order's next animation once the current motion is over.
void QueueNextAnimation(CUnitServer* c) {
    AnimationSteps* next = c->NextAnimation();
    Queue(c)->CopySteps(*next);
    QueueStart(c) = next->extra;
    Queue(c)->Assign(*next);
    if (QueueStart(c) == -1) QueueStart(c) = WorldTime(ObjectWorld(c));
    c->PopFinishedSteps();
    c->ShowCurrentStep();
    c->movingAnimation = 1;
}

// A unit hit while turning on the spot: the death (or hit) animation of the last blow is played when
// it fits. Returns from the tick.
void HitWhileTurning(CUnitServer* c) {
    AnimationSteps steps;
    steps.ConstructOwn();
    if (c->moveMode.mode != 0 || c->state != 0) {
        c->BuildDeathAnimation(&steps, c->hitDirection, 0, 0);
        if (c->CanReactFor(steps.Duration())) {
            c->keepHitDirection = 1;
            c->savedQueue.Assign(*Queue(c));
            c->PlaySteps(&steps);
            c->animationHeld = 1;
        }
    }
    steps.DestructSteps();
}

// A hit while moving: unless the queue's first animation (an attack, flags 0x200000 under
// 0x3C0000) is still running, the current animation followed by the fall of the last blow
// replaces the queue.
void HitWhileMoving(CUnitServer* c) {
    if (Queue(c)->size > 0) {
        std::uint32_t key = (static_cast<std::uint32_t>(Queue(c)->size) & 0xFFFFFF00u) |
                            Queue(c)->data[0].animation;
        const FigureAnimation* entry = UnitOf(c)->AnimationEntry(key);
        if (entry && (entry->key & 0x3C0000) == 0x200000) {
            std::uint32_t ends = entry->frameCount + static_cast<std::uint32_t>(QueueStart(c));
            if (ends > static_cast<std::uint32_t>(WorldTime(ObjectWorld(c)))) return;
        }
    }
    AnimationSteps current;
    current.ConstructOwn();
    AnimationSteps fall;
    fall.ConstructOwn();
    c->BuildMoveSteps(&current, 0);
    c->BuildDeathAnimation(&fall, c->hitDirection, 0, 1);
    for (int i = 0; i < current.size; ++i) {
        AnimationStep step;
        step = current.data[i];   // GetAt by value
        // The dwords at step + 1 / step + 0 are passed; AddEmote / AddStep keep their low byte only (the
        // variant / the animation).
        if (step.animation == 0xFF) {
            fall.AddEmote(step.variant);
        } else if (step.variant == 0xFF) {
            fall.AddStep(step.animation, step.duration);
        } else {
            fall.SetAtGrow(fall.size, step);
        }
    }
    Queue(c)->Assign(fall);   // twice, as the game does
    Queue(c)->Assign(fall);
    if (QueueStart(c) == -1) QueueStart(c) = WorldTime(ObjectWorld(c));
    c->PopFinishedSteps();
    c->ShowCurrentStep();
    fall.DestructSteps();
    current.DestructSteps();
}

// The unit blocking the step: whether to wait for it (2), give way and replan around it (1), or
// neither (0, then the unit stays). Sets `liftBlocker` when the units around the step must be
// lifted while replanning.
int ResolveBlocker(CUnitServer* c, CUnit* blocker, bool* liftBlocker, int reason) {
    CUnit* self = UnitOf(c);
    CPlayer* selfPlayer = self->player;
    if (selfPlayer && blocker->player == selfPlayer &&
        blocker->ServerController()->brain.LeaderOrFollowed() == self && CurrentWorldTick() < c->waitFrame + 2) {
        return kBlockedWait;   // a party member following this unit
    }
    if (c->followsLeader && c->followedLeader == blocker) return kBlockedWait;

    CUnitServer* other = blocker->ServerController();
    auto isAction = [](int kind) { return kind == 3 || kind == 4 || kind == 5; };
    bool sameAttack = blocker->side == self->side && isAction(c->action.kind) &&
                      isAction(other->action.kind) && other->action.target == c->action.target &&
                      blocker->sizeClass == self->sizeClass && other->state != kStateCasting;
    if (sameAttack) {
        CMapObject* target = c->action.target;
        sameAttack = FartherThan(&target->localPosition_.x, &self->localPosition_.x, &worlddata::QueueWaitLimitRef());
    }
    if (sameAttack) {
        // Both go for the same target: wait behind the other unless the waiting would go round in a
        // circle back to this unit, which breaks the circle and replans.
        CUnitServer* link = other;
        while (link->followsLeader) {
            CUnit* waitsFor = link->followedLeader;
            if (!waitsFor) break;
            if (waitsFor == self) {
                ClearWaitingFor(other);
                ClearWaitingFor(c);
                *liftBlocker = true;
                return kBlockedReplan;
            }
            link = waitsFor->ServerController();
        }
        SetWaitingFor(c, blocker);
        return kBlockedWait;
    }
    float ownSpeed;
    SpeedOf(self, &ownSpeed);
    if (SpeedBelow(blocker, &ownSpeed) && reason == kBlockedWait) {
        *liftBlocker = true;
        return kBlockedReplan;
    }
    return -1;   // stays where it is
}

// The motion is over: a path with no cells left (`reached`, +0xA8) stops facing its way; otherwise
// the path continues or, at its end, the unit stops facing its way.
void MotionOver(CUnitServer* c, CWorldServer* world) {
    CUnit* unit = UnitOf(c);
    if (MotionPathOf(c)->reached) {
        std::uint8_t wasFlag = FailedByte(c) == 1;
        MotionPathOf(c)->Clear();
        int time = WorldTime(ObjectWorld(c));
        Vector3 axis, back;
        UnitOf(c)->localRotation_.AxisY(&axis);
        Vector3* facing = NegateVector(&back, axis);
        if (MotionPathOf(c)->Start(&UnitOf(c)->localPosition_, facing, time)) UnitOf(c)->RefreshMotion();
        c->SetStanding(0);
        c->SetState(6);
        c->stoodBlocked = wasFlag;
        return;
    }
    if (!world->AIMap()->ContinuePath(unit, MotionPathOf(c))) {
        MotionPathOf(c)->Clear();
        int time = WorldTime(ObjectWorld(c));
        Vector3 axis, back;
        UnitOf(c)->localRotation_.AxisY(&axis);
        Vector3* facing = NegateVector(&back, axis);
        if (MotionPathOf(c)->Start(&UnitOf(c)->localPosition_, facing, time)) UnitOf(c)->RefreshMotion();
        c->SetStanding(0);
        c->SetState(6);
        c->stoodBlocked = 0;
        return;
    }
    UnitOf(c)->RefreshMotion();
    int state = c->state;
    if (state >= 0 && (state <= 1 || state == 2)) return;
    c->PlaySteps(c->IdleAnimation());
}

// Replans the path to the order's goal (+0x9C; its z, +0xA4, holds the range PathTo asked for), with
// the units around the blocked step lifted when `liftBlocker`.
void Replan(CUnitServer* c, CWorldServer* world, bool liftBlocker, const Vector3* position) {
    if (liftBlocker) {
        world->Sectors()->Remove(UnitOf(c));
        world->AIMap()->StampWaterPatch(position, 1);
        world->Sectors()->Add(UnitOf(c));
    }
    // The path's remaining cells (AIPath::cells, +0x78) emptied, as RemoveAll inlined.
    MotionPathOf(c)->cells.RemoveAll();

    float range = MotionPathOf(c)->goal.z;
    std::uint32_t saved = UnitOf(c)->altPassCosts;
    CWorldServer* context = ServerOf(c);
    MotionPathOf(c)->flag6E = UnitOf(c)->player ? 0 : 1;
    std::uint32_t current = UnitOf(c)->altPassCosts;
    UnitOf(c)->altPassCosts = saved;
    bool found = context->AIMap()->FindPath(UnitOf(c), &MotionPathOf(c)->goal, MotionPathOf(c), range, 0.0f);
    UnitOf(c)->altPassCosts = current;
    if (liftBlocker) {
        world->Sectors()->Remove(UnitOf(c));
        world->AIMap()->StampWaterPatch(position, 0);
        world->Sectors()->Add(UnitOf(c));
    }
    if (found) {
        UnitOf(c)->RefreshMotion();
        int state = c->state;
        if (state >= 0 && (state <= 1 || state == 2)) return;
        c->PlaySteps(c->IdleAnimation());
        return;
    }
    std::uint8_t wasFlag = FailedByte(c) == 1;
    c->StandStill();
    c->SetState(6);
    c->stoodBlocked = wasFlag;
}

// Running units of a player tire: the server object's mana (vitals +0x14) is their breath and drains
// by the maximum (+0x18) times a factor per tick; out of breath they stop running (message 0x1D).
// Then the tick's end.
void Exhaustion(CUnitServer* c) {
    CUnit* unit = UnitOf(c);
    if (unit->obstacle == 0) return;
    if (c->moveMode.mode != 2 || c->moveMode2.mode != 2) return;
    CPlayer* player = unit->player;
    if (c->moveMode.wasMode3 && player && player->godMode_ == 0 && TheServerWorld()->sessionKind_ != 3) {
        UnitVitals& vitals = unit->ServerCompanion()->vitals;
        float* stamina = &vitals.mana;
        const float* rate = &vitals.maxMana;
        float value;
        std::memcpy(&value, stamina, 4);
        EI_NAN_AUDIT(value);
        if (value == 0.0f) {
            c->moveMode2.wasMode3 = 0;
            int mode = (c->moveMode2.wasMode3 & 1) ? 3 : c->moveMode2.mode;
            if (mode != unit->moveMode2) {
                unit->moveMode2 = mode;
                UnitOf(c)->MarkChanged(6);
            }
        } else {
            if (Drain(stamina, rate)) {
                if (UnitOf(c)->player)
                    UnitOf(c)->player->Server()->SendObject0B(UnitOf(c), 0x1D);
                std::memset(stamina, 0, 4);
                c->moveMode2.wasMode3 = 0;
                int mode = (c->moveMode2.wasMode3 & 1) ? 3 : c->moveMode2.mode;
                if (mode != UnitOf(c)->moveMode2) {
                    UnitOf(c)->moveMode2 = mode;
                    UnitOf(c)->MarkChanged(6);
                }
            }
            UnitOf(c)->MarkChanged(0);
        }
    }
    c->UpdateSneak();
}

} // namespace

// Called by name from CUnitServer's update (combat_update.cpp).
void UpdateMotion(CUnitServer* c) {
    CWorldServer* world = ServerOf(c);
    Vector3 position;
    std::memcpy(&position, &UnitOf(c)->localPosition_.x, 12);
    ToHalfCells(&position.x);
    int time = WorldTime(world);
    bool turning = false;
    Vector3 direction;
    float speed;
    int cellX, cellY;
    bool going = UnitOf(c)->Motion()->Sample(world, &MotionPathOf(c)->motionOrder, TimeAsFloat(&time), &turning,
                              &position.x, &direction.x, &speed, &cellX, &cellY);
    bool motionOver = !going;
    FromHalfCells(&position.x);

    if (!turning && c->movingAnimation == 0) QueueNextAnimation(c);

    if (c->hitDirection != 0xFFFFFFFFu) {
        if (turning) {
            HitWhileTurning(c);
            return;
        }
        HitWhileMoving(c);
    }

    // Leave the sector map, see whether the unit can stand on the sampled cell, and if not, what
    // the blocking unit means for it.
    bool canStand;
    bool liftBlocker = false;
    // The game's "blocked" local is uninitialised: only CanStandAtFromHere (a cell inside the map) and the
    // blocker cases write it, so a unit that reached no cell, or a cell outside the map, decides on
    // whatever the stack held there (entry ESP - 0x98, left by earlier calls). Every value but
    // kBlockedReplan and kBlockedWait acts alike; here it is 0 (docs/notes/combat.md).
    int blocked = 0;
    if (cellX < 0) {
        canStand = true;
    } else {
        CSectorMap* sectors = world->Sectors();
        sectors->Remove(UnitOf(c));
        canStand = world->AIMap()->CanStandAtFromHere(UnitOf(c), cellX, cellY, &blocked);
        if (!canStand) {
            CUnit* blocker = world->AIMap()->BlockingUnitAt(UnitOf(c), &position);
            if (!blocker) {
                canStand = true;
            } else {
                int outcome = ResolveBlocker(c, blocker, &liftBlocker, blocked);
                if (outcome != -1) blocked = outcome;
            }
        }
        sectors->Add(UnitOf(c));
    }

    // An idle unit standing in a kind-3 occluder (while its motion order started (+0x4C) before
    // the world's +0x2804 and moveMode2's flag4 is clear) replans.
    bool replan = false;
    if (c->state == kStateIdle && world->AIMap()->HasKind3OccluderAt(&position) &&
        OrderOf(MotionPathOf(c))->startTime < world->AIMap()->footprintTick && c->moveMode2.flag4 == 0) {
        blocked = kBlockedReplan;
        replan = true;
    }
    if (!replan && canStand && world->Sectors()->Move(UnitOf(c), &position)) {
        SetObstacle(c, c->state == kStateIdle ? 1 : 0);
        UnitOf(c)->SetHeading(&direction.x);
        if (motionOver) MotionOver(c, world);
    } else if (blocked == kBlockedReplan) {
        Replan(c, world, liftBlocker, &position);
    } else if (blocked == kBlockedWait) {
        if (c->stats.movementKind == 2) c->animationHeld = 1;   // +0x240: the stats' +0x60
        else c->frozen = 1;
        if (UnitOf(c)->obstacle != 0) SetObstacle(c, 0);
    }
    Exhaustion(c);
}

EI_REWRITTEN(0x00547890, UpdateMotion);
