// Combat, seventh part: the controller's movement modes, idle and movement animations, paths to a point, the
// start of a planned action and the controller's reset. See docs/notes/combat.md ("Update").
#include "ai_alarm.h"
#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"
#include "core_maths.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "item.h"
#include "logic_settings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "player_messages.h"
#include "random.h"
#include "script.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>

namespace {

constexpr int kModeRun = 2;
constexpr std::uint32_t kKeyIdle = kAnimationSpecialIdleKey;
constexpr std::uint8_t kEmoteStand = 0xFE;   // the idle "emote" of a walking hero
constexpr float kNoRange = 1e38f;
static_assert(std::bit_cast<std::uint32_t>(kNoRange) == 0x7E967699u);
constexpr std::uint32_t kFarBits = 0x49742400u;                 // 1e6

// --- Helpers --------------------------------------------------------------------------------------

constexpr float kHalfCell = 0.5f;


UnitVitals& ServerVitals(CUnit* unit) { return unit->ServerCompanion()->vitals; }

void ShowHead(CUnitServer* self) {
    AnimationSteps& queue = self->queue;
    if (queue.empty()) return;
    const AnimationStep& head = queue[0];
    if (!head.IsBlank()) self->unit->SetAnimationEntries(head.animation, head.variant, self->queue.extra, head.flags);
}

// The unit stops where it is, facing its direction (the motion restarted with the reversed Y axis of its
// rotation), then stands (0) and takes state 6.
void FaceAndStop(CUnitServer* self) {
    self->motion.Clear();
    int now = self->World()->tick_;
    Vector3 axis;
    self->unit->localRotation_.AxisY(&axis);
    Vector3 back;
    const Vector3* direction = NegateVector(&back, axis);
    if (self->motion.Start(self->unit->localPosition_, *direction, now)) self->unit->RefreshMotion();
    self->ClearStanding();
    self->SetState(UnitState::Stopped);
}

// The unit's movement mode from a record: 3 while it was mode 3 (`wasMode3`), else its mode.
int UnitModeOf(const MoveModeState& state) { return (state.wasMode3 & 1) ? 3 : state.mode; }

// A pause step for an emote (`value` as its variant byte, open-ended), reusing a trailing one.
void AddEmote(AnimationSteps* steps, std::uint8_t value) {
    if (steps->empty() || steps->back().variant != AnimationStep::kNone) steps->emplace_back();
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
void Play(CUnitServer* self, const AnimationSteps& steps, UnitState newState) {
    self->state = newState;
    self->unit->UpdateSizeClass();
    self->DropEffectObject();
    self->queue = steps;
    self->StartQueueIfUnset();
    self->PopFinishedSteps();
    ShowHead(self);
    self->motion.Clear();
    int now = self->World()->tick_;
    Vector3 axis;
    self->unit->localRotation_.AxisY(&axis);
    Vector3 back = -axis;
    if (self->motion.Start(self->unit->localPosition_, back, now)) self->unit->RefreshMotion();
    CWorldServer* world = self->ServerWorld();
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
    if (action.kind == UnitOrderKind::Attack && state == UnitState::Turning &&
        World()->tick_ + ticks > nextAttackTime)
        return false;
    return true;
}

// --- Playing an animation ----------------------------------------------------------------------

void CUnitServer::PlayAnimationOrder(const AnimationSteps& steps) { Play(this, steps, UnitState::Animating); }

void CUnitServer::PlayEmoteOrder(const AnimationSteps& steps) { Play(this, steps, UnitState::Emote); }

// --- Idle and movement animations --------------------------------------------------------------

void CUnitServer::BuildIdle(AnimationSteps* steps) { FillIdle(this, steps); }

AnimationSteps* CUnitServer::IdleAnimation() {
    std::uint32_t key = WeaponAndModeBits(this);
    if (idleKey == key) {
        idleAnimation.extra = -1;
    } else {
        idleKey = key;
        FillIdle(this, &idleAnimation);
    }
    return &idleAnimation;
}

void CUnitServer::PlayIdle() {
    if (state == UnitState::Idle || state == UnitState::Moving || state == UnitState::Turning) return;
    std::uint32_t key = WeaponAndModeBits(this);
    if (idleKey == key) {
        idleAnimation.extra = -1;
    } else {
        idleKey = key;
        FillIdle(this, &idleAnimation);
    }
    // The idle list into the queue, started.
    queue = idleAnimation;
    StartQueueIfUnset();
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
        if (moveMode.onGuard || self->MapType() != MapObjectType::Hero) {
            bits = 0x10000;
            break;
        }
        [[fallthrough]];
    default:
        // A hero's own walk is looped with the walking bits (not when played once).
        bits = (!once && self->MapType() == MapObjectType::Hero) ? 0x10000 : 0;
        break;
    }
    std::uint32_t key =
        WeaponTypeMask() | bits | AnimationKeyOf(self->moveMode == 3 ? AnimationKind::Run : AnimationKind::Walk);
    if (once) {
        key |= AnimationKeyFlag::Once;
    } else {
        key |= AnimationKeyFlag::Looped;
        if (self->MapType() == MapObjectType::Hero) {
            float stamina = stats.loadFactor;
            EI_NAN_AUDIT(stamina, g_logicSettings.damageLevel2Value, g_logicSettings.damageLevel1Value);
            if (stamina <= g_logicSettings.damageLevel2Value) key |= AnimationKeyFlag::DamageLevel2;
            else if (stamina <= g_logicSettings.damageLevel1Value) key |= AnimationKeyFlag::DamageLevel1;
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
        key |= AnimationKeyOf(AnimationKind::Run);
    } else {
        key |= AnimationKeyOf(AnimationKind::Walk);
        if (self->MapType() == MapObjectType::Hero) {
            float stamina = stats.loadFactor;
            EI_NAN_AUDIT(stamina, g_logicSettings.damageLevel2Value, g_logicSettings.damageLevel1Value);
            if (stamina <= g_logicSettings.damageLevel2Value) key |= AnimationKeyFlag::DamageLevel2;
            else if (stamina <= g_logicSettings.damageLevel1Value) key |= AnimationKeyFlag::DamageLevel1;
        }
    }
    if (moveAnimationKey == key) return &moveAnimation;
    moveAnimationKey = key;
    if (self->MapType() != MapObjectType::Hero) {
        BuildMoveSteps(&moveAnimation, 0);
        return &moveAnimation;
    }
    // A hero: the start of the movement once, then the loop appended step by step.
    AnimationSteps loop;
    BuildMoveSteps(&loop, 0);
    AnimationSteps& steps = moveAnimation;
    BuildMoveSteps(&steps, 1);
    for (const AnimationStep& step : loop) {
        if (step.animation == AnimationStep::kNone) {
            // A pause: an emote of its variant byte (the original passes a misaligned dword; AddEmote keeps its low byte
            // only).
            steps.AddEmote(step.variant);
        } else if (step.variant == AnimationStep::kNone) {
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
    OrderPlan& plan = currentPlan;
    for (AnimationStep& step : plan.animation) step.flags = static_cast<std::uint8_t>(stepFlags);
    if (state != UnitState::Idle && state != UnitState::Moving && state != UnitState::Turning) {
        std::uint32_t key = WeaponTypeMask() | MoveModeMask(&moveMode);
        if (idleKey == key) {
            idleAnimation.extra = -1;
        } else {
            idleKey = key;
            BuildIdle(&idleAnimation);
        }
        PlaySteps(&idleAnimation);
    }
    state = UnitState::Acting;
    actingCheckTime = WorldContext()->tick_ + 10;
    if (plan.targetId == -1) {
        int now = World()->tick_;
        motion.StandAndTurnUntil(now, unit, plan.direction, plan.endTime, plan.startTime);
    } else {
        int until = follow ? plan.endTime : 0;
        int now = World()->tick_;
        motion.StandAndTrack(now, unit, plan.targetId, until, plan.startTime);
    }
    unit->RefreshMotion();
    CWorldServer* world = ServerWorld();
    if (unit->obstacle == 0) return;
    CSectorMap* sectors = &world->aiMap_.sectorMap;
    sectors->Remove(unit);
    unit->obstacle = 0;
    sectors->Add(unit);
    unit->MarkChanged(7);
}

// --- Movement modes -----------------------------------------------------------------------------

void CUnitServer::UpdateSneak() {
    CWorldServer* world = ServerWorld();
    int running = stats.MovementState(1);
    if (running != 0 && moveMode2.wasMode3 != 0) {
        // Out of breath: back from the second mode's mode 3, with a remark.
        AckKind kind = running == 1 ? AckKind::Injured : AckKind::Overloaded;
        if (IsShoutable(kind) && unit->player) PlayerMessage(unit->player->Server(), unit, kind);
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
    g_serverWorld.aiMap_.sectorMap.Remove(unit);
    bool passableBefore = g_serverWorld.aiMap_.IsUnitCellPassable(unit);
    bool sameMode = moveMode.mode == moveMode2.mode;
    moveMode = moveMode2;
    unit->moveMode = UnitModeOf(moveMode);
    unit->UpdateSizeClass();
    bool placed;
    if (!sameMode &&
        !(passableBefore ? g_serverWorld.aiMap_.IsUnitCellPassable(unit) : g_serverWorld.aiMap_.CanLeaveCell(unit)))
        placed = false;
    else
        placed = g_serverWorld.aiMap_.sectorMap.Add(unit);
    unit->MarkChanged(6);
    if (!placed) {
        // The new mode does not fit here: back to the old one, and the party is told.
        moveMode = saved;
        unit->moveMode = UnitModeOf(moveMode);
        moveMode2.mode = moveMode.mode;
        if (moveMode.mode != 2) moveMode2.wasMode3 = 0;
        unit->moveMode2 = UnitModeOf(moveMode2);
        unit->UpdateSizeClass();
        g_serverWorld.aiMap_.sectorMap.Add(unit);
        CUnit* self = unit;
        if (self->player) PlayerMessage(self->player->Server(), self, AckKind::CantChangePosition);
        return false;
    }
    if (state == UnitState::Idle) {
        if (modeChanged) FaceAndStop(this);
        if (state == UnitState::Idle && !modeChanged) {
            unit->UpdateSizeClass();
            if (action2.kind == UnitOrderKind::None) action2.Assign(&action);
            return true;
        }
    }
    if (action.kind != UnitOrderKind::Attack) {
        AnimationSteps transition;
        if (BuildTransition(&transition, &saved, &moveMode)) {
            FaceAndStop(this);
            PlayAnimationOrder(transition);   // the transition played as an animation order
        }
    }
    unit->UpdateSizeClass();
    if (action2.kind == UnitOrderKind::None) action2.Assign(&action);
    return true;
}

// --- Paths --------------------------------------------------------------------------------------

bool CUnitServer::PathTo(const Vector3& point, float range) {
    UpdateSneak();
    motionChanged = 0;
    stoodBlocked = 0;
    motion.Clear();
    std::uint32_t keep = unit->altPassCosts;
    CWorldServer* world = ServerWorld();
    motion.mayExcludeRegions = unit->player ? 0 : 1;
    std::uint32_t before = unit->altPassCosts;
    unit->altPassCosts = keep;
    bool found = world->aiMap_.FindPath(unit, point, &motion, range, 0.0f);
    unit->altPassCosts = before;
    if (!found) {
        // Read as a byte: the original compares it with 1.
        std::uint8_t blocked = motion.failed;
        movingAnimation = 0;
        StandStill();
        SetState(UnitState::Stopped);
        stoodBlocked = blocked == 1 ? 1 : 0;
        return false;
    }
    motion.goal.z = range;   // the range (Replan reads it back)
    sneakWait = 0;
    unit->RefreshMotion();
    if (state != UnitState::Idle && state != UnitState::Moving && state != UnitState::Turning) PlaySteps(IdleAnimation());
    state = UnitState::Idle;
    unit->UpdateSizeClass();
    DropEffectObject();
    if (movingAnimation != 0) {
        // Moving already: the movement animation goes on unless it is the same one-step loop.
        const AnimationSteps& moving = *NextAnimation();
        AnimationSteps& current = queue;
        if (current.size() != 1 || moving.size() != 1 || current[0].variant != moving[0].variant) {
            current = moving;
            StartQueueIfUnset();
            PopFinishedSteps();
            ShowCurrentStep();
        }
    }
    return true;
}

std::list<CUnit*> CUnitServer::LiftNearby(const Vector3& point, char moving) {
    CWorldServer* world = ServerWorld();
    CPlayer* player = unit->player;
    std::list<CUnit*> list;
    if (!player || player->sessionKind_ != 1) return list;
    // Away from the point: the units near it that are not in the player's group are lifted off the sector map
    // and stay in the list.
    float dx = point.x - unit->localPosition_.x;
    float dy = point.y - unit->localPosition_.y;
    float dz = point.z - unit->localPosition_.z;
    if (LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) > 16.0f) {
        CSectorMap* sectors = &world->aiMap_.sectorMap;
        list = sectors->FindUnitsInRadius(point, 4.0f);
        const ScriptGroup* group = &player->Server()->partyUnits_;
        for (auto node = list.begin(); node != list.end();) {
            std::list<CUnit*>::iterator  current = node;
            ++node;
            CUnit* other = *current;
            if (group->Contains(other) == 0) {
                sectors->Remove(other);
                continue;
            }
            list.erase(current);
        }
    }
    // The party members moving (when `moving`) or following this unit are lifted and listed too.
    const PartyMap* party = &player->units_;
    for (auto node = party->begin(); node != party->end(); ++node) {
        CUnit* member = node->second;
        if (!member) break;
        if (member == unit) continue;
        auto* controller = member->ServerController();
        bool lift = moving && (controller->action.kind == UnitOrderKind::Move || controller->action2.kind == UnitOrderKind::Move);
        if (!lift) lift = controller->brain.LeaderOrFollowed() == unit;
        if (!lift) continue;
        auto added = list.emplace(list.end());
        *added = member;
        g_serverWorld.aiMap_.sectorMap.Remove(member);
    }
    return list;
}

void CUnitServer::ReportPath(AIPath* path, char partial) {
    if (brain.action == AIBrain::kActionFollow) return;   // following reports nothing
    CUnit* self = unit;
    const float* goal = &path->goal.x;
    // The path as a motion order for the message. The header is left uninitialised when the path is not partial
    // (the message does not read it for kind 3).
    LocalMotionOrder end;
    AckKind message;
    if (!partial) {
        if (self->player) self->player->Server()->SendPathMessage(self, &end, goal, 3, 1, kFarBits);
        if (brain.LeaderOrFollowed()) return;
        self = unit;
        if (!self->player) return;
        message = AckKind::NoPath;
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
        message = stillFar ? AckKind::Move : AckKind::NoPath;
    }
    PlayerMessage(self->player->Server(), self, message);
}

// --- Resync with the unit ---------------------------------------------------------------------

void CUnitServer::ResyncWithUnit() {
    action.kind = UnitOrderKind::None;
    action.target.Reset();
    action.pointIsUseSpot = 0;
    action.param = 0;
    action.modifier = 0;
    if (action.kind != UnitOrderKind::Idle && action2.kind != UnitOrderKind::Animation && action2.kind != UnitOrderKind::Emote) {   // the first is always true
        action2.kind = UnitOrderKind::None;
        action2.target.Reset();
        action2.pointIsUseSpot = 0;
        action2.param = 0;
        action2.modifier = 0;
        action2.kind = UnitOrderKind::Idle;
        action2.target.Reset();
        action2.modifier = 0;
    }
    RecordFromMode(&moveMode, unit->moveMode);
    RecordFromMode(&moveMode2, unit->moveMode2);
    motion.Clear();
    int now = World()->tick_;
    Vector3 axis;
    unit->localRotation_.AxisY(&axis);
    Vector3 back;
    const Vector3* direction = NegateVector(&back, axis);
    if (motion.Start(unit->localPosition_, *direction, now)) unit->RefreshMotion();
    ClearStanding();
    SetState(UnitState::Stopped);
    unit->UpdateSizeClass();
    unit->pendingWeapon = 0;
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));
    if (state == UnitState::Turning) return;
    std::uint32_t key = WeaponTypeMask() | MoveModeMask(&moveMode);
    if (idleKey == key) {
        idleAnimation.extra = -1;
    } else {
        idleKey = key;
        BuildIdle(&idleAnimation);
    }
    if (!queue.SameAs(&idleAnimation)) {
        // The idle animation starts at a random phase (up to 50 ticks back) on an empty queue.
        if (queue.empty() && queue.extra == -1)
            idleAnimation.extra = World()->tick_ - static_cast<int>(g_random.Next() % 0x33);
        queue = idleAnimation;
        StartQueueIfUnset();
        PopFinishedSteps();
        ShowCurrentStep();
    }
    StandStill();
    SetState(UnitState::Turning);
}

// --- Reset ---------------------------------------------------------------------------------------

void CUnitServer::ResetController() {
    auto releaseRef = [](Ref<CMapObject>& ref) {
        ref.Reset();
    };
    action.kind = UnitOrderKind::None;
    releaseRef(action.target);
    action.pointIsUseSpot = 0;
    action.param = 0;
    action.modifier = 0;
    action2.kind = UnitOrderKind::None;
    releaseRef(action2.target);
    action2.pointIsUseSpot = 0;
    action2.param = 0;
    action2.modifier = 0;
    frozen = 0;
    animationHeld = 0;
    queue = {};
    motion.Clear();
    OrderPlan& plan = currentPlan;
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
    state = UnitState::Stopped;
    hitDirection = kNoHitDirection;
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
    effectObject.Reset();
    actingCheckTime = -1;
    action.kind = UnitOrderKind::Idle;
    action2.kind = UnitOrderKind::None;
    nextAttackTime = 0;
    interceptTime = 0;
    orderStopped = 0;
    deathReported = 0;
    followsLeader = 0;
    followedLeader.Reset();
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
    moveAnimationKey = 0x666DEAD;
    moveAnimation = {};
    idleKey = 0x666DEAD;
    idleAnimation = {};
    boredTicks = 0;
    armourSpells.clear();   // each element's item released
    actionDone = nullptr;
}
