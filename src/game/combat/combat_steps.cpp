// Combat, fourth part: the animation steps of an order (which animation of the unit plays, for how long, and
// when the action happens). See combat.h, combat/combat_internal.h and docs/notes/combat.md ("Order lifecycle").
#include "game/combat/combat.h"

#include "game/combat/combat_internal.h"
#include "engine/core/random.h"
#include "game/items/item.h"
#include "game/objects/object3d.h"
#include "game/units/unit.h"

#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"
#include "platform/log.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {
constexpr float kPercent = 0.01f;
constexpr float kHeightSteps = 100.0f;
constexpr float kLowerPart = 0.4f;
constexpr float kUpperPart = 0.8f;
constexpr float kEyeLevel = 0.7f;
constexpr float kNearReach = 5.0f;
constexpr float kSteepUp = 1.73205f;
constexpr float kSteepDown = -1.73205f;
constexpr float kReachSlack = 0.15f;

constexpr int kNoSizeClasses = 2;         // UnitStats::movementKind

constexpr std::uint32_t kKeyAction = AnimationKeyOf(AnimationKind::Special);
constexpr std::uint32_t kKeyGuarded = AnimationKeyOf(AnimationMoveBits::Walk);   // a guarded cast; the attacks have it too
constexpr std::uint32_t kKeyAttack = AnimationKeyOf(AnimationKind::Attack) | kKeyGuarded;
constexpr std::uint32_t kKeyNoWeapon = 0x20;
constexpr std::uint32_t kKeyHigh = AnimationKeyOf(AttackHeight::High);
constexpr std::uint32_t kKeyMiddle = AnimationKeyOf(AttackHeight::Middle);
constexpr std::uint32_t kKeyLow = AnimationKeyOf(AttackHeight::Low);
constexpr std::uint32_t kKeyBlow = kAnimationAttackBlow;
constexpr std::uint32_t kKeyCastStanding = kKeyMiddle | kKeyBlow | AnimationKeyOf(AnimationMoveBits::Crawl);
constexpr std::uint32_t kKeyCastA = kKeyMiddle | kKeyBlow;
constexpr std::uint32_t kKeyCastB = kKeyHigh | kKeyBlow;
constexpr std::uint32_t kKeyNoUnitCast = kKeyMiddle | kKeyBlow | kKeyAttack;
constexpr std::uint32_t kKeyFallback = kKeyMiddle | kKeyBlow | kKeyAttack;    // | weapon bit: any attack of the weapon
constexpr std::uint32_t kKeyNonHeroLow = kKeyLow | kKeyBlow;
constexpr std::uint32_t kKeyNonHeroHigh = kKeyMiddle | kKeyBlow;
constexpr std::uint32_t kKeyNoTarget = kKeyMiddle | kKeyBlow;

// The key of a skill's animation (any weapon).
constexpr std::uint32_t SkillKey(SkillAnimation skill) {
    return AnimationNumberBits(static_cast<int>(skill)) | kKeyAction;
}

// The part an attack on every body part (kAttackAllParts) aims at, picked uniformly from these 20.
constexpr int kRandomPartTable[20] = {0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5};
constexpr int kLocationBody = 1;
constexpr int kLocationCount = 6;

// Logged when not even the fallback attack animation exists (the original's debugger output).
const char* const kNoAnimationMessage = "Fucking ShITTT!!! ArtIsTs MuSt diE!!! CaN'T FiNd pRoPER ANiMatioN!!!";

bool HasRangedWeapon(Unit* unit) {
    ItemWeapon* weapon = unit->ActiveWeapon();
    return weapon && weapon->IsRanged();
}

// Ticks of entry `index`'s `frames` (frameCount the whole animation, moment the moment of the action): the
// zero-extended frame count stored as a float, divided by the unit's speed factor for the entry, truncated (_ftol:
// through 64 bits, the low half, 0 for a NaN).
int EntryTicks(Unit* unit, std::uint32_t index, std::uint32_t FigureAnimation::*frames) {
    FigureAnimation* entry = unit->AnimationEntry(index);
    if (!entry) return 0;
    float frameCount = static_cast<float>(entry->*frames);
    float speed = unit->ActionSpeed(entry);
    return static_cast<int>(static_cast<std::int64_t>(frameCount / speed));
}

// Key bits of the mode being entered (a Cross animation's number: 4, 3, 2, 5 for Lie, Crawl, Walk and Stop; 1 for a
// hero's own walk: Walk without onGuard). The mode being left gives UnitServer::MoveModeMask.
std::uint32_t EnterBits(const MoveModeState* to, const Unit* unit) {
    switch (to->mode) {
    case MoveMode::Lie: return AnimationNumberBits(4);
    case MoveMode::Crawl: return AnimationNumberBits(3);
    case MoveMode::Walk: return AnimationNumberBits(!to->onGuard && unit->MapType() == MapObjectType::Hero ? 1 : 2);
    case MoveMode::Stop: return AnimationNumberBits(5);
    default: return 0;
    }
}

// The animation key of an order of `kind` with `source`. Sets *frame when the unit steps out of its movement mode
// for it (skills, unless walking).
std::uint32_t OrderKey(UnitServer* self, UnitOrderKind kind, std::uint32_t source, std::uint32_t* weaponBit,
                       bool* frame) {
    *weaponBit = 0;
    *frame = false;
    if (kind != UnitOrderKind::Use) {
        *weaponBit = self->WeaponTypeMask();
        return *weaponBit | source | kKeyAttack;
    }
    bool leavesWalk = self->moveMode.mode != MoveMode::Crawl;
    switch (static_cast<UseSource>(source)) {
    case UseSource::Use:
        *frame = leavesWalk;
        return SkillKey(SkillAnimation::Use);
    case UseSource::Steal:
        *frame = leavesWalk;
        return SkillKey(SkillAnimation::Steal);
    case UseSource::Tame:
        *frame = leavesWalk;
        return SkillKey(SkillAnimation::Tame);
    case UseSource::SearchBody: {
        std::uint32_t key;
        if (self->moveMode.mode == MoveMode::Walk) {
            key = SkillKey(HasRangedWeapon(self->unit) ? SkillAnimation::SearchBodyMode2Ranged
                                                       : SkillAnimation::SearchBodyMode2);
        } else {
            key = SkillKey(HasRangedWeapon(self->unit) ? SkillAnimation::SearchBodyRanged : SkillAnimation::SearchBody);
            *frame = leavesWalk;
        }
        if (!self->unit->ActiveWeapon() && static_cast<int>(self->stats.naturalWeapon) >= kAnimatedWeaponTypes) return key | kKeyNoWeapon;
        return key | self->WeaponTypeMask();
    }
    default:
        return source | kKeyAction;
    }
}

// The height band (key bits) of a blow at height `h` on the attacker's body: low up to 0.4 of its
// height, middle up to 0.8, high up to its top; anywhere else the key stays middle.
std::uint32_t BandAt(float h, float base, float size, float top) {
    float low = size * kLowerPart + base;
    float middle = size * kUpperPart + base;
    if (h >= base && h <= low) return kKeyLow;
    if (h >= low && h <= middle) return kKeyMiddle;
    if (h >= middle && h <= top) return kKeyHigh;
    return kKeyMiddle;
}

// A random height in [from, to) in steps of 0.01 (the span in hundredths, rounded), else in the
// overlap [high, low), else `high`. The pick (a dword, exact) times 0.01 rounds once.
float RandomHeight(float to, float from, float low, float high) {
    float span = (to - from) * kHeightSteps;
    float base = from;
    if (lib::RoundEven(span) <= 0) {
        span = (low - high) * kHeightSteps;
        base = high;
        if (lib::RoundEven(span) <= 0) return high;
    }
    std::uint32_t steps = static_cast<std::uint32_t>(lib::RoundEven(span));
    std::uint32_t pick = g_random.Next() % steps;
    return (static_cast<float>(pick) * kPercent) + base;
}

constexpr std::uint32_t kKeyDeath = AnimationNumberBits(0x18);
constexpr std::uint32_t kKeyDeathStanding = kKeyDeath | AnimationKeyOf(AnimationMoveBits::Lie);
constexpr std::uint32_t kKeyFallWalking = AnimationNumberBits(8) | kKeyGuarded;
constexpr std::uint32_t kKeyFall = AnimationNumberBits(0x10);
constexpr std::uint32_t kKeySideMask = ~kAnimationNumberMask;   // the key without its number (and so its side)
constexpr std::uint32_t kKeyNoSide = ~kBlowSideMask;
constexpr int kStepLying = 2;   // the kind of every step of a fall

// A random side for a hero's fall (Back or Front).
std::uint32_t RandomSide(std::uint32_t key) {
    key &= kKeySideMask;
    return key | AnimationKeyOf((g_random.Next() & 1) ? BlowSide::Front : BlowSide::Back);
}

constexpr std::uint8_t kNoStepVariant = 0xFF;    // AnimationStep::animation / variant: none (a pause)

} // namespace

// --- Durations ---------------------------------------------------------------------------------

int UnitServer::AnimationDuration(std::uint32_t index) { return EntryTicks(unit, index, &FigureAnimation::frameCount); }

int UnitServer::AnimationMoment(std::uint32_t index) { return EntryTicks(unit, index, &FigureAnimation::moment); }

// --- Movement mode transitions -------------------------------------------------------------

bool UnitServer::BuildTransition(AnimationSteps* steps, const MoveModeState* from, const MoveModeState* to) {
    *steps = {};
    if (unit->MapType() != MapObjectType::Hero && to->mode != MoveMode::Stop && from->mode != MoveMode::Stop) return false;
    if (from->mode == to->mode && (from->mode != MoveMode::Walk || from->onGuard == to->onGuard)) return false;

    std::uint32_t key = WeaponTypeMask() | (MoveModeMask(from) | AnimationKeyOf(AnimationKind::Cross));
    key |= EnterBits(to, unit);
    FigureAnimation* animation = unit->FindAnimation(key, 1);
    if (!animation) return false;
    std::uint8_t index = animation->index;
    int duration = EntryTicks(unit, index, &FigureAnimation::frameCount);
    AnimationStep& added = steps->steps.emplace_back();
    added.animation = index;
    added.duration = duration;
    return true;
}

// --- Orders ----------------------------------------------------------------------------------

void UnitServer::BuildOrderSteps(AnimationSteps* steps, int* duration, UnitOrderKind kind, std::uint32_t source) {
    *steps = {};
    std::uint32_t weaponBit;
    bool frame;
    std::uint32_t key = OrderKey(this, kind, source, &weaponBit, &frame);

    // A use order whose key fits no entry takes any entry of its skill, as an attack falls back to any attack of its
    // weapon below (a deviation the user chose: the original played none; combat.md, "Animation keys").
    FigureAnimation* animation = unit->FindAnimation(key, kind == UnitOrderKind::Use);
    if ((!animation || (animation->key & weaponBit) != weaponBit) && kind == UnitOrderKind::Attack) {
        animation = unit->FindAnimation(weaponBit | kKeyFallback, 1);
        if (!animation) {
            ei::Log("%s", kNoAnimationMessage);
            animation = unit->AnimationEntry(0);
        }
    }
    *duration = 0;
    if (!animation) return;

    if (frame) {
        // Step out of the movement mode into standing (mode 1) first.
        AnimationSteps out;
        MoveModeState to{};
        to.mode = MoveMode::Crawl;
        if (BuildTransition(&out, &moveMode, &to)) {
            steps->steps.push_back(out.steps[0]);
            *duration += out.steps[0].duration;
        }
    }
    std::uint8_t index = animation->index;
    int length = EntryTicks(unit, index, &FigureAnimation::frameCount);
    AnimationStep& added = steps->steps.emplace_back();
    added.animation = animation->index;
    added.duration = length;
    *duration += EntryTicks(unit, animation->index, &FigureAnimation::moment);
    if (frame) {
        AnimationSteps back;
        MoveModeState from{};
        from.mode = MoveMode::Crawl;
        if (BuildTransition(&back, &from, &moveMode)) steps->steps.push_back(back.steps[0]);
    }
}

// --- Casts -----------------------------------------------------------------------------------

void UnitServer::BuildCastSteps(AnimationSteps* steps, int* duration, UnitOrderKind kind, int value) {
    (void)kind;
    *steps = {};
    std::uint32_t key;
    if (unit->MapType() == MapObjectType::Hero) {
        key = WeaponTypeMask() | AnimationKeyOf(AnimationKind::Cast);
        if (moveMode.mode == MoveMode::Crawl) {
            key |= kKeyCastStanding;
        } else {
            if (moveMode.onGuard) key |= kKeyGuarded;
            key |= (g_random.Next() & 1) ? kKeyCastB : kKeyCastA;
        }
    } else {
        key = kKeyNoUnitCast;
    }
    FigureAnimation* animation = unit->FindAnimation(key, 1);
    *duration = 0;
    if (!animation) return;
    std::uint8_t index = animation->index;

    if (value <= EntryTicks(unit, index, &FigureAnimation::frameCount)) {
        steps->AddStep(index, AnimationDuration(index));
        *duration = AnimationMoment(index);
        return;
    }

    if (stats.movementKind == kNoSizeClasses) {
        // A pause for the extra time, then the animation; the action at its moment.
        *duration = EntryTicks(unit, index, &FigureAnimation::moment);
        int pause = value - EntryTicks(unit, index, &FigureAnimation::frameCount);
        AnimationStep& first = steps->steps.emplace_back();
        first.animation = AnimationStep::kNone;
        first.duration = pause;
        int length = EntryTicks(unit, index, &FigureAnimation::frameCount);
        AnimationStep& second = steps->steps.emplace_back();
        second.animation = index;
        second.duration = length;
        *duration = *duration + pause;
        return;
    }

    // The animation up to its moment, a pause, then the rest of it.
    int moment = EntryTicks(unit, index, &FigureAnimation::moment);
    *duration = moment;
    int pause = value - EntryTicks(unit, index, &FigureAnimation::frameCount);
    AnimationStep& first = steps->steps.emplace_back();
    first.animation = index;
    first.duration = moment;
    AnimationStep& second = steps->steps.emplace_back();
    second.animation = AnimationStep::kNone;
    second.duration = pause;
    int rest = EntryTicks(unit, index, &FigureAnimation::frameCount) - *duration;
    AnimationStep& third = steps->steps.emplace_back();
    third.animation = AnimationStep::kNone;
    third.duration = rest;
    steps->steps[1].kind = 1;
    *duration = *duration + pause;
}

// --- Attacks ---------------------------------------------------------------------------------

void UnitServer::BuildAttackSteps(OrderPlan* plan, AnimationSteps* steps, int* duration, MapObject* target) {
    Unit* victim = UnitOrNull(target);   // the target is set
    if (!victim) {
        plan->hitLocation = 1;
        BuildOrderSteps(steps, duration, UnitOrderKind::Attack, kKeyNoTarget);
        return;
    }

    // An attack order on the target, as ActionDistance sees it (the target is referenced).
    UnitAction order{};
    order.kind = UnitOrderKind::Attack;
    order.source = kAttackAllParts;
    order.target.Assign(target);

    const Vector3 here = unit->localPosition;
    Vector3 aim = target->AimPoint();
    float distance = std::sqrt(LengthSquared(here - aim));
    float orderDistance = ActionDistance(&order);
    float reach = SelectLess(distance, orderDistance, distance, orderDistance);

    UnitStats* victimStats = &victim->ServerController()->stats;
    float myBase = unit->GroundHeight(here.x, here.y) + here.z;
    float mySize = unit->DoubleHeight();
    float theirBase = victim->GroundHeight(aim.x, aim.y) + aim.z;
    float theirSize = victim->DoubleHeight();

    // Where to hit: an attack on every part picks at random, else the order's part (the body when it is no part);
    // the victim's stats move it to a part that can still be hit.
    int location;
    std::uint32_t source = action.source;
    if (source == kAttackAllParts) {
        int around = kRandomPartTable[g_random.Next() % 20];
        location = victimStats->PickLocation(around);
    } else if (static_cast<int>(source) < 0 || static_cast<int>(source) >= kLocationCount) {
        location = victimStats->PickLocation(kLocationBody);
    } else {
        location = static_cast<int>(source);
    }

    // The height of the blow: the gap when the bodies do not overlap, else a random height on the
    // location within the overlap, measured from the attacker's eye level.
    std::uint32_t key = kKeyMiddle;
    float blow;
    float myTop = mySize + myBase;
    if (myTop < theirBase) {
        blow = theirBase - myTop;
        key = kKeyHigh;
    } else {
        float theirTop = theirSize + theirBase;
        if (theirTop < myBase) {
            blow = myBase - theirTop;
            key = kKeyLow;
        } else {
            float low = SelectLess(theirTop, myTop, theirTop, myTop);         // top of the overlap
            float high = SelectLess(myBase, theirBase, theirBase, myBase);    // its bottom
            float from = theirBase;
            float to;
            switch (static_cast<std::uint32_t>(location)) {
            case 0: {   // head
                float part = theirSize * kUpperPart + theirBase;
                from = SelectLess(high, part, part, high);
                to = SelectLess(theirTop, low, theirTop, low);
                break;
            }
            case 4:
            case 5: {   // legs
                from = SelectLess(high, theirBase, theirBase, high);
                float part = theirSize * kLowerPart + theirBase;
                to = SelectLess(part, low, part, low);
                break;
            }
            default: {   // body and arms (1..3), and the body for any other location
                float part = theirSize * kLowerPart + theirBase;
                from = SelectLess(high, part, part, high);
                float upper = theirSize * kUpperPart + theirBase;
                to = SelectLess(upper, low, upper, low);
                break;
            }
            }
            float h = RandomHeight(to, from, low, high);
            key = BandAt(h, myBase, mySize, myTop);
            float offset = (h - myBase) + mySize * kEyeLevel;
            blow = AbsBits(offset);
        }
    }

    // Beyond close range the slope of the blow decides instead.
    float slope = blow / reach;
    if (reach > kNearReach) {
        if (slope > kSteepUp) key = kKeyHigh;
        else if (slope < kSteepDown) key = kKeyLow;
        else key = kKeyMiddle;
    }
    key |= kKeyBlow;

    if (unit->MapType() != MapObjectType::Hero) {
        // Other creatures strike low at targets that reach further than the distance between them.
        float myRadius = unit->Radius();
        MapObject* other = order.target;
        float span = other->Radius() + myRadius;
        if (Unit* otherUnit = UnitOrNull(other)) {
            float scale = (stats.reachHeightScale + 1.0f) * myRadius;
            span = otherUnit->standingHeight + scale;
        }
        key = span - kReachSlack > distance ? kKeyNonHeroLow : kKeyNonHeroHigh;
    }
    plan->hitLocation = location;
    order.target.Reset();

    BuildOrderSteps(steps, duration, UnitOrderKind::Attack, key);
}

// --- Death and falls -------------------------------------------------------------------------

void UnitServer::BuildDeathAnimation(AnimationSteps* steps, BlowSide direction, int dying, int fall) {
    bool isDying = static_cast<std::uint8_t>(dying) != 0;
    bool isFall = static_cast<std::uint8_t>(fall) != 0;
    *steps = {};
    std::uint32_t key = WeaponTypeMask() | MoveModeMask(&moveMode);
    MoveMode mode = moveMode.mode;

    if (unit->MapType() == MapObjectType::Hero) {
        if (isDying && mode != MoveMode::Stop) {
            if (mode == MoveMode::Crawl) {
                if (isFall) key = RandomSide(key);
            } else if (mode != MoveMode::Lie) {
                key |= AnimationKeyOf(direction);
                if (isFall && (key & 1)) key = RandomSide(key);
            }
        }
        key &= ~kKeyGuarded;
        if (isFall && mode != MoveMode::Lie) {
            key |= moveMode.wasMode3 && mode == MoveMode::Walk ? kKeyFallWalking : kKeyFall;
        } else {
            key |= mode == MoveMode::Lie ? kKeyDeathStanding : kKeyDeath;
        }
    } else {
        if (isFall && !isDying) return;
        // A non-hero's death takes the blow's side only from the front or back.
        if (direction == BlowSide::MinusX || direction == BlowSide::PlusX) key |= kKeyDeath;
        else key |= AnimationKeyOf(direction) | kKeyDeath;
    }
    key |= AnimationKeyOf(isDying ? AnimationKind::Death : AnimationKind::Hit);

    FigureAnimation* animation = unit->FindAnimation(key, 1);
    bool add;
    if (isDying) {
        // (The original reads the dying argument here.)
        if (!animation) animation = unit->FindAnimation(key & kKeyNoSide, 1);
        add = animation != nullptr;
    } else {
        add = moveMode.mode != MoveMode::Lie && animation;
    }
    if (add) {
        std::uint8_t index = animation->index;
        int duration = EntryTicks(unit, index, &FigureAnimation::frameCount);
        AnimationStep& added = steps->steps.emplace_back();
        added.animation = animation->index;
        added.duration = duration;
    }
    if (isFall)
        for (AnimationStep& step : steps->steps) step.kind = kStepLying;
}

// --- The step lists ----------------------------------------------------------------------------

// AnimationSteps, the step lists of the unit controller's animations: appending steps and
// emotes, flags, duration, comparison. See combat.h and docs/notes/combat.md.

void AnimationSteps::AddStep(int animation, int duration) {
    AnimationStep& last = steps.emplace_back();
    last.animation = static_cast<std::uint8_t>(animation);
    last.duration = duration;
}

void AnimationSteps::AddEmote(int emote) {
    if (steps.empty() || steps.back().variant != kNoStepVariant) steps.emplace_back();
    AnimationStep& last = steps.back();
    last.variant = static_cast<std::uint8_t>(emote);
    last.duration = -1;
}

int AnimationSteps::Duration() const {
    int total = 0;
    for (const AnimationStep& step : steps) {
        if (step.duration < 0) return -1;
        total += step.duration;
    }
    return total;
}

bool AnimationSteps::SameAs(const AnimationSteps* other) const {
    if (steps.size() != other->steps.size()) return false;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const AnimationStep& a = steps[i];
        const AnimationStep& b = other->steps[i];
        if (a.animation != b.animation || a.variant != b.variant || a.duration != b.duration ||
            a.kind != b.kind || a.flags != b.flags)
            return false;
    }
    return true;
}

void AnimationSteps::Load(ByteStream* stream) {
    steps.resize(stream->ReadCount());
    for (AnimationStep& step : steps) stream->ReadValue(step);
    stream->ReadValue(extra);
}

void AnimationSteps::Save(ByteStream* stream) const {
    stream->WriteCount(steps.size());
    for (const AnimationStep& step : steps) stream->WriteValue(step);
    int last = extra;
    stream->WriteValue(last);
}
