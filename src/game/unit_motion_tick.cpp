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
#include "game_singletons.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "player_messages.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_data.h"

#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

namespace {

// --- Float helpers -------------------------------------------------------------------------------

// The stamina drain step, a double in the exe.
constexpr double kDrainStep = 0.006666666666666666;

void ToHalfCells(float* xy) {
    const double factor = 2.0;
    xy[0] = static_cast<float>(factor * xy[0]);
    xy[1] = static_cast<float>(factor * xy[1]);
}

void FromHalfCells(float* xy) {
    const double factor = 0.5;
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

bool SpeedBelow(CMapObject* object, const float* than) {
    EI_NAN_AUDIT(object->MoveSpeed(), *than);
    return object->MoveSpeed() < *than;
}

void SpeedOf(CMapObject* object, float* out) { *out = static_cast<float>(object->MoveSpeed()); }

// *value -= *rate * the drain step (double); true when the unrounded result compares below 0.
bool Drain(float* value, const float* rate) {
    const double drained = static_cast<double>(*value) - *rate * kDrainStep;
    *value = static_cast<float>(drained);
    EI_NAN_AUDIT(drained);
    return drained < 0.0;
}

// The world's time as a float (what Sample is called with).
float TimeAsFloat(const int* time) { return static_cast<float>(*time); }

// --- Accessors -----------------------------------------------------------------------------------

CUnit* UnitOf(CUnitServer* c) { return c->unit; }
AnimationSteps* Queue(CUnitServer* c) { return &c->queue; }
int& QueueStart(CUnitServer* c) { return c->queue.extra; }
AIPath* MotionPathOf(CUnitServer* c) { return &c->motion; }
// The path's `failed` byte as the game reads it: compared with 1, so read as a byte, not a bool.
std::uint8_t FailedByte(CUnitServer* c) { return c->motion.failed; }
CWorldServer* ServerOf(CUnitServer* c) { return c->ServerWorld(); }
int WorldTime(const CWorld* world) { return world->tick_; }

constexpr int kStateIdle = 0;
constexpr int kStateCasting = 5;
constexpr int kBlockedReplan = 1;
constexpr int kBlockedWait = 2;

// The counted "waiting for" link (followsLeader / followedLeader: the fields the chase uses for the leader it
// follows).
void SetWaitingFor(CUnitServer* c, CUnit* blocker) {
    c->followsLeader = 1;
    c->followedLeader.Assign(blocker);
}
void ClearWaitingFor(CUnitServer* controller) {
    controller->followsLeader = 0;
    controller->followedLeader.Reset();
}

void SetObstacle(CUnitServer* c, int obstacle) {
    CWorldServer* world = ServerOf(c);
    CUnit* unit = UnitOf(c);
    if (static_cast<int>(unit->obstacle) == obstacle) return;
    world->aiMap_.sectorMap.Remove(unit);
    UnitOf(c)->obstacle = static_cast<std::uint32_t>(obstacle);
    world->aiMap_.sectorMap.Add(UnitOf(c));
    UnitOf(c)->MarkChanged(7);
}

void QueueNextAnimation(CUnitServer* c) {
    *Queue(c) = *c->NextAnimation();
    if (QueueStart(c) == -1) QueueStart(c) = WorldTime(ObjectWorld(c));
    c->PopFinishedSteps();
    c->ShowCurrentStep();
    c->movingAnimation = 1;
}

// A unit hit while turning on the spot: the death (or hit) animation of the last blow is played when
// it fits. Returns from the tick.
void HitWhileTurning(CUnitServer* c) {
    AnimationSteps steps;
    if (c->moveMode.mode != 0 || c->state != 0) {
        c->BuildDeathAnimation(&steps, c->hitDirection, 0, 0);
        if (c->CanReactFor(steps.Duration())) {
            c->keepHitDirection = 1;
            c->savedQueue = *Queue(c);
            c->PlaySteps(&steps);
            c->animationHeld = 1;
        }
    }
}

// A hit while moving: unless the queue's first animation (a hit) is still running, the current animation
// followed by the fall of the last blow replaces the queue.
void HitWhileMoving(CUnitServer* c) {
    if (!Queue(c)->empty()) {
        std::uint32_t key = (static_cast<std::uint32_t>(Queue(c)->size()) & 0xFFFFFF00u) |
                            (*Queue(c))[0].animation;
        const FigureAnimation* entry = UnitOf(c)->AnimationEntry(key);
        if (entry && entry->Kind() == AnimationKind::Hit) {
            std::uint32_t ends = entry->frameCount + static_cast<std::uint32_t>(QueueStart(c));
            if (ends > static_cast<std::uint32_t>(WorldTime(ObjectWorld(c)))) return;
        }
    }
    AnimationSteps current;
    AnimationSteps fall;
    c->BuildMoveSteps(&current, 0);
    c->BuildDeathAnimation(&fall, c->hitDirection, 0, 1);
    for (const AnimationStep& step : current) {
        // The dwords at step + 1 / step + 0 are passed; AddEmote / AddStep keep their low byte only (the
        // variant / the animation).
        if (step.animation == AnimationStep::kNone) {
            fall.AddEmote(step.variant);
        } else if (step.variant == AnimationStep::kNone) {
            fall.AddStep(step.animation, step.duration);
        } else {
            fall.push_back(step);
        }
    }
    *Queue(c) = fall;
    if (QueueStart(c) == -1) QueueStart(c) = WorldTime(ObjectWorld(c));
    c->PopFinishedSteps();
    c->ShowCurrentStep();
}

// The unit blocking the step: whether to wait for it (2), give way and replan around it (1), or
// neither (0, then the unit stays). Sets `liftBlocker` when the units around the step must be
// lifted while replanning.
int ResolveBlocker(CUnitServer* c, CUnit* blocker, bool* liftBlocker, int reason) {
    CUnit* self = UnitOf(c);
    CPlayer* selfPlayer = self->player;
    if (selfPlayer && blocker->player == selfPlayer &&
        blocker->ServerController()->brain.LeaderOrFollowed() == self && g_serverWorld->tick_ < c->waitFrame + 2) {
        return kBlockedWait;   // a party member following this unit
    }
    if (c->followsLeader && c->followedLeader == blocker) return kBlockedWait;

    CUnitServer* other = blocker->ServerController();
    auto isAction = [](UnitOrderKind kind) { return kind == UnitOrderKind::Attack || kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn; };
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

// The motion is over: a path with no cells left (`reached`) stops facing its way; otherwise the path
// continues or, at its end, the unit stops facing its way.
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
    if (!world->aiMap_.ContinuePath(unit, MotionPathOf(c))) {
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

// Replans the path to the order's goal (its z holds the range PathTo asked for), with the units around the
// blocked step lifted when `liftBlocker`.
void Replan(CUnitServer* c, CWorldServer* world, bool liftBlocker, const Vector3* position) {
    if (liftBlocker) {
        world->aiMap_.sectorMap.Remove(UnitOf(c));
        world->aiMap_.StampWaterPatch(position, 1);
        world->aiMap_.sectorMap.Add(UnitOf(c));
    }
    MotionPathOf(c)->cells.clear();

    float range = MotionPathOf(c)->goal.z;
    std::uint32_t saved = UnitOf(c)->altPassCosts;
    CWorldServer* context = ServerOf(c);
    MotionPathOf(c)->mayExcludeRegions = UnitOf(c)->player ? 0 : 1;
    std::uint32_t current = UnitOf(c)->altPassCosts;
    UnitOf(c)->altPassCosts = saved;
    bool found = context->aiMap_.FindPath(UnitOf(c), &MotionPathOf(c)->goal, MotionPathOf(c), range, 0.0f);
    UnitOf(c)->altPassCosts = current;
    if (liftBlocker) {
        world->aiMap_.sectorMap.Remove(UnitOf(c));
        world->aiMap_.StampWaterPatch(position, 0);
        world->aiMap_.sectorMap.Add(UnitOf(c));
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

// Running units of a player tire: the server object's mana is their breath and drains
// by the maximum times a factor per tick; out of breath they stop running (message 0x1D).
// Then the tick's end.
void Exhaustion(CUnitServer* c) {
    CUnit* unit = UnitOf(c);
    if (unit->obstacle == 0) return;
    if (c->moveMode.mode != 2 || c->moveMode2.mode != 2) return;
    CPlayer* player = unit->player;
    if (c->moveMode.wasMode3 && player && player->godMode_ == 0 && g_serverWorld->sessionKind_ != 3) {
        UnitVitals& vitals = unit->ServerCompanion()->vitals;
        float* stamina = &vitals.mana;
        const float* rate = &vitals.maxMana;
        float value;
        value = *stamina;
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
                    UnitOf(c)->player->Server()->SendObject0B(UnitOf(c), AckKind::OutOfStamina);
                *stamina = 0.0f;
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
    bool going = UnitOf(c)->motion.Sample(world, &MotionPathOf(c)->motionOrder, TimeAsFloat(&time), &turning,
                              &position.x, &direction.x, &speed, &cellX, &cellY);
    bool motionOver = !going;
    FromHalfCells(&position.x);

    if (!turning && c->movingAnimation == 0) QueueNextAnimation(c);

    if (c->hitDirection != CUnitServer::kNoHitDirection) {
        if (turning) {
            HitWhileTurning(c);
            return;
        }
        HitWhileMoving(c);
    }

    bool canStand;
    bool liftBlocker = false;
    // The game's "blocked" local is uninitialised: only CanStandAtFromHere (a cell inside the map) and the
    // blocker cases write it, so a unit that reached no cell, or a cell outside the map, decides on whatever
    // the stack held there. Every value but kBlockedReplan and kBlockedWait acts alike; here it is 0
    // (docs/notes/combat.md).
    int blocked = 0;
    if (cellX < 0) {
        canStand = true;
    } else {
        CSectorMap* sectors = &world->aiMap_.sectorMap;
        sectors->Remove(UnitOf(c));
        canStand = world->aiMap_.CanStandAtFromHere(UnitOf(c), cellX, cellY, &blocked);
        if (!canStand) {
            CUnit* blocker = world->aiMap_.BlockingUnitAt(UnitOf(c), &position);
            if (!blocker) {
                canStand = true;
            } else {
                int outcome = ResolveBlocker(c, blocker, &liftBlocker, blocked);
                if (outcome != -1) blocked = outcome;
            }
        }
        sectors->Add(UnitOf(c));
    }

    // An idle unit standing in a kind-3 occluder (while its motion order started before the AI map's
    // footprintTick and moveMode2's onGuard is clear) replans.
    bool replan = false;
    if (c->state == kStateIdle && world->aiMap_.HasSpellAreaAt(&position) &&
        OrderOf(MotionPathOf(c))->startTime < world->aiMap_.footprintTick && c->moveMode2.onGuard == 0) {
        blocked = kBlockedReplan;
        replan = true;
    }
    if (!replan && canStand && world->aiMap_.sectorMap.Move(UnitOf(c), &position)) {
        SetObstacle(c, c->state == kStateIdle ? 1 : 0);
        UnitOf(c)->SetHeading(&direction.x);
        if (motionOver) MotionOver(c, world);
    } else if (blocked == kBlockedReplan) {
        Replan(c, world, liftBlocker, &position);
    } else if (blocked == kBlockedWait) {
        if (c->stats.movementKind == 2) c->animationHeld = 1;
        else c->frozen = 1;
        if (UnitOf(c)->obstacle != 0) SetObstacle(c, 0);
    }
    Exhaustion(c);
}
