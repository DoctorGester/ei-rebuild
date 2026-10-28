// Combat, seventh part: the controller's movement modes, idle and movement animations, paths to a point, the
// start of a planned action and the controller's reset. See docs/notes/combat.md ("Update").
#include "ai_alarm.h"
#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"
#include "core_maths.h"
#include "engine_object.h"
#include "map_object.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "item.h"
#include "map_object_figures.h"
#include "player_messages.h"
#include "random.h"
#include "script.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"
#include "logic_settings.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>

namespace {

constexpr int kStateIdle = 0;
constexpr int kStateTurning = 2;
constexpr int kStateAnimation = 3;   // an animation order plays
constexpr int kStateEmote = 4;       // an emote plays
constexpr int kStateActing = 5;
constexpr int kStateChasing = 6;
constexpr int kOrderMove = 1;
constexpr int kOrderAttack = 3;
constexpr int kOrderNone = 9;
constexpr int kModeRun = 2;
constexpr std::uint32_t kKeyIdle = 0x80580000;
constexpr std::uint32_t kKeyMoveOnce = 0x40000000;
constexpr std::uint32_t kKeyMoveLoop = 0x80000000;
constexpr std::uint8_t kEmoteStand = 0xFE;   // the idle "emote" of a walking hero
constexpr float kNoRange = 1e38f;
static_assert(std::bit_cast<std::uint32_t>(kNoRange) == 0x7E967699u);
constexpr std::uint32_t kFarBits = 0x49742400u;                 // 1e6
constexpr int kMessageModeBlocked = 0x0D;
constexpr int kMessageCannotDo = 0x0E;
constexpr int kMessagePartial = 0x01;

// --- Helpers --------------------------------------------------------------------------------------

constexpr float kHalfCell = 0.5f;

AnimationSteps& Queue(CUnitServer* self) { return self->queue; }
int& QueueTime(CUnitServer* self) { return self->queue.extra; }
OrderPlan& Plan(CUnitServer* self) { return self->currentPlan; }
AIPath* Motion(CUnitServer* self) { return &self->motion; }
AnimationSteps& MoveSteps(CUnitServer* self) { return self->moveAnimation; }
AnimationSteps& IdleSteps(CUnitServer* self) { return self->idleAnimation; }
std::uint32_t& MoveKey(CUnitServer* self) { return self->moveAnimationKey; }
std::uint32_t& IdleKey(CUnitServer* self) { return self->idleKey; }
int& IdleTime(CUnitServer* self) { return self->idleAnimation.extra; }

// The world time (CWorld::tick_) of an object's world (ObjectWorld) or manager context.
int TickOf(const CWorld* world) { return world->tick_; }
CWorldServer* ContextOf(CUnitServer* self) { return self->ServerWorld(); }
UnitVitals& ServerVitals(CUnit* unit) { return unit->ServerCompanion()->vitals; }

void StartQueueIfUnset(CUnitServer* self) {
    if (QueueTime(self) == -1) QueueTime(self) = TickOf(ObjectWorld(self));
}

void ShowHead(CUnitServer* self) {
    AnimationSteps& queue = Queue(self);
    if (queue.empty()) return;
    const AnimationStep& head = queue[0];
    if (head.animation != 0xFF || head.variant != 0xFF)
        self->unit->SetAnimationEntries(head.animation, head.variant, QueueTime(self), head.flags);
}

void DropEffectObject(CUnitServer* self) {
    if (!self->effectObject) return;
    g_serverWorld->RemoveTimedObject(self->effectObject->id_);
    CEngineObject* effect = self->effectObject;
    self->effectObject = nullptr;
    if (effect) effect->Release();
}

// The unit stops where it is, facing its direction (the motion restarted with the reversed Y axis of its
// rotation), then stands (0) and takes state 6.
void FaceAndStop(CUnitServer* self) {
    Motion(self)->Clear();
    int now = TickOf(ObjectWorld(self));
    Vector3 axis;
    self->unit->localRotation_.AxisY(&axis);
    Vector3 back;
    const Vector3* direction = NegateVector(&back, axis);
    if (Motion(self)->Start(&self->unit->localPosition_, direction, now)) self->unit->RefreshMotion();
    self->SetStanding(0);
    self->SetState(kStateChasing);
}

// The unit's movement mode from a record: 3 while it was mode 3 (`wasMode3`), else its mode.
int UnitModeOf(const MoveModeState& state) { return (state.wasMode3 & 1) ? 3 : state.mode; }

// A pause step for an emote (`value` as its variant byte, open-ended), reusing a trailing one.
void AddEmote(AnimationSteps* steps, std::uint8_t value) {
    if (steps->empty() || steps->back().variant != 0xFF) steps->emplace_back();
    AnimationStep& last = steps->back();
    last.variant = value;
    last.duration = -1;
}

// The weapon and movement bits of the unit's animation key for the current movement mode.
std::uint32_t WeaponAndModeBits(CUnitServer* self) {
    return self->WeaponTypeMask() | self->MoveModeMask(&self->moveMode);
}

// The idle animation into `steps`: a walking hero stands with the emote 0xFE, the others with their idle
// animation for the key, if any.
void FillIdle(CUnitServer* self, AnimationSteps* steps) {
    *steps = {};
    if (self->moveMode.mode == kModeRun && self->moveMode.onGuard == 0) {
        AddEmote(steps, kEmoteStand);
        return;
    }
    const FigureAnimation* entry = self->unit->FindAnimation(WeaponAndModeBits(self) | kKeyIdle, 1);
    if (entry) AddEmote(steps, entry->index);
}

std::uint32_t IdleKeyNow(CUnitServer* self) { return WeaponAndModeBits(self); }

// A motion order on the stack, as AIPath::ToMotionOrder fills it. The header is zeroed (the original leaves it
// uninitialised).
struct LocalMotionOrder : PathOrder {
    LocalMotionOrder() { std::memset(&motionOrder, 0, sizeof(motionOrder)); }
    LocalMotionOrder(const LocalMotionOrder&) = delete;
    LocalMotionOrder& operator=(const LocalMotionOrder&) = delete;
};

// A player's party, walked like the original's hash_map iteration.
using PartyMap = CPlayer::UnitMap;

// The two variants differ only in the state they set.
void Play(CUnitServer* self, const AnimationSteps& steps, int newState) {
    self->state = newState;
    self->unit->UpdateSizeClass();
    DropEffectObject(self);
    Queue(self) = steps;
    StartQueueIfUnset(self);
    self->PopFinishedSteps();
    ShowHead(self);
    Motion(self)->Clear();
    int now = TickOf(ObjectWorld(self));
    Vector3 axis;
    self->unit->localRotation_.AxisY(&axis);
    Vector3 back = -axis;
    if (Motion(self)->Start(&self->unit->localPosition_, &back, now)) self->unit->RefreshMotion();
    CWorldServer* world = ContextOf(self);
    if (self->unit->obstacle != 0) {
        CSectorMap* sectors = &world->aiMap_.sectorMap;
        sectors->Remove(self->unit);
        self->unit->obstacle = 0;
        sectors->Add(self->unit);
        self->unit->MarkChanged(7);
    }
}

// A movement record from the unit's mode: mode 3 is mode 2 with `wasMode3`.
void RecordFromMode(MoveModeState* record, int mode) {
    record->wasMode3 = 0;
    record->mode = mode;
    if (mode == 3) {
        record->mode = 2;
        record->wasMode3 = 1;
    }
}

} // namespace

// --- Small checks -------------------------------------------------------------------------------

bool CUnitServer::CanReactFor(int ticks) {
    if (action.kind == kOrderAttack && state == kStateTurning &&
        TickOf(ObjectWorld(this)) + ticks > nextAttackTime)
        return false;
    return true;
}

// --- Playing an animation ----------------------------------------------------------------------

void CUnitServer::PlayAnimationOrder(const AnimationSteps& steps) { Play(this, steps, kStateAnimation); }

void CUnitServer::PlayEmoteOrder(const AnimationSteps& steps) { Play(this, steps, kStateEmote); }

// --- Idle and movement animations --------------------------------------------------------------

void CUnitServer::BuildIdle(AnimationSteps* steps) { FillIdle(this, steps); }

AnimationSteps* CUnitServer::IdleAnimation() {
    std::uint32_t key = IdleKeyNow(this);
    if (IdleKey(this) == key) {
        IdleTime(this) = -1;
    } else {
        IdleKey(this) = key;
        FillIdle(this, &IdleSteps(this));
    }
    return &IdleSteps(this);
}

void CUnitServer::PlayIdle() {
    if (state >= 0 && (state <= 1 || state == kStateTurning)) return;
    std::uint32_t key = IdleKeyNow(this);
    if (IdleKey(this) == key) {
        IdleTime(this) = -1;
    } else {
        IdleKey(this) = key;
        FillIdle(this, &IdleSteps(this));
    }
    // The idle list into the queue, started.
    Queue(this) = IdleSteps(this);
    StartQueueIfUnset(this);
    PopFinishedSteps();
    ShowHead(this);
}

void CUnitServer::BuildMoveSteps(AnimationSteps* steps, char once) {
    *steps = {};
    CUnit* self = unit;
    std::uint32_t bits;
    switch (static_cast<std::uint32_t>(moveMode.mode)) {
    case 0: bits = 0x30000; break;
    case 1: bits = 0x20000; break;
    case 4: bits = 0x8000; break;
    case 2:
        if (moveMode.onGuard || self->type_ != kUnitTypeHero) {
            bits = 0x10000;
            break;
        }
        [[fallthrough]];
    default:
        // A hero's own walk is looped with the walking bits (not when played once).
        bits = (!once && self->type_ == kUnitTypeHero) ? 0x10000 : 0;
        break;
    }
    std::uint32_t key = WeaponTypeMask() | bits | (self->moveMode == 3 ? 0x100000u : 0x140000u);
    if (once) {
        key |= kKeyMoveOnce;
    } else {
        key |= kKeyMoveLoop;
        if (self->type_ == kUnitTypeHero) {
            float stamina = stats.loadFactor;
            EI_NAN_AUDIT(stamina, g_logicSettings.damageLevel2Value, g_logicSettings.damageLevel1Value);
            if (stamina <= g_logicSettings.damageLevel2Value) key |= 0x800000;
            else if (stamina <= g_logicSettings.damageLevel1Value) key |= 0x400000;
        }
    }
    if (const FigureAnimation* entry = self->FindAnimation(key, 1)) {
        std::uint8_t index = entry->index;
        if (!once) {
            AddEmote(steps, index);
        } else {
            int duration = AnimationDuration(index);
            AnimationStep& last = steps->emplace_back();
            last.animation = index;
            last.duration = duration;
        }
    }
    for (AnimationStep& step : *steps) step.kind = 2;
}

AnimationSteps* CUnitServer::NextAnimation() {
    CUnit* self = unit;
    std::uint32_t key = WeaponAndModeBits(this);
    if (self->moveMode == 3) {
        key |= 0x100000;
    } else {
        key |= 0x140000;
        if (self->type_ == kUnitTypeHero) {
            float stamina = stats.loadFactor;
            EI_NAN_AUDIT(stamina, g_logicSettings.damageLevel2Value, g_logicSettings.damageLevel1Value);
            if (stamina <= g_logicSettings.damageLevel2Value) key |= 0x800000;
            else if (stamina <= g_logicSettings.damageLevel1Value) key |= 0x400000;
        }
    }
    if (MoveKey(this) == key) return &MoveSteps(this);
    MoveKey(this) = key;
    if (self->type_ != kUnitTypeHero) {
        BuildMoveSteps(&MoveSteps(this), 0);
        return &MoveSteps(this);
    }
    // A hero: the start of the movement once, then the loop appended step by step.
    AnimationSteps loop;
    BuildMoveSteps(&loop, 0);
    AnimationSteps& steps = MoveSteps(this);
    BuildMoveSteps(&steps, 1);
    for (const AnimationStep& step : loop) {
        if (step.animation == 0xFF) {
            // A pause: an emote of its variant byte (the original passes a misaligned dword; AddEmote keeps its low byte
            // only).
            steps.AddEmote(step.variant);
        } else if (step.variant == 0xFF) {
            AnimationStep& last = steps.emplace_back();   // padding 0, not a stack byte (the harness zeroes the original's)
            last.animation = step.animation;
            last.duration = step.duration;
        } else {
            steps.push_back(step);
        }
    }
    return &steps;
}

// --- Starting the planned action ---------------------------------------------------------------

void CUnitServer::StartAction(char follow, char stepFlags) {
    OrderPlan& plan = Plan(this);
    for (AnimationStep& step : plan.animation) step.flags = static_cast<std::uint8_t>(stepFlags);
    if (state < 0 || (state > 1 && state != kStateTurning)) {
        std::uint32_t key = WeaponTypeMask() | MoveModeMask(&moveMode);
        if (IdleKey(this) == key) {
            IdleTime(this) = -1;
        } else {
            IdleKey(this) = key;
            BuildIdle(&IdleSteps(this));
        }
        PlaySteps(&IdleSteps(this));
    }
    state = kStateActing;
    actingCheckTime = TickOf(WorldContext()) + 10;
    if (plan.targetId == -1) {
        int now = TickOf(ObjectWorld(this));
        Motion(this)->StandAndTurnUntil(now, unit, &plan.direction, plan.endTime, plan.startTime);
    } else {
        int until = follow ? plan.endTime : 0;
        int now = TickOf(ObjectWorld(this));
        Motion(this)->StandAndTrack(now, unit, plan.targetId, until, plan.startTime);
    }
    unit->RefreshMotion();
    CWorldServer* world = ContextOf(this);
    if (unit->obstacle == 0) return;
    CSectorMap* sectors = &world->aiMap_.sectorMap;
    sectors->Remove(unit);
    unit->obstacle = 0;
    sectors->Add(unit);
    unit->MarkChanged(7);
}

// --- Movement modes -----------------------------------------------------------------------------

void CUnitServer::UpdateSneak() {
    CWorldServer* world = ContextOf(this);
    int running = stats.MovementState(1);
    if (running != 0 && moveMode2.wasMode3 != 0) {
        // Out of breath: back from the second mode's mode 3, with a remark.
        std::uint32_t code = running == 1 ? 0x15 : 0x14;
        if ((code <= 0x29 || code == 0x30) && code != 0xFFFFFFFFu && unit->player)
            PlayerMessage(unit->player->Server(), unit, static_cast<int>(code));
        if (action.modifier != 0) action.modifier = 0;
        moveMode2.wasMode3 = 0;
        int mode = moveMode2.mode;
        if (mode != unit->moveMode2) {
            unit->moveMode2 = mode;
            unit->MarkChanged(6);
        }
    }
    if (moveMode.wasMode3 != 0) {
        if (moveMode2.wasMode3 != 0 && world->aiMap_.StandsOnCheapCell(unit) && running == 0) {
            sneakWait = 0;
            return;
        }
        int& waited = sneakWait;
        ++waited;
        if (waited <= 5 && moveMode2.wasMode3 != 0) return;
        moveMode.wasMode3 = 0;
        int mode = moveMode.mode;
        if (mode != unit->moveMode) {
            unit->moveMode = mode;
            unit->MarkChanged(6);
        }
        unit->UpdateSizeClass();
        motionChanged = 1;
        return;
    }
    int allowed = unit->player ? unit->player->godMode_ : 1;
    if (moveMode.mode != kModeRun || moveMode2.wasMode3 == 0) return;
    EI_NAN_AUDIT(ServerVitals(unit).mana);
    if (allowed == 0 && ServerVitals(unit).mana <= 0.0f) return;   // the breath
    if (!world->aiMap_.StandsOnCheapCell(unit)) return;
    if (running != 0) return;
    sneakWait = 0;
    moveMode.wasMode3 = 1;
    int mode = UnitModeOf(moveMode);
    if (mode != unit->moveMode) {
        unit->moveMode = mode;
        unit->MarkChanged(6);
    }
    unit->UpdateSizeClass();
    motionChanged = 1;
}

bool CUnitServer::ChangeMoveMode() {
    if (moveMode.mode == moveMode2.mode && moveMode.onGuard == moveMode2.onGuard) return false;
    bool modeChanged = moveMode.mode != moveMode2.mode;
    MoveModeState saved = moveMode;
    g_serverWorld->aiMap_.sectorMap.Remove(unit);
    bool passableBefore = g_serverWorld->aiMap_.IsUnitCellPassable(unit);
    bool sameMode = moveMode.mode == moveMode2.mode;
    moveMode = moveMode2;
    unit->moveMode = UnitModeOf(moveMode);
    unit->UpdateSizeClass();
    bool placed;
    if (!sameMode &&
        !(passableBefore ? g_serverWorld->aiMap_.IsUnitCellPassable(unit) : g_serverWorld->aiMap_.CanLeaveCell(unit)))
        placed = false;
    else
        placed = g_serverWorld->aiMap_.sectorMap.Add(unit);
    unit->MarkChanged(6);
    if (!placed) {
        // The new mode does not fit here: back to the old one, and the party is told.
        moveMode = saved;
        unit->moveMode = UnitModeOf(moveMode);
        moveMode2.mode = moveMode.mode;
        if (moveMode.mode != 2) moveMode2.wasMode3 = 0;
        unit->moveMode2 = UnitModeOf(moveMode2);
        unit->UpdateSizeClass();
        g_serverWorld->aiMap_.sectorMap.Add(unit);
        CUnit* self = unit;
        if (self->player) PlayerMessage(self->player->Server(), self, kMessageModeBlocked);
        return false;
    }
    if (state == kStateIdle) {
        if (modeChanged) FaceAndStop(this);
        if (state == kStateIdle && !modeChanged) {
            unit->UpdateSizeClass();
            if (action2.kind == kOrderNone) action2.Assign(&action);
            return true;
        }
    }
    if (action.kind != kOrderAttack) {
        AnimationSteps transition;
        if (BuildTransition(&transition, &saved, &moveMode)) {
            FaceAndStop(this);
            PlayAnimationOrder(transition);   // the transition played as an animation order
        }
    }
    unit->UpdateSizeClass();
    if (action2.kind == kOrderNone) action2.Assign(&action);
    return true;
}

// --- Paths --------------------------------------------------------------------------------------

bool CUnitServer::PathTo(const Vector3* point, float range) {
    UpdateSneak();
    motionChanged = 0;
    stoodBlocked = 0;
    Motion(this)->Clear();
    std::uint32_t keep = unit->altPassCosts;
    CWorldServer* world = ContextOf(this);
    Motion(this)->mayExcludeRegions = unit->player ? 0 : 1;
    std::uint32_t before = unit->altPassCosts;
    unit->altPassCosts = keep;
    bool found = world->aiMap_.FindPath(unit, point, Motion(this), range, 0.0f);
    unit->altPassCosts = before;
    if (!found) {
        // Read as a byte: the original compares it with 1.
        std::uint8_t blocked = Motion(this)->failed;
        movingAnimation = 0;
        StandStill();
        SetState(kStateChasing);
        stoodBlocked = blocked == 1 ? 1 : 0;
        return false;
    }
    Motion(this)->goal.z = range;   // the range (Replan reads it back)
    sneakWait = 0;
    unit->RefreshMotion();
    if (state < 0 || (state > 1 && state != kStateTurning)) PlaySteps(IdleAnimation());
    state = kStateIdle;
    unit->UpdateSizeClass();
    DropEffectObject(this);
    if (movingAnimation != 0) {
        // Moving already: the movement animation goes on unless it is the same one-step loop.
        const AnimationSteps& moving = *NextAnimation();
        AnimationSteps& current = Queue(this);
        if (current.size() != 1 || moving.size() != 1 || current[0].variant != moving[0].variant) {
            current = moving;
            StartQueueIfUnset(this);
            PopFinishedSteps();
            ShowCurrentStep();
        }
    }
    return true;
}

void CUnitServer::LiftNearby(std::list<CUnit*>* list, const Vector3* point, char moving) {
    CWorldServer* world = ContextOf(this);
    CPlayer* player = unit->player;
    list->clear();
    if (!player || player->sessionKind_ != 1) return;
    // Away from the point: the units near it that are not in the player's group are lifted off the sector map
    // and stay in the list.
    float dx = point->x - unit->localPosition_.x;
    float dy = point->y - unit->localPosition_.y;
    float dz = point->z - unit->localPosition_.z;
    if (LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) > 16.0f) {
        CSectorMap* sectors = &world->aiMap_.sectorMap;
        sectors->FindUnitsInRadius(point, list, 4.0f);
        const ScriptGroup* group = &player->Server()->partyUnits_;
        for (auto node = list->begin(); node != list->end();) {
            std::list<CUnit*>::iterator  current = node;
            ++node;
            CUnit* other = *current;
            if (group->Contains(other) == 0) {
                sectors->Remove(other);
                continue;
            }
            list->erase(current);
        }
    }
    // The party members moving (when `moving`) or following this unit are lifted and listed too.
    const PartyMap* party = &player->units_;
    for (auto node = party->begin(); node != party->end(); ++node) {
        CUnit* member = node->second;
        if (!member) break;
        if (member == unit) continue;
        auto* controller = member->ServerController();
        bool lift = moving && (controller->action.kind == kOrderMove || controller->action2.kind == kOrderMove);
        if (!lift) lift = controller->brain.LeaderOrFollowed() == unit;
        if (!lift) continue;
        auto added = list->emplace(list->end());
        *added = member;
        g_serverWorld->aiMap_.sectorMap.Remove(member);
    }
}

void CUnitServer::ReportPath(AIPath* path, char partial) {
    if (brain.action == AIBrain::kActionFollow) return;   // following reports nothing
    CUnit* self = unit;
    const float* goal = &path->goal.x;
    // The path as a motion order for the message. The header is left uninitialised when the path is not partial
    // (the message does not read it for kind 3).
    LocalMotionOrder end;
    int message;
    if (!partial) {
        if (self->player) self->player->Server()->SendPathMessage(self, &end, goal, 3, 1, kFarBits);
        if (brain.LeaderOrFollowed()) return;
        self = unit;
        if (!self->player) return;
        message = kMessageCannotDo;
    } else {
        path->ToMotionOrder(&end);
        float dy = self->localPosition_.y - goal[1];
        float dx = self->localPosition_.x - goal[0];
        float reached = LengthSquared2DUnrounded(dx, dy);
        float half = kHalfCell;
        float ey = half * path->targetY - goal[1];
        float ex = half * path->targetX - goal[0];
        float left = LengthSquared2DUnrounded(ex, ey);
        bool stillFar = reached * 0.010000001f > left;   // unordered: not far
        if (self->player)
            self->player->Server()->SendPathMessage(self, &end, goal, 0, stillFar ? 0 : 1, kFarBits);
        if (brain.LeaderOrFollowed()) return;
        self = unit;
        if (!self->player) return;
        message = stillFar ? kMessagePartial : kMessageCannotDo;
    }
    PlayerMessage(self->player->Server(), self, message);
}

// --- Resync with the unit ---------------------------------------------------------------------

void CUnitServer::ResyncWithUnit() {
    action.kind = kOrderNone;
    if (action.target) action.target->Release();
    action.target = nullptr;
    action.pointIsUseSpot = 0;
    action.param = 0;
    action.modifier = 0;
    if (action.kind != 0 && action2.kind != 7 && action2.kind != 8) {   // the first is always true
        action2.kind = kOrderNone;
        action2.target.Reset();
        action2.pointIsUseSpot = 0;
        action2.param = 0;
        action2.modifier = 0;
        action2.kind = 0;
        action2.target.Assign(nullptr);
        action2.modifier = 0;
    }
    RecordFromMode(&moveMode, unit->moveMode);
    RecordFromMode(&moveMode2, unit->moveMode2);
    Motion(this)->Clear();
    int now = TickOf(ObjectWorld(this));
    Vector3 axis;
    unit->localRotation_.AxisY(&axis);
    Vector3 back;
    const Vector3* direction = NegateVector(&back, axis);
    if (Motion(this)->Start(&unit->localPosition_, direction, now)) unit->RefreshMotion();
    SetStanding(0);
    SetState(kStateChasing);
    unit->UpdateSizeClass();
    unit->pendingWeapon = 0;
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));
    if (state == kStateTurning) return;
    std::uint32_t key = WeaponTypeMask() | MoveModeMask(&moveMode);
    if (IdleKey(this) == key) {
        IdleTime(this) = -1;
    } else {
        IdleKey(this) = key;
        BuildIdle(&IdleSteps(this));
    }
    if (!Queue(this).SameAs(&IdleSteps(this))) {
        // The idle animation starts at a random phase (up to 50 ticks back) on an empty queue.
        if (Queue(this).empty() && QueueTime(this) == -1)
            IdleTime(this) = TickOf(ObjectWorld(this)) - static_cast<int>(g_random.Next() % 0x33);
        Queue(this) = IdleSteps(this);
        StartQueueIfUnset(this);
        PopFinishedSteps();
        ShowCurrentStep();
    }
    StandStill();
    SetState(kStateTurning);
}

// --- Reset ---------------------------------------------------------------------------------------

void CUnitServer::ResetController() {
    auto releaseRef = [](AIRefTo<CMapObject>& ref) {
        if (ref) ref->Release();
        ref = nullptr;
    };
    action.kind = kOrderNone;
    releaseRef(action.target);
    action.pointIsUseSpot = 0;
    action.param = 0;
    action.modifier = 0;
    action2.kind = kOrderNone;
    releaseRef(action2.target);
    action2.pointIsUseSpot = 0;
    action2.param = 0;
    action2.modifier = 0;
    frozen = 0;
    animationHeld = 0;
    Queue(this) = {};
    Motion(this)->Clear();
    OrderPlan& plan = Plan(this);
    plan.animation = {};
    plan.targetId = -1;
    plan.endTime = 0;
    plan.startTime = 0;
    plan.hitLocation = 1;
    plan.rangeSq = kNoRange;
    plan.isCast = 0;
    plan.notSkill = 0;
    plan.castKind = 8;
    std::memset(&pendingHit, 0, 0x3C);
    pendingHit.multiplier = 1.0f;
    savedQueue = {};
    state = kStateChasing;
    hitDirection = 0xFFFFFFFFu;
    keepHitDirection = 0;
    stoodBlocked = 0;
    motionChanged = 0;
    movingAnimation = 0;
    moveMode.mode = 2;
    moveMode.onGuard = 0;
    moveMode.wasMode3 = 0;
    moveMode2.mode = 2;
    moveMode2.onGuard = 0;
    moveMode2.wasMode3 = 0;
    CEngineObject* effect = effectObject;
    effectObject = nullptr;
    if (effect) effect->Release();
    actingCheckTime = -1;
    action.kind = 0;
    action2.kind = kOrderNone;
    nextAttackTime = 0;
    interceptTime = 0;
    orderStopped = 0;
    deathReported = 0;
    followsLeader = 0;
    CUnit* waited = followedLeader;
    followedLeader = nullptr;
    if (waited) waited->Release();
    searchingTarget = 0;
    brain.Init();
    voice.Reset();
    aiPresence = nullptr;
    moveFactors[0] = 1.0f;
    moveFactors[2] = 1.0f;
    moveFactors[3] = 1.0f;
    moveFactors[1] = 1.0f;
    stats.Reset();
    tameStage = 0;
    ownSide = -1;
    tamerSide = -1;
    speedClass = 0xF;
    claimedHostile = 0;
    sinkSpeed = 0.0f;
    MoveKey(this) = 0x666DEAD;
    MoveSteps(this) = {};
    IdleKey(this) = 0x666DEAD;
    IdleSteps(this) = {};
    boredTicks = 0;
    // The armour spell list emptied, each element's item released.
    for (auto node = armourSpells.begin(); node != armourSpells.end(); ++node)
        if (node->item) node->item->Release();
    armourSpells.clear();
    actionDone = nullptr;
}
