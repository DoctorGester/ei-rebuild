// The unit movement tick (called twice for the controller, UnitServer): samples the unit's motion order at
// the world's time, starts the next animation of a finished motion, plays a pending fall or death
// animation, then moves the unit to the sampled position on the sector map; a blocked step either waits
// (the blocker is a party member leading, or the unit already waits for it), yields to a slower unit, or
// replans the path. See docs/notes/combat.md ("Movement tick") and docs/notes/ai.md ("How a unit moves").
#include "game/ai/ai_map.h"
#include "game/ai/ai_motion.h"
#include "game/combat/combat.h"
#include "game/combat/combat_internal.h"
#include "engine/core/core_maths.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/ai/sector_map.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"

#include "lib/vecmath.h"


#include <cstdint>
#include <cstring>

namespace {

constexpr float kQueueWaitLimit = 60.0f;   // squared distance a blocking ally may be away

// --- Float helpers -------------------------------------------------------------------------------

// The stamina drain step.
constexpr float kDrainStep = 1.0f / 150.0f;

void FromHalfCells(float* xy) {
    xy[0] = 0.5f * xy[0];
    xy[1] = 0.5f * xy[1];
}

// Whether |a - b|^2, summed as (dz^2 + dy^2) + dx^2, is above *limit (false for NaN).
bool FartherThan(const float* a, const float* b, const float* limit) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return LengthSquared(Vector3{dx, dy, dz}) > *limit;
}

bool SpeedBelow(MapObject* object, const float* than) {
    return object->MoveSpeed() < *than;
}

void SpeedOf(MapObject* object, float* out) { *out = object->MoveSpeed(); }

// *value -= *rate * the drain step; true when the result is below 0.
bool Drain(float* value, const float* rate) {
    const float drained = *value - (*rate * kDrainStep);
    *value = drained;
    return drained < 0.0f;
}

// The world's time as a float (what Sample is called with).
float TimeAsFloat(const int* time) { return static_cast<float>(*time); }

// --- Accessors -----------------------------------------------------------------------------------


constexpr int kBlockedReplan = 1;
constexpr int kBlockedWait = 2;

// The counted "waiting for" link (followsLeader / followedLeader: the fields the chase uses for the leader it
// follows).
void SetWaitingFor(UnitServer* c, Unit* blocker) {
    c->followsLeader = 1;
    c->followedLeader.Assign(blocker);
}
void ClearWaitingFor(UnitServer* controller) {
    controller->followsLeader = 0;
    controller->followedLeader.Reset();
}

void SetObstacle(UnitServer* c, int obstacle) {
    WorldServer* world = c->ServerWorld();
    Unit* unit = c->unit;
    if (static_cast<int>(unit->obstacle) == obstacle) return;
    world->aiMap.sectorMap.Remove(unit);
    c->unit->obstacle = static_cast<std::uint32_t>(obstacle);
    world->aiMap.sectorMap.Add(c->unit);
    c->unit->MarkChanged(NetField::Motion);
}

void QueueNextAnimation(UnitServer* c) {
    *&c->queue = *c->NextAnimation();
    if (c->queue.extra == -1) c->queue.extra = c->OwningWorld()->tick;
    c->PopFinishedSteps();
    c->ShowCurrentStep();
    c->movingAnimation = 1;
}

// A unit hit while turning on the spot: the death (or hit) animation of the last blow is played when
// it fits. Returns from the tick.
void HitWhileTurning(UnitServer* c) {
    AnimationSteps steps;
    if (c->moveMode.mode != MoveMode::Lie || c->state != UnitState::Idle) {
        c->BuildDeathAnimation(&steps, c->hitDirection, 0, 0);
        if (c->CanReactFor(steps.Duration())) {
            c->keepHitDirection = 1;
            c->savedQueue = *&c->queue;
            c->PlaySteps(&steps);
            c->animationHeld = 1;
        }
    }
}

// A hit while moving: unless the queue's first animation (a hit) is still running, the current animation
// followed by the fall of the last blow replaces the queue.
void HitWhileMoving(UnitServer* c) {
    if (!c->queue.steps.empty()) {
        std::uint32_t key = (static_cast<std::uint32_t>(c->queue.steps.size()) & 0xFFFFFF00u) |
                            c->queue.steps[0].animation;
        const FigureAnimation* entry = c->unit->AnimationEntry(key);
        if (entry && entry->Kind() == AnimationKind::Hit) {
            std::uint32_t ends = entry->frameCount + static_cast<std::uint32_t>(c->queue.extra);
            if (ends > static_cast<std::uint32_t>(c->OwningWorld()->tick)) return;
        }
    }
    AnimationSteps current;
    AnimationSteps fall;
    c->BuildMoveSteps(&current, 0);
    c->BuildDeathAnimation(&fall, c->hitDirection, 0, 1);
    for (const AnimationStep& step : current.steps) {
        // The dwords at step + 1 / step + 0 are passed; AddEmote / AddStep keep their low byte only (the
        // variant / the animation).
        if (step.animation == AnimationStep::kNone) {
            fall.AddEmote(step.variant);
        } else if (step.variant == AnimationStep::kNone) {
            fall.AddStep(step.animation, step.duration);
        } else {
            fall.steps.push_back(step);
        }
    }
    c->queue = fall;
    if (c->queue.extra == -1) c->queue.extra = c->OwningWorld()->tick;
    c->PopFinishedSteps();
    c->ShowCurrentStep();
}

// The unit blocking the step: whether to wait for it (2), give way and replan around it (1), or
// neither (0, then the unit stays). Sets `liftBlocker` when the units around the step must be
// lifted while replanning.
int ResolveBlocker(UnitServer* c, Unit* blocker, bool* liftBlocker, int reason) {
    Unit* self = c->unit;
    Player* selfPlayer = self->player;
    if (selfPlayer && blocker->player == selfPlayer &&
        blocker->ServerController()->brain.LeaderOrFollowed() == self && g_serverWorld.tick < c->waitFrame + 2) {
        return kBlockedWait;   // a party member following this unit
    }
    if (c->followsLeader && c->followedLeader == blocker) return kBlockedWait;

    UnitServer* other = blocker->ServerController();
    auto isAction = [](UnitOrderKind kind) { return kind == UnitOrderKind::Attack || kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn; };
    bool sameAttack = blocker->side == self->side && isAction(c->action.kind) &&
                      isAction(other->action.kind) && other->action.target == c->action.target &&
                      blocker->sizeClass == self->sizeClass && other->state != UnitState::Acting;
    if (sameAttack) {
        MapObject* target = c->action.target;
        sameAttack = FartherThan(&target->localPosition.x, &self->localPosition.x, &kQueueWaitLimit);
    }
    if (sameAttack) {
        // Both go for the same target: wait behind the other unless the waiting would go round in a
        // circle back to this unit, which breaks the circle and replans.
        UnitServer* link = other;
        while (link->followsLeader) {
            Unit* waitsFor = link->followedLeader;
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
void MotionOver(UnitServer* c, WorldServer* world) {
    Unit* unit = c->unit;
    if (c->motion.reached) {
        std::uint8_t wasFlag = c->motion.failed == 1;
        c->motion.Clear();
        int time = c->OwningWorld()->tick;
        Vector3 axis = c->unit->localRotation.AxisY();
        if (c->motion.Start(c->unit->localPosition, -axis, time)) c->unit->RefreshMotion();
        c->ClearStanding();
        c->SetState(UnitState::Stopped);
        c->stoodBlocked = wasFlag;
        return;
    }
    if (!world->aiMap.ContinuePath(unit, &c->motion)) {
        c->motion.Clear();
        int time = c->OwningWorld()->tick;
        Vector3 axis = c->unit->localRotation.AxisY();
        if (c->motion.Start(c->unit->localPosition, -axis, time)) c->unit->RefreshMotion();
        c->ClearStanding();
        c->SetState(UnitState::Stopped);
        c->stoodBlocked = 0;
        return;
    }
    c->unit->RefreshMotion();
    UnitState state = c->state;
    if (state == UnitState::Idle || state == UnitState::Moving || state == UnitState::Turning) return;
    c->PlaySteps(c->IdleAnimation());
}

// Replans the path to the order's goal (its z holds the range PathTo asked for), with the units around the
// blocked step lifted when `liftBlocker`.
void Replan(UnitServer* c, WorldServer* world, bool liftBlocker, const Vector3& position) {
    if (liftBlocker) {
        world->aiMap.sectorMap.Remove(c->unit);
        world->aiMap.StampWaterPatch(position, 1);
        world->aiMap.sectorMap.Add(c->unit);
    }
    c->motion.cells.clear();

    float range = c->motion.goal.z;
    std::uint32_t saved = c->unit->altPassCosts;
    WorldServer* context = c->ServerWorld();
    c->motion.mayExcludeRegions = c->unit->player ? 0 : 1;
    std::uint32_t current = c->unit->altPassCosts;
    c->unit->altPassCosts = saved;
    bool found = context->aiMap.FindPath(c->unit, c->motion.goal, &c->motion, range, 0.0f);
    c->unit->altPassCosts = current;
    if (liftBlocker) {
        world->aiMap.sectorMap.Remove(c->unit);
        world->aiMap.StampWaterPatch(position, 0);
        world->aiMap.sectorMap.Add(c->unit);
    }
    if (found) {
        c->unit->RefreshMotion();
        UnitState state = c->state;
        if (state == UnitState::Idle || state == UnitState::Moving || state == UnitState::Turning) return;
        c->PlaySteps(c->IdleAnimation());
        return;
    }
    std::uint8_t wasFlag = c->motion.failed == 1;
    c->StandStill();
    c->SetState(UnitState::Stopped);
    c->stoodBlocked = wasFlag;
}

// Running units of a player tire: the server object's mana is their breath and drains
// by the maximum times a factor per tick; out of breath they stop running (message 0x1D).
// Then the tick's end.
void Exhaustion(UnitServer* c) {
    Unit* unit = c->unit;
    if (unit->obstacle == 0) return;
    if (c->moveMode.mode != MoveMode::Walk || c->moveMode2.mode != MoveMode::Walk) return;
    Player* player = unit->player;
    if (c->moveMode.wasMode3 && player && player->godMode == 0 && g_serverWorld.zoneMode != ZoneMode::Base) {
        UnitVitals& vitals = unit->Vitals();
        float* stamina = &vitals.mana;
        const float* rate = &vitals.maxMana;
        float value;
        value = *stamina;
        if (value == 0.0f) {
            c->moveMode2.wasMode3 = 0;
            MoveMode mode = (c->moveMode2.wasMode3 & 1) ? MoveMode::Run : c->moveMode2.mode;
            if (mode != unit->moveMode2) {
                unit->moveMode2 = mode;
                c->unit->MarkChanged(NetField::Modes);
            }
        } else {
            if (Drain(stamina, rate)) {
                if (c->unit->player)
                    c->unit->player->Server()->SendUnitSpeech(c->unit, AckKind::OutOfStamina);
                *stamina = 0.0f;
                c->moveMode2.wasMode3 = 0;
                MoveMode mode = (c->moveMode2.wasMode3 & 1) ? MoveMode::Run : c->moveMode2.mode;
                if (mode != c->unit->moveMode2) {
                    c->unit->moveMode2 = mode;
                    c->unit->MarkChanged(NetField::Modes);
                }
            }
            c->unit->MarkChanged(NetField::Vitals);
        }
    }
    c->UpdateSneak();
}

} // namespace

void UpdateMotion(UnitServer* c) {
    WorldServer* world = c->ServerWorld();
    int time = world->tick;
    MotionSample sample = c->unit->motion.Sample(world, &c->motion.motionOrder, TimeAsFloat(&time));
    Vector3 position = WithZ(sample.position, c->unit->localPosition.z);
    bool turning = sample.turning;
    Vector3 direction = sample.direction;
    const Point cell = sample.cell;
    bool motionOver = !sample.alive;
    FromHalfCells(&position.x);

    if (!turning && c->movingAnimation == 0) QueueNextAnimation(c);

    if (c->hitDirection != UnitServer::kNoHitDirection) {
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
    if (cell.x < 0) {
        canStand = true;
    } else {
        SectorMap* sectors = &world->aiMap.sectorMap;
        sectors->Remove(c->unit);
        canStand = world->aiMap.CanStandAtFromHere(c->unit, cell, &blocked);
        if (!canStand) {
            Unit* blocker = world->aiMap.BlockingUnitAt(c->unit, position);
            if (!blocker) {
                canStand = true;
            } else {
                int outcome = ResolveBlocker(c, blocker, &liftBlocker, blocked);
                if (outcome != -1) blocked = outcome;
            }
        }
        sectors->Add(c->unit);
    }

    // An idle unit standing in a kind-3 occluder (while its motion order started before the AI map's
    // footprintTick and moveMode2's onGuard is clear) replans.
    bool replan = false;
    if (c->state == UnitState::Idle && world->aiMap.HasSpellAreaAt(position) &&
        OrderOf(&c->motion)->startTime < world->aiMap.footprintTick && c->moveMode2.onGuard == 0) {
        blocked = kBlockedReplan;
        replan = true;
    }
    if (!replan && canStand && world->aiMap.sectorMap.Move(c->unit, position)) {
        SetObstacle(c, c->state == UnitState::Idle ? 1 : 0);
        c->unit->SetHeading(direction);
        if (motionOver) MotionOver(c, world);
    } else if (blocked == kBlockedReplan) {
        Replan(c, world, liftBlocker, position);
    } else if (blocked == kBlockedWait) {
        if (c->stats.movementKind == 2) c->animationHeld = 1;
        else c->frozen = 1;
        if (c->unit->obstacle != 0) SetObstacle(c, 0);
    }
    Exhaustion(c);
}
