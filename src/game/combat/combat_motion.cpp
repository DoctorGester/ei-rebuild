// Combat, seventh part: the controller's movement modes, idle and movement animations, paths to a point, the
// start of a planned action and the controller's reset. See docs/notes/combat.md ("Update").
#include "game/ai/ai_alarm.h"
#include "game/ai/ai_map.h"
#include "game/ai/ai_motion.h"
#include "game/combat/combat.h"
#include "game/combat/combat_internal.h"
#include "engine/core/core_maths.h"
#include "engine/core/engine_object.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/app/logic_settings.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/core/random.h"
#include "game/script/script.h"
#include "game/ai/sector_map.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"

#include "lib/vecmath.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kKeyIdle = kAnimationSpecialIdleKey;
constexpr std::uint8_t kEmoteStand = 0xFE;   // the idle "emote" of a walking hero

// --- Helpers --------------------------------------------------------------------------------------

constexpr float kHalfCell = 0.5f;


// The unit's movement mode from a record: 3 while it was mode 3 (`wasMode3`), else its mode.
MoveMode UnitModeOf(const MoveModeState& state) { return (state.wasMode3 & 1) ? MoveMode::Run : state.mode; }

// A pause step for an emote (`value` as its variant byte, open-ended), reusing a trailing one.
void AddEmote(AnimationSteps* steps, std::uint8_t value) {
    if (steps->steps.empty() || steps->steps.back().variant != AnimationStep::kNone) steps->steps.emplace_back();
    AnimationStep& last = steps->steps.back();
    last.variant = value;
    last.duration = -1;
}

// The weapon and movement bits of the unit's animation key for the current movement mode.
std::uint32_t WeaponAndModeBits(UnitServer* self) {
    return self->WeaponTypeMask() | self->MoveModeMask(&self->moveMode);
}

// The idle animation into `steps`: a walking hero stands with the emote 0xFE, the others with their idle
// animation for the key, if any.
void FillIdle(UnitServer* self, AnimationSteps* steps) {
    *steps = {};
    if (self->moveMode.mode == MoveMode::Walk && self->moveMode.onGuard == 0) {
        AddEmote(steps, kEmoteStand);
        return;
    }
    const FigureAnimation* entry = self->unit->FindAnimation(WeaponAndModeBits(self) | kKeyIdle, 1);
    if (entry) AddEmote(steps, entry->index);
}

// A player's party, walked like the original's hash_map iteration.
using PartyMap = Player::UnitMap;

// The two variants differ only in the state they set.
void Play(UnitServer* self, const AnimationSteps& steps, UnitState newState) {
    self->SetState(newState);
    self->PlaySteps(&steps);
    self->StandStill();
}

// A movement record from the unit's mode: mode 3 is mode 2 with `wasMode3`.
void RecordFromMode(MoveModeState* record, MoveMode mode) {
    record->wasMode3 = 0;
    record->mode = mode;
    if (mode == MoveMode::Run) {
        record->mode = MoveMode::Walk;
        record->wasMode3 = 1;
    }
}

} // namespace

// --- Small checks -------------------------------------------------------------------------------

bool UnitServer::CanReactFor(int ticks) {
    if (action.kind == UnitOrderKind::Attack && state == UnitState::Turning &&
        OwningWorld()->tick + ticks > nextAttackTime)
        return false;
    return true;
}

// --- Playing an animation ----------------------------------------------------------------------

void UnitServer::PlayAnimationOrder(const AnimationSteps& steps) { Play(this, steps, UnitState::Animating); }

void UnitServer::PlayEmoteOrder(const AnimationSteps& steps) { Play(this, steps, UnitState::Emote); }

// --- Idle and movement animations --------------------------------------------------------------

AnimationSteps* UnitServer::IdleAnimation() {
    std::uint32_t key = WeaponAndModeBits(this);
    if (idleKey == key) {
        idleAnimation.extra = -1;
    } else {
        idleKey = key;
        FillIdle(this, &idleAnimation);
    }
    return &idleAnimation;
}

void UnitServer::PlayIdle() {
    if (state == UnitState::Idle || state == UnitState::Moving || state == UnitState::Turning) return;
    PlaySteps(IdleAnimation());
}

void UnitServer::BuildMoveSteps(AnimationSteps* steps, char once) {
    *steps = {};
    Unit* self = unit;
    std::uint32_t bits = MoveModeMask(&moveMode);
    // A hero's own walk is looped with the walking bits (not when played once).
    if (bits == 0 && !once && self->MapType() == MapObjectType::Hero) bits = AnimationKeyOf(AnimationMoveBits::Walk);
    std::uint32_t key = WeaponTypeMask() | bits |
                        AnimationKeyOf(self->moveMode == MoveMode::Run ? AnimationKind::Run : AnimationKind::Walk);
    if (once) {
        key |= AnimationKeyFlag::Once;
    } else {
        key |= AnimationKeyFlag::Looped;
        if (self->MapType() == MapObjectType::Hero) {
            float stamina = stats.loadFactor;
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
            AnimationStep& last = steps->steps.emplace_back();
            last.animation = index;
            last.duration = duration;
        }
    }
    for (AnimationStep& step : steps->steps) step.kind = 2;
}

AnimationSteps* UnitServer::NextAnimation() {
    Unit* self = unit;
    std::uint32_t key = WeaponAndModeBits(this);
    if (self->moveMode == MoveMode::Run) {
        key |= AnimationKeyOf(AnimationKind::Run);
    } else {
        key |= AnimationKeyOf(AnimationKind::Walk);
        if (self->MapType() == MapObjectType::Hero) {
            float stamina = stats.loadFactor;
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
    for (const AnimationStep& step : loop.steps) {
        if (step.animation == AnimationStep::kNone) {
            // A pause: an emote of its variant byte (the original passes a misaligned dword; AddEmote keeps its low byte
            // only).
            steps.AddEmote(step.variant);
        } else if (step.variant == AnimationStep::kNone) {
            AnimationStep& last = steps.steps.emplace_back();
            last.animation = step.animation;
            last.duration = step.duration;
        } else {
            steps.steps.push_back(step);
        }
    }
    return &steps;
}

// --- Starting the planned action ---------------------------------------------------------------

void UnitServer::StartAction(char follow, char missed) {
    OrderPlan& plan = currentPlan;
    for (AnimationStep& step : plan.animation.steps) step.flags = static_cast<std::uint8_t>(missed);
    if (state != UnitState::Idle && state != UnitState::Moving && state != UnitState::Turning) {
        PlaySteps(IdleAnimation());
    }
    state = UnitState::Acting;
    actingCheckTime = OwningWorld()->tick + 10;
    if (plan.targetId == -1) {
        int now = OwningWorld()->tick;
        motion.StandAndTurnUntil(now, unit, plan.direction, plan.endTime, plan.startTime);
    } else {
        int until = follow ? plan.endTime : 0;
        int now = OwningWorld()->tick;
        motion.StandAndTrack(now, unit, plan.targetId, until, plan.startTime);
    }
    unit->RefreshMotion();
    ClearStanding();
}

// --- Movement modes -----------------------------------------------------------------------------

void UnitServer::UpdateSneak() {
    WorldServer* world = ServerWorld();
    int running = stats.MovementState(1);
    if (running != 0 && moveMode2.wasMode3 != 0) {
        // Out of breath: back from the second mode's mode 3, with a remark.
        AckKind kind = running == 1 ? AckKind::Injured : AckKind::Overloaded;
        if (IsShoutable(kind) && unit->player) PlayerMessage(unit->player->Server(), unit, kind);
        if (action.modifier != 0) action.modifier = 0;
        moveMode2.wasMode3 = 0;
        MoveMode mode = moveMode2.mode;
        if (mode != unit->moveMode2) {
            unit->moveMode2 = mode;
            unit->MarkChanged(NetField::Modes);
        }
    }
    if (moveMode.wasMode3 != 0) {
        if (moveMode2.wasMode3 != 0 && world->aiMap.StandsOnCheapCell(unit) && running == 0) {
            sneakWait = 0;
            return;
        }
        ++sneakWait;
        if (sneakWait <= 5 && moveMode2.wasMode3 != 0) return;
        moveMode.wasMode3 = 0;
        MoveMode mode = moveMode.mode;
        if (mode != unit->moveMode) {
            unit->moveMode = mode;
            unit->MarkChanged(NetField::Modes);
        }
        unit->UpdateSizeClass();
        motionChanged = 1;
        return;
    }
    int allowed = unit->player ? unit->player->godMode : 1;
    if (moveMode.mode != MoveMode::Walk || moveMode2.wasMode3 == 0) return;
    if (allowed == 0 && unit->Vitals().mana <= 0.0f) return;   // the breath
    if (!world->aiMap.StandsOnCheapCell(unit)) return;
    if (running != 0) return;
    sneakWait = 0;
    moveMode.wasMode3 = 1;
    MoveMode mode = UnitModeOf(moveMode);
    if (mode != unit->moveMode) {
        unit->moveMode = mode;
        unit->MarkChanged(NetField::Modes);
    }
    unit->UpdateSizeClass();
    motionChanged = 1;
}

bool UnitServer::ChangeMoveMode() {
    if (moveMode.mode == moveMode2.mode && moveMode.onGuard == moveMode2.onGuard) return false;
    bool modeChanged = moveMode.mode != moveMode2.mode;
    MoveModeState saved = moveMode;
    g_serverWorld.aiMap.sectorMap.Remove(unit);
    bool passableBefore = g_serverWorld.aiMap.IsUnitCellPassable(unit);
    bool sameMode = moveMode.mode == moveMode2.mode;
    moveMode = moveMode2;
    unit->moveMode = UnitModeOf(moveMode);
    unit->UpdateSizeClass();
    bool placed;
    if (!sameMode &&
        !(passableBefore ? g_serverWorld.aiMap.IsUnitCellPassable(unit) : g_serverWorld.aiMap.CanLeaveCell(unit)))
        placed = false;
    else
        placed = g_serverWorld.aiMap.sectorMap.Add(unit);
    unit->MarkChanged(NetField::Modes);
    if (!placed) {
        // The new mode does not fit here: back to the old one, and the party is told.
        moveMode = saved;
        unit->moveMode = UnitModeOf(moveMode);
        moveMode2.mode = moveMode.mode;
        if (moveMode.mode != MoveMode::Walk) moveMode2.wasMode3 = 0;
        unit->moveMode2 = UnitModeOf(moveMode2);
        unit->UpdateSizeClass();
        g_serverWorld.aiMap.sectorMap.Add(unit);
        Unit* self = unit;
        if (self->player) PlayerMessage(self->player->Server(), self, AckKind::CantChangePosition);
        return false;
    }
    if (state == UnitState::Idle) {
        if (modeChanged) {
            StandStill();
            SetState(UnitState::Stopped);
        }
        if (state == UnitState::Idle && !modeChanged) {
            unit->UpdateSizeClass();
            if (action2.kind == UnitOrderKind::None) action2.Assign(&action);
            return true;
        }
    }
    if (action.kind != UnitOrderKind::Attack) {
        AnimationSteps transition;
        if (BuildTransition(&transition, &saved, &moveMode)) {
            StandStill();
            SetState(UnitState::Stopped);
            PlayAnimationOrder(transition);   // the transition played as an animation order
        }
    }
    unit->UpdateSizeClass();
    if (action2.kind == UnitOrderKind::None) action2.Assign(&action);
    return true;
}

// --- Paths --------------------------------------------------------------------------------------

bool UnitServer::PathTo(const Vector3& point, float range) {
    UpdateSneak();
    motionChanged = 0;
    stoodBlocked = 0;
    motion.Clear();
    WorldServer* world = ServerWorld();
    motion.mayExcludeRegions = unit->player ? 0 : 1;
    std::uint32_t before = unit->altPassCosts;
    bool found = world->aiMap.FindPath(unit, point, &motion, range, 0.0f);
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
    SetState(UnitState::Idle);
    if (movingAnimation != 0) {
        // Moving already: the movement animation goes on unless it is the same one-step loop.
        const AnimationSteps& moving = *NextAnimation();
        AnimationSteps& current = queue;
        if (current.steps.size() != 1 || moving.steps.size() != 1 || current.steps[0].variant != moving.steps[0].variant) {
            PlaySteps(&moving);
        }
    }
    return true;
}

std::vector<Unit*> UnitServer::LiftNearby(const Vector3& point, char moving) {
    WorldServer* world = ServerWorld();
    Player* player = unit->player;
    std::vector<Unit*> list;
    if (!player || player->sessionKind != 1) return list;
    // Away from the point (in the plane: a move point's z is no height): the units near it that are not in the
    // player's group are lifted off the sector map and stay in the list.
    if (DistanceSquaredXY(point, unit->localPosition) > 16.0f) {
        SectorMap* sectors = &world->aiMap.sectorMap;
        const ScriptGroup* group = &player->Server()->partyUnits;
        for (Unit* other : sectors->FindUnitsInRadius(point, 4.0f)) {
            if (group->Contains(other) != 0) continue;
            sectors->Remove(other);
            list.push_back(other);
        }
    }
    // The party members moving (when `moving`) or following this unit are lifted and listed too.
    const PartyMap* party = &player->units;
    for (auto node = party->begin(); node != party->end(); ++node) {
        Unit* member = node->second;
        if (!member) break;
        if (member == unit) continue;
        auto* controller = member->ServerController();
        bool lift = moving && (controller->action.kind == UnitOrderKind::Move || controller->action2.kind == UnitOrderKind::Move);
        if (!lift) lift = controller->brain.LeaderOrFollowed() == unit;
        if (!lift) continue;
        list.push_back(member);
        world->aiMap.sectorMap.Remove(member);
    }
    return list;
}

void UnitServer::ReportPath(AIPath* path, char partial) {
    if (brain.action == AIAction::Follow) return;   // following reports nothing
    Unit* self = unit;
    const Vector3& goal = path->goal;
    // The path as a motion order for the message (left empty when the path is not partial: the message does not
    // read it for kind 3).
    LocalMotionOrder end;
    AckKind message;
    if (!partial) {
        if (self->player) self->player->Server()->SendPathMessage(self, &end, goal, 3, 1, kNoPathLimit);
        if (brain.LeaderOrFollowed()) return;
        if (!self->player) return;
        message = AckKind::NoPath;
    } else {
        path->ToMotionOrder(&end);
        float reached = DistanceSquaredXY(self->localPosition, goal);
        float half = kHalfCell;
        float left = LengthSquared2D(half * path->target - XY(goal));
        bool stillFar = reached * 0.010000001f > left;   // unordered: not far
        if (self->player)
            self->player->Server()->SendPathMessage(self, &end, goal, 0, stillFar ? 0 : 1, kNoPathLimit);
        if (brain.LeaderOrFollowed()) return;
        if (!self->player) return;
        message = stillFar ? AckKind::Move : AckKind::NoPath;
    }
    PlayerMessage(self->player->Server(), self, message);
}

// --- Resync with the unit ---------------------------------------------------------------------

void UnitServer::ResyncWithUnit() {
    action.Reset();
    Idle();
    RecordFromMode(&moveMode, unit->moveMode);
    RecordFromMode(&moveMode2, unit->moveMode2);
    StandStill();
    SetState(UnitState::Stopped);
    unit->UpdateSizeClass();
    unit->pendingWeapon = 0;
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        unit->controller->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));
    StandIdle();
}

// --- Reset ---------------------------------------------------------------------------------------

void UnitServer::ResetController() {
    action.Reset();
    action.kind = UnitOrderKind::Idle;
    action2.Reset();
    frozen = 0;
    animationHeld = 0;
    queue = {};
    motion.Clear();
    currentPlan.Reset();
    pendingHit = {.multiplier = 1.0f};
    savedQueue = {};
    state = UnitState::Stopped;
    hitDirection = kNoHitDirection;
    keepHitDirection = 0;
    stoodBlocked = 0;
    motionChanged = 0;
    movingAnimation = 0;
    moveMode = {MoveMode::Walk, 0, 0};
    moveMode2 = {MoveMode::Walk, 0, 0};
    effectObject.Reset();
    actingCheckTime = -1;
    nextAttackTime = 0;
    interceptTime = 0;
    orderStopped = 0;
    deathReported = 0;
    followsLeader = 0;
    followedLeader.Reset();
    searchingTarget = 0;
    brain.Init();
    voice.Reset();
    for (float& factor : moveFactors) factor = 1.0f;
    stats.Reset();
    tameStage = 0;
    ownSide = -1;
    tamerSide = -1;
    speedClass = 15;
    claimedHostile = 0;
    sinkSpeed = 0.0f;
    moveAnimationKey = kNoAnimationKey;
    moveAnimation = {};
    idleKey = kNoAnimationKey;
    idleAnimation = {};
    boredTicks = 0;
    armourSpells.clear();   // each element's item released
    actionDone = nullptr;
}
