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
#include "unit.h"
#include "world.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_units.h"

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
__declspec(naked) void __fastcall RestartQueueA(CUnitServer*) EI_ORIGINAL(0x00546B40)
__declspec(naked) bool __fastcall CanPlayFor(CUnitServer*, void*, int /*ticks*/) EI_ORIGINAL(0x00549D00)
__declspec(naked) void __fastcall PlaySteps(CUnitServer*, void*, AnimationSteps*) EI_ORIGINAL(0x00546C40)
// 00550A80 CUnitServer::BuildDeathAnimation (combat_steps.cpp).
__declspec(naked) void __fastcall DeathAnimation(CUnitServer*, void*, AnimationSteps*, std::uint32_t, int, int) EI_ORIGINAL(0x00550A80)
// Animation lists: 00556850 constructor, 00557E70 destructor, 005483C0 assign (the queue +0x24 and
// the copy +0x148), 00556950 append, 00544E40 total duration (-1 if a step is open-ended), 00558920
// GetAt into `out`, 00544D50 add an emote, 00544CE0 add a step, 00558950 SetAtGrow.
__declspec(naked) AnimationSteps* __fastcall StepsConstruct(AnimationSteps*) EI_ORIGINAL(0x00556850)
inline void __fastcall StepsDestroy(AnimationSteps* s) { AnimationStepsDestroyOriginal(s); }
__declspec(naked) int __fastcall StepsDuration(const AnimationSteps*) EI_ORIGINAL(0x00544E40)
__declspec(naked) void __fastcall StepsGetAt(const AnimationSteps*, void*, AnimationStep* /*out*/, int) EI_ORIGINAL(0x00558920)
void __fastcall StepsEmote(AnimationSteps* a0, void*, std::uint32_t a2) { StepsEmoteOriginal((void*)a0, nullptr, (int)a2); }
void __fastcall StepsAddStep(AnimationSteps* a0, void*, std::uint32_t a2, int a3) { StepsAddStepOriginal((void*)a0, nullptr, (int)a2, a3); }
// CUnit: 00530240 its animation entry for a key (combat_steps.cpp), 0050E9F0 MarkChanged
// (unit.cpp), 0052E9D0 the path speeds, 00402220 face a direction.
__declspec(naked) void __fastcall FaceDirection(CUnit*, void*, const float*) EI_ORIGINAL(0x00402220)
// The sector map (world + 0x294): 005C1010 Remove, 005C0DF0 Add, 005C11D0 Move (patched by Rebirth).
// The AI map (world + 0x170, ai_map.cpp / ai_movement.cpp / ai_path.cpp): 005B81B0
// CanStandAtFromHere, 005B7BB0 BlockingUnitAt, 005BBDB0 HasKind3OccluderAt, 005BB350 FindPath,
// 005BB900 ContinuePath, 005B7EF0 lifts (1) / restores (0) the units around a point (not
// rewritten).
__declspec(naked) bool __fastcall MapCanStand(void*, void*, CUnit*, int, int, int*) EI_ORIGINAL(0x005B81B0)
__declspec(naked) bool __fastcall MapContinuePath(void*, void*, CUnit*, void* /*path*/) EI_ORIGINAL(0x005BB900)
// The motion order (+0x3C, an AIPath): 005B23A0 Clear, 005C7B00 Start (ai_motion.cpp).
// 0040F560 Quaternion::AxisY, 00402200 NegateVector (core_maths.cpp).
// 005D5E90 the AI brain's leader (unit_ai.cpp); 0066D6E0 a party message.
__declspec(naked) CUnit* __fastcall BrainLeader(void*) EI_ORIGINAL(0x005D5E90)
__declspec(naked) void __fastcall PartyMessage(void*, void*, CUnit*, int) EI_ORIGINAL(0x0066D6E0)

// --- x87 sequences ------------------------------------------------------------------------------

// World units to half-cells: (x, y) *= 1 / (cellSize * 0.5), the factor unrounded.
__declspec(naked) void __fastcall ToHalfCells(float* /*xy*/) {
    __asm {
        fld dword ptr ds:[0073EEE4h]
        fmul dword ptr ds:[0073B86Ch]
        fdivr dword ptr ds:[0073B84Ch]
        fld st(0)
        fmul dword ptr [ecx]
        fstp dword ptr [ecx]
        fmul dword ptr [ecx + 4]
        fstp dword ptr [ecx + 4]
        ret
    }
}

// And back: (x, y) *= cellSize * 0.5.
__declspec(naked) void __fastcall FromHalfCells(float* /*xy*/) {
    __asm {
        fld dword ptr ds:[0073EEE4h]
        fmul dword ptr ds:[0073B86Ch]
        fld st(0)
        fmul dword ptr [ecx]
        fstp dword ptr [ecx]
        fmul dword ptr [ecx + 4]
        fstp dword ptr [ecx + 4]
        ret
    }
}

// Whether |a - b|^2, summed as (dz^2 + dy^2) + dx^2 unrounded, is above *limit (false for NaN).
__declspec(naked) bool __fastcall FartherThan(const float* /*a*/, const float* /*b*/, const float* /*limit*/) {
    __asm {
        fld dword ptr [ecx]
        fsub dword ptr [edx]
        fld dword ptr [ecx + 4]
        fsub dword ptr [edx + 4]
        fld dword ptr [ecx + 8]
        fsub dword ptr [edx + 8]
        fld st(0)
        fmul st, st(1)
        fld st(2)
        fmul st, st(3)
        faddp st(1), st
        fld st(3)
        fmul st, st(4)
        faddp st(1), st
        mov eax, dword ptr [esp + 4]
        fcomp dword ptr [eax]
        fstp st(0)
        fnstsw ax
        fstp st(0)
        test ah, 41h
        fstp st(0)
        sete al
        ret 4
    }
}

// Whether `object`'s speed (virtual slot 59, left in ST0) compares below *than (or unordered).
__declspec(naked) bool __fastcall SpeedBelow(CEngineObject* /*object*/, const float* /*than*/) {
    __asm {
        push esi
        mov esi, edx
        mov eax, dword ptr [ecx]
        call dword ptr [eax + 0ECh]
        fcomp dword ptr [esi]
        fnstsw ax
        test ah, 1
        setne al
        pop esi
        ret
    }
}

// The speed rounded to float.
__declspec(naked) void __fastcall SpeedOf(CEngineObject* /*object*/, float* /*out*/) {
    __asm {
        push esi
        mov esi, edx
        mov eax, dword ptr [ecx]
        call dword ptr [eax + 0ECh]
        fstp dword ptr [esi]
        pop esi
        ret
    }
}

// *value -= *rate * (double)[0073F088]; true when the unrounded result compares below 0 (or is NaN).
__declspec(naked) bool __fastcall Drain(float* /*value*/, const float* /*rate*/) {
    __asm {
        fld dword ptr [ecx]
        fld dword ptr [edx]
        fmul qword ptr ds:[0073F088h]
        fsubp st(1), st
        fst dword ptr [ecx]
        fcomp dword ptr ds:[0073B858h]
        fnstsw ax
        test ah, 1
        setne al
        ret
    }
}

// FILD of the world's time, FSTP to float (the time Sample is called with).
__declspec(naked) float __fastcall TimeAsFloat(const int* /*time*/) {
    __asm {
        push ecx
        fild dword ptr [ecx]
        fstp dword ptr [esp]
        fld dword ptr [esp]
        pop ecx
        ret
    }
}

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

constexpr std::uintptr_t kQueueWaitLimit = 0x0073F090;   // squared distance a blocking ally may be away
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
    if (static_cast<int>(unit->unknown_278) == obstacle) return;
    SectorMapRemove(world->Sectors(), nullptr, unit);
    UnitOf(c)->unknown_278 = static_cast<std::uint32_t>(obstacle);
    SectorMapAdd(world->Sectors(), nullptr, UnitOf(c));
    UnitMarkChanged(UnitOf(c), nullptr, 7);
}

// The motion order's next animation once the current motion is over.
void QueueNextAnimation(CUnitServer* c) {
    AnimationSteps* next = CtlNextAnimation(c);
    StepsAppend(Queue(c), nullptr, next);
    QueueStart(c) = next->extra;
    StepsCopyB(Queue(c), nullptr, next);
    if (QueueStart(c) == -1) QueueStart(c) = WorldTime(ObjectWorld(c));
    RestartQueueA(c);
    CtlShowCurrentStep(c);
    c->movingAnimation = 1;
}

// A unit hit while turning on the spot: the death (or hit) animation of the last blow is played when
// it fits. Returns from the tick.
void HitWhileTurning(CUnitServer* c) {
    AnimationSteps steps;
    StepsConstruct(&steps);
    if (c->moveMode.mode != 0 || c->state != 0) {
        DeathAnimation(c, nullptr, &steps, c->hitDirection, 0, 0);
        if (CanPlayFor(c, nullptr, StepsDuration(&steps))) {
            c->keepHitDirection = 1;
            StepsCopyB(&c->savedQueue, nullptr, Queue(c));
            PlaySteps(c, nullptr, &steps);
            c->unknown_021 = 1;
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
        const auto* entry = reinterpret_cast<const FigureAnimation*>(UnitAnimationEntry(UnitOf(c), nullptr, key));
        if (entry && (entry->key & 0x3C0000) == 0x200000) {
            std::uint32_t ends = entry->frameCount + static_cast<std::uint32_t>(QueueStart(c));
            if (ends > static_cast<std::uint32_t>(WorldTime(ObjectWorld(c)))) return;
        }
    }
    AnimationSteps current;
    StepsConstruct(&current);
    AnimationSteps fall;
    StepsConstruct(&fall);
    CtlBuildMoveSteps(c, nullptr, &current, 0);
    DeathAnimation(c, nullptr, &fall, c->hitDirection, 0, 1);
    for (int i = 0; i < current.count; ++i) {
        AnimationStep step;
        StepsGetAt(&current, nullptr, &step, i);
        if (step.animation == 0xFF) {
            std::uint32_t emote;
            std::memcpy(&emote, reinterpret_cast<const char*>(&step) + 1, 4);
            StepsEmote(&fall, nullptr, emote);
        } else if (step.unknown_01 == 0xFF) {
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
    RestartQueueA(c);
    CtlShowCurrentStep(c);
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
        BrainLeader(ServerControllerOf(blocker)->brain) == self && CurrentFrame() < c->waitFrame + 2) {
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
        sameAttack = FartherThan(&target->position.x, &self->position.x, reinterpret_cast<const float*>(kQueueWaitLimit));
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
        if (AIPathStartOriginal(MotionPathOf(c), nullptr, &UnitOf(c)->position, facing, time)) UnitPathSpeeds(UnitOf(c));
        CtlSetStanding(c, nullptr, 0);
        CtlSetState(c, nullptr, 6);
        c->stoodBlocked = wasFlag;
        return;
    }
    if (!MapContinuePath(world->AIMap(), nullptr, unit, MotionPathOf(c))) {
        PathClear(MotionPathOf(c));
        int time = WorldTime(ObjectWorld(c));
        Vector3 axis, back;
        QuaternionAxisY(&UnitOf(c)->orientation, nullptr, &axis);
        Vector3* facing = Negate(&back, &axis);
        if (AIPathStartOriginal(MotionPathOf(c), nullptr, &UnitOf(c)->position, facing, time)) UnitPathSpeeds(UnitOf(c));
        CtlSetStanding(c, nullptr, 0);
        CtlSetState(c, nullptr, 6);
        c->stoodBlocked = 0;
        return;
    }
    UnitPathSpeeds(UnitOf(c));
    int state = c->state;
    if (state >= 0 && (state <= 1 || state == 2)) return;
    PlaySteps(c, nullptr, CtlIdleAnimation(c));
}

// Replans the path to the order's goal (+0x9C; its z, +0xA4, holds the range PathTo asked for), with
// the units around the blocked step lifted when `liftBlocker`.
void Replan(CUnitServer* c, CWorldServer* world, bool liftBlocker, const Vector3* position) {
    if (liftBlocker) {
        SectorMapRemove(world->Sectors(), nullptr, UnitOf(c));
        StampWaterPatchOriginal(world->AIMap(), nullptr, position, 1);
        SectorMapAdd(world->Sectors(), nullptr, UnitOf(c));
    }
    // The path's remaining cells (AIPath::cells, +0x78) emptied, as RemoveAll inlined.
    MotionPathOf(c)->cells.RemoveAll();

    float range = MotionPathOf(c)->goal.z;
    std::uint32_t saved = UnitOf(c)->unknown_43C;
    CWorldServer* context = ServerOf(c);
    MotionPathOf(c)->flag6E = UnitOf(c)->player ? 0 : 1;
    std::uint32_t current = UnitOf(c)->unknown_43C;
    UnitOf(c)->unknown_43C = saved;
    bool found = MapFindPath(context->AIMap(), nullptr, UnitOf(c), &MotionPathOf(c)->goal, MotionPathOf(c), range, 0.0f);
    UnitOf(c)->unknown_43C = current;
    if (liftBlocker) {
        SectorMapRemove(world->Sectors(), nullptr, UnitOf(c));
        StampWaterPatchOriginal(world->AIMap(), nullptr, position, 0);
        SectorMapAdd(world->Sectors(), nullptr, UnitOf(c));
    }
    if (found) {
        UnitPathSpeeds(UnitOf(c));
        int state = c->state;
        if (state >= 0 && (state <= 1 || state == 2)) return;
        PlaySteps(c, nullptr, CtlIdleAnimation(c));
        return;
    }
    std::uint8_t wasFlag = FailedByte(c) == 1;
    CtlStandStill(c);
    CtlSetState(c, nullptr, 6);
    c->stoodBlocked = wasFlag;
}

// Running units of a player tire: the server object's mana (vitals +0x14) is their breath and drains
// by the maximum (+0x18) times a factor per tick; out of breath they stop running (message 0x1D).
// Then the tick's end (00548520).
void Exhaustion(CUnitServer* c) {
    CUnit* unit = UnitOf(c);
    if (unit->unknown_278 == 0) return;
    if (c->moveMode.mode != 2 || c->moveMode2.mode != 2) return;
    CPlayer* player = PlayerOf(unit);
    if (c->moveMode.wasMode3 && player && player->unknown_108 == 0 && GameMode() != 3) {
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
                UnitMarkChanged(UnitOf(c), nullptr, 6);
            }
        } else {
            if (Drain(stamina, rate)) {
                if (UnitOf(c)->player)
                    PartyMessage(PlayerOf(UnitOf(c))->connection_, nullptr, UnitOf(c), 0x1D);
                std::memset(stamina, 0, 4);
                c->moveMode2.wasMode3 = 0;
                int mode = (c->moveMode2.wasMode3 & 1) ? 3 : c->moveMode2.mode;
                if (mode != UnitOf(c)->moveMode2) {
                    UnitOf(c)->moveMode2 = mode;
                    UnitMarkChanged(UnitOf(c), nullptr, 6);
                }
            }
            UnitMarkChanged(UnitOf(c), nullptr, 0);
        }
    }
    CtlUpdateSneak(c);
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
    bool going = MotionSample(UnitOf(c)->Motion(), nullptr, world, MotionPathOf(c), TimeAsFloat(&time), &turning,
                              &position, &direction, &speed, &cellX, &cellY);
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
        canStand = MapCanStand(world->AIMap(), nullptr, UnitOf(c), cellX, cellY, &blocked);
        if (!canStand) {
            CUnit* blocker = BlockingUnitAtOriginal(world->AIMap(), nullptr, UnitOf(c), &position);
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
    if (c->state == kStateIdle && HasKind3OccluderAtOriginal(world->AIMap(), nullptr, &position) &&
        OrderOf(MotionPathOf(c))->startTime < world->occluderReplanTime_ && c->moveMode2.flag4 == 0) {
        blocked = kBlockedReplan;
        replan = true;
    }
    if (!replan && canStand && SectorMapMove(world->Sectors(), nullptr, UnitOf(c), &position)) {
        SetObstacle(c, c->state == kStateIdle ? 1 : 0);
        FaceDirection(UnitOf(c), nullptr, &direction.x);
        if (motionOver) MotionOver(c, world);
    } else if (blocked == kBlockedReplan) {
        Replan(c, world, liftBlocker, &position);
    } else if (blocked == kBlockedWait) {
        if (c->stats.unknown_060 == 2) c->unknown_021 = 1;   // +0x240: the stats' +0x60
        else c->unknown_020 = 1;
        if (UnitOf(c)->unknown_278 != 0) SetObstacle(c, 0);
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
