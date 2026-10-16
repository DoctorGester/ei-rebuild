// The unit movement tick (00547890; called twice by 005499F0 for the controller, CUnitServer):
// samples the unit's motion order at the world's time, starts the next animation of a finished
// motion, plays a pending fall or death animation, then moves the unit to the sampled position on
// the sector map; a blocked step either waits (the blocker is a party member leading, or the unit
// already waits for it), yields to a slower unit, or replans the path. See docs/notes/combat.md
// ("Movement tick") and docs/notes/ai.md ("How a unit moves").
#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"
#include "combat_plan.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "player_messages.h"
#include "unit_ai.h"
#include "world_data.h"
#include "unit.h"
#include "world.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "game_data.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

namespace {

// --- Forwarders (thiscall as __fastcall with the unused EDX) --------------------------------------

// 00455940 the owning manager's context (the world server); 00455950 the object's world.
// 005C7430 UnitMotion::Sample (ai_motion.cpp) on CUnit + 0x27C.
// The controller's animation helpers (not rewritten): 00551400 the next animation of the order,
// 00546B40 / 00546C10 restart the animation queue, 00549D00 whether an animation of `ticks` can be
// played now, 00546C40 play steps, 005511A0 the current animation as steps, 00550F30 the idle
// animation, 00547280 / 00547090 / 005467B0 motion updates, 00548520 the end of the tick.
// 00550A80 CUnitServer::BuildDeathAnimation (combat_steps.cpp).
// Animation lists: 00556850 constructor, 00557E70 destructor, 005483C0 assign (the queue +0x24 and
// the copy +0x148), 00556950 append, 00544E40 total duration (-1 if a step is open-ended), 00558920
// GetAt into `out`, 00544D50 add an emote, 00544CE0 add a step, 00558950 SetAtGrow.
__declspec(naked) AnimationSteps* __fastcall StepsConstruct(AnimationSteps*) EI_ORIGINAL(0x00556850)
inline void __fastcall StepsDestroy(AnimationSteps* s) { AnimationStepsDestroyOriginal(s); }
__declspec(naked) void __fastcall StepsGetAt(const AnimationSteps*, void*, AnimationStep* /*out*/, int) EI_ORIGINAL(0x00558920)
void __fastcall StepsEmote(AnimationSteps* a0, void*, std::uint32_t a2) { static_cast<AnimationSteps*>((void*)a0)->AddEmote((int)a2); }
void __fastcall StepsAddStep(AnimationSteps* a0, void*, std::uint32_t a2, int a3) { static_cast<AnimationSteps*>((void*)a0)->AddStep((int)a2, a3); }
// CUnit: 00530240 its animation entry for a key (combat_steps.cpp), 0050E9F0 MarkChanged
// (unit.cpp), 0052E9D0 the path speeds, 00402220 face a direction.
// The sector map (world + 0x294): 005C1010 Remove, 005C0DF0 Add, 005C11D0 Move (patched by Rebirth).
// The AI map (world + 0x170, ai_map.cpp / ai_movement.cpp / ai_path.cpp): 005B81B0
// CanStandAtFromHere, 005B7BB0 BlockingUnitAt, 005BBDB0 HasKind3OccluderAt, 005BB350 FindPath,
// 005BB900 ContinuePath, 005B7EF0 lifts (1) / restores (0) the units around a point (not
// rewritten).
// The motion order (+0x3C, an AIPath): 005B23A0 Clear, 005C7B00 Start (ai_motion.cpp).
// 0040F560 Quaternion::AxisY, 00402200 NegateVector (core_maths.cpp).
// 005D5E90 the AI brain's leader (unit_ai.cpp); 0066D6E0 a party message.

// --- Float helpers -------------------------------------------------------------------------------

// The exe's constants: 0073EEE4 the cell size, 0073B86C 0.5, 0073B84C 1.0, 0073F088 the stamina
// drain step (double).
float CellSize() { return ei::GameData<const float>(0x0073EEE4); }
float Half() { return ei::GameData<const float>(0x0073B86C); }
float One() { return ei::GameData<const float>(0x0073B84C); }
double DrainStep() { return ei::GameData<const double>(0x0073F088); }

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

// Whether `object`'s speed (virtual slot 59, unrounded) compares below *than (or unordered).
bool SpeedBelow(CEngineObject* object, const float* than) {
    return !(static_cast<CMapObject*>(object)->MoveSpeed() >= *than);
}

// The speed rounded to float.
void SpeedOf(CEngineObject* object, float* out) { *out = static_cast<float>(static_cast<CMapObject*>(object)->MoveSpeed()); }

// *value -= *rate * (double)[0073F088]; true when the unrounded result compares below 0 (or is NaN).
bool Drain(float* value, const float* rate) {
    const double drained = static_cast<double>(*value) - *rate * DrainStep();
    *value = static_cast<float>(drained);
    return !(drained >= 0.0);
}

// FILD of the world's time, FSTP to float (the time Sample is called with).
float TimeAsFloat(const int* time) { return static_cast<float>(*time); }

// --- Accessors -----------------------------------------------------------------------------------

CUnit* UnitOf(CUnitServer* c) { return c->unit; }
AnimationSteps* Queue(CUnitServer* c) { return &c->queue; }
int& QueueStart(CUnitServer* c) { return c->queue.extra; }
AIPath* MotionPathOf(CUnitServer* c) { return c->MotionPath(); }
// The path's `failed` byte as the original reads it: compared with 1, so read as a byte, not a bool.
std::uint8_t FailedByte(CUnitServer* c) { return reinterpret_cast<const std::uint8_t&>(c->MotionPath()->failed); }
// The world server (ManagerContextOf) and an object's world (ObjectWorld).
CWorldServer* ServerOf(CUnitServer* c) { return static_cast<CWorldServer*>(ManagerContextOf(c)); }
int WorldTime(void* world) { return static_cast<CWorld*>(world)->tick_; }
CPlayer* PlayerOf(CUnit* unit) { return reinterpret_cast<CPlayer*>(unit->player); }
CUnitServer* ServerControllerOf(CUnit* unit) { return static_cast<CUnitServer*>(unit->controller); }
int CurrentFrame() { return *reinterpret_cast<const int*>(0x007AFFC4); }
int GameMode() { return *reinterpret_cast<const int*>(0x007C2CC4); }

constexpr int kStateIdle = 0;
constexpr int kStateCasting = 5;
constexpr int kBlockedReplan = 1;
constexpr int kBlockedWait = 2;

// The counted "waiting for" link (followsLeader +0x1D8, followedLeader +0x1DC: the fields the
// chase uses for the leader it follows).
void SetWaitingFor(CUnitServer* c, CEngineObject* blocker) {
    c->followsLeader = 1;
    CEngineObject* old = c->followedLeader;
    c->followedLeader = blocker;
    ++blocker->refCount_;
    if (old) old->Release();
}
void ClearWaitingFor(CUnitServer* controller) {
    controller->followsLeader = 0;
    CEngineObject* old = controller->followedLeader;
    controller->followedLeader = nullptr;
    if (old) old->Release();
}

// The unit is on the map as an obstacle (+0x278) or not: re-registered when that changes.
void SetObstacle(CUnitServer* c, int obstacle) {
    CWorldServer* world = ServerOf(c);
    CUnit* unit = UnitOf(c);
    if (static_cast<int>(unit->obstacle) == obstacle) return;
    SectorMapRemove(world->Sectors(), nullptr, unit);
    UnitOf(c)->obstacle = static_cast<std::uint32_t>(obstacle);
    SectorMapAdd(world->Sectors(), nullptr, UnitOf(c));
    static_cast<CMapObject*>(UnitOf(c))->MarkChanged(7);
}

// The motion order's next animation once the current motion is over.
void QueueNextAnimation(CUnitServer* c) {
    AnimationSteps* next = c->NextAnimation();
    StepsAppend(Queue(c), nullptr, next);
    QueueStart(c) = next->extra;
    StepsCopyB(Queue(c), nullptr, next);
    if (QueueStart(c) == -1) QueueStart(c) = WorldTime(ObjectWorld(c));
    c->PopFinishedSteps();
    c->ShowCurrentStep();
    c->movingAnimation = 1;
}

// A unit hit while turning on the spot: the death (or hit) animation of the last blow is played when
// it fits. Returns from the tick.
void HitWhileTurning(CUnitServer* c) {
    AnimationSteps steps;
    StepsConstruct(&steps);
    if (c->moveMode.mode != 0 || c->state != 0) {
        c->BuildDeathAnimation(&steps, c->hitDirection, 0, 0);
        if (c->CanReactFor(steps.Duration())) {
            c->keepHitDirection = 1;
            StepsCopyB(&c->savedQueue, nullptr, Queue(c));
            c->PlaySteps(&steps);
            c->animationHeld = 1;
        }
    }
    StepsDestroy(&steps);
}

// A hit while moving: unless the queue's first animation (an attack, flags 0x200000 under
// 0x3C0000) is still running, the current animation followed by the fall of the last blow
// replaces the queue.
void HitWhileMoving(CUnitServer* c) {
    if (Queue(c)->count > 0) {
        std::uint32_t key = (static_cast<std::uint32_t>(Queue(c)->count) & 0xFFFFFF00u) |
                            Queue(c)->steps[0].animation;
        const auto* entry = reinterpret_cast<const FigureAnimation*>(UnitOf(c)->AnimationEntry(key));
        if (entry && (entry->key & 0x3C0000) == 0x200000) {
            std::uint32_t ends = entry->frameCount + static_cast<std::uint32_t>(QueueStart(c));
            if (ends > static_cast<std::uint32_t>(WorldTime(ObjectWorld(c)))) return;
        }
    }
    AnimationSteps current;
    StepsConstruct(&current);
    AnimationSteps fall;
    StepsConstruct(&fall);
    c->BuildMoveSteps(&current, 0);
    c->BuildDeathAnimation(&fall, c->hitDirection, 0, 1);
    for (int i = 0; i < current.count; ++i) {
        AnimationStep step;
        StepsGetAt(&current, nullptr, &step, i);
        if (step.animation == 0xFF) {
            std::uint32_t emote;
            std::memcpy(&emote, reinterpret_cast<const char*>(&step) + 1, 4);
            StepsEmote(&fall, nullptr, emote);
        } else if (step.variant == 0xFF) {
            std::uint32_t head;
            std::memcpy(&head, &step, 4);
            StepsAddStep(&fall, nullptr, head, step.duration);
        } else {
            StepsSetAtGrow(&fall, nullptr, fall.count, &step);
        }
    }
    StepsCopyB(Queue(c), nullptr, &fall);
    StepsCopyB(Queue(c), nullptr, &fall);
    if (QueueStart(c) == -1) QueueStart(c) = WorldTime(ObjectWorld(c));
    c->PopFinishedSteps();
    c->ShowCurrentStep();
    StepsDestroy(&fall);
    StepsDestroy(&current);
}

// The unit blocking the step: whether to wait for it (2), give way and replan around it (1), or
// neither (0, then the unit stays). Sets `liftBlocker` when the units around the step must be
// lifted while replanning.
int ResolveBlocker(CUnitServer* c, CUnit* blocker, bool* liftBlocker, int reason) {
    CUnit* self = UnitOf(c);
    char* selfPlayer = self->player;
    if (selfPlayer && blocker->player == selfPlayer &&
        ServerControllerOf(blocker)->brain.LeaderOrFollowed() == self && CurrentFrame() < c->waitFrame + 2) {
        return kBlockedWait;   // a party member following this unit
    }
    if (c->followsLeader && c->followedLeader == blocker) return kBlockedWait;

    CUnitServer* other = ServerControllerOf(blocker);
    auto isAction = [](int kind) { return kind == 3 || kind == 4 || kind == 5; };
    bool sameAttack = blocker->side == self->side && isAction(c->action.kind) &&
                      isAction(other->action.kind) && other->action.target == c->action.target &&
                      blocker->sizeClass == self->sizeClass && other->state != kStateCasting;
    if (sameAttack) {
        auto* target = static_cast<CMapObject*>(c->action.target);
        sameAttack = FartherThan(&target->position.x, &self->position.x, &worlddata::QueueWaitLimitRef());
    }
    if (sameAttack) {
        // Both go for the same target: wait behind the other unless the waiting would go round in a
        // circle back to this unit, which breaks the circle and replans.
        CUnitServer* link = other;
        while (link->followsLeader) {
            auto* waitsFor = static_cast<CUnit*>(link->followedLeader);
            if (!waitsFor) break;
            if (waitsFor == self) {
                ClearWaitingFor(other);
                ClearWaitingFor(c);
                *liftBlocker = true;
                return kBlockedReplan;
            }
            link = ServerControllerOf(waitsFor);
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
        PathClear(MotionPathOf(c));
        int time = WorldTime(ObjectWorld(c));
        Vector3 axis, back;
        QuaternionAxisY(&UnitOf(c)->orientation, nullptr, &axis);
        Vector3* facing = Negate(&back, &axis);
        if (MotionPathOf(c)->Start(&UnitOf(c)->position, facing, time)) UnitOf(c)->RefreshMotion();
        c->SetStanding(0);
        c->SetState(6);
        c->stoodBlocked = wasFlag;
        return;
    }
    if (!world->AIMap()->ContinuePath(unit, MotionPathOf(c))) {
        PathClear(MotionPathOf(c));
        int time = WorldTime(ObjectWorld(c));
        Vector3 axis, back;
        QuaternionAxisY(&UnitOf(c)->orientation, nullptr, &axis);
        Vector3* facing = Negate(&back, &axis);
        if (MotionPathOf(c)->Start(&UnitOf(c)->position, facing, time)) UnitOf(c)->RefreshMotion();
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
        SectorMapRemove(world->Sectors(), nullptr, UnitOf(c));
        world->AIMap()->StampWaterPatch(position, 1);
        SectorMapAdd(world->Sectors(), nullptr, UnitOf(c));
    }
    // The path's remaining cells (AIPath::cells, +0x78) emptied, as RemoveAll inlined.
    MotionPathOf(c)->cells.RemoveAll();

    float range = MotionPathOf(c)->goal.z;
    std::uint32_t saved = UnitOf(c)->altPassCosts;
    CWorldServer* context = ServerOf(c);
    MotionPathOf(c)->flag6E = UnitOf(c)->player ? 0 : 1;
    std::uint32_t current = UnitOf(c)->altPassCosts;
    UnitOf(c)->altPassCosts = saved;
    bool found = MapFindPath(context->AIMap(), nullptr, UnitOf(c), &MotionPathOf(c)->goal, MotionPathOf(c), range, 0.0f);
    UnitOf(c)->altPassCosts = current;
    if (liftBlocker) {
        SectorMapRemove(world->Sectors(), nullptr, UnitOf(c));
        world->AIMap()->StampWaterPatch(position, 0);
        SectorMapAdd(world->Sectors(), nullptr, UnitOf(c));
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
// Then the tick's end (00548520).
void Exhaustion(CUnitServer* c) {
    CUnit* unit = UnitOf(c);
    if (unit->obstacle == 0) return;
    if (c->moveMode.mode != 2 || c->moveMode2.mode != 2) return;
    CPlayer* player = PlayerOf(unit);
    if (c->moveMode.wasMode3 && player && player->godMode_ == 0 && GameMode() != 3) {
        UnitVitals& vitals = static_cast<CMapObjectServer*>(unit->companion)->vitals;
        float* stamina = &vitals.mana;
        const float* rate = &vitals.maxMana;
        float value;
        std::memcpy(&value, stamina, 4);
        if (!(value < 0.0f) && !(value > 0.0f)) {   // compares equal to 0 (NaN too)
            c->moveMode2.wasMode3 = 0;
            int mode = (c->moveMode2.wasMode3 & 1) ? 3 : c->moveMode2.mode;
            if (mode != unit->moveMode2) {
                unit->moveMode2 = mode;
                static_cast<CMapObject*>(UnitOf(c))->MarkChanged(6);
            }
        } else {
            if (Drain(stamina, rate)) {
                if (UnitOf(c)->player)
                    PlayerSendObject0BOriginal(PlayerOf(UnitOf(c))->connection_, nullptr, UnitOf(c), 0x1D);
                std::memset(stamina, 0, 4);
                c->moveMode2.wasMode3 = 0;
                int mode = (c->moveMode2.wasMode3 & 1) ? 3 : c->moveMode2.mode;
                if (mode != UnitOf(c)->moveMode2) {
                    UnitOf(c)->moveMode2 = mode;
                    static_cast<CMapObject*>(UnitOf(c))->MarkChanged(6);
                }
            }
            static_cast<CMapObject*>(UnitOf(c))->MarkChanged(0);
        }
    }
    c->UpdateSneak();
}

} // namespace

// 00547890. `stale` is what the original finds in its uninitialised "blocked" local (see
// UpdateMotion below): it is only written by CanStandAtFromHere (inside the map) and the blocker
// cases, so a unit that reached no cell, or a cell outside the map, decides on whatever the stack
// held there.
void __fastcall UpdateMotionBody(CUnitServer* c, int stale) {
    CWorldServer* world = ServerOf(c);
    Vector3 position;
    std::memcpy(&position, &UnitOf(c)->position.x, 12);
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
    int blocked = stale;
    if (cellX < 0) {
        canStand = true;
    } else {
        CSectorMap* sectors = world->Sectors();
        SectorMapRemove(sectors, nullptr, UnitOf(c));
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
        SectorMapAdd(sectors, nullptr, UnitOf(c));
    }

    // An idle unit standing in a kind-3 occluder (while its motion order started (+0x4C) before
    // the world's +0x2804 and moveMode2's flag4 is clear) replans.
    bool replan = false;
    if (c->state == kStateIdle && world->AIMap()->HasKind3OccluderAt(&position) &&
        OrderOf(MotionPathOf(c))->startTime < world->occluderReplanTime_ && c->moveMode2.flag4 == 0) {
        blocked = kBlockedReplan;
        replan = true;
    }
    if (!replan && canStand && SectorMapMove(world->Sectors(), nullptr, UnitOf(c), &position)) {
        SetObstacle(c, c->state == kStateIdle ? 1 : 0);
        TransformSetHeading(reinterpret_cast<CTransformObject*>(UnitOf(c)), nullptr, &direction.x);
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

// The entry: reads the dword the original's local [ESP+0x14] occupies (entry ESP - 0x98) before
// anything overwrites it, and continues in the body with the same return address.
__declspec(naked) void __fastcall UpdateMotion(CUnitServer* /*c*/) {
    __asm {
        mov edx, dword ptr [esp - 98h]
        jmp UpdateMotionBody
    }
}
EI_REPLACE(0x00547890, UpdateMotion);
