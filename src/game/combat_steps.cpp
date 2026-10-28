// Combat, fourth part: the animation steps of an order (which animation of the unit plays, for how long, and
// when the action happens). See combat.h, combat_plan.h and docs/notes/combat.md ("Order lifecycle").
#include "combat.h"
#include "combat_plan.h"

#include "item.h"
#include "map_object_figures.h"
#include "maths.h"
#include "random.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/x87.h"
#include "unit.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kZero = 0.0f;
constexpr float kOne = 1.0f;
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);       // 0.01
constexpr float kHundred = 100.0f;
constexpr float kLowerPart = std::bit_cast<float>(0x3ECCCCCDu);     // 0.4
constexpr float kUpperPart = std::bit_cast<float>(0x3F4CCCCDu);     // 0.8
constexpr float kEyeLevel = std::bit_cast<float>(0x3F333333u);      // 0.7
constexpr float kNearReach = 5.0f;
constexpr float kSteepUp = std::bit_cast<float>(0x3FDDB3D0u);       // 1.73205
constexpr float kSteepDown = std::bit_cast<float>(0xBFDDB3D0u);     // -1.73205
constexpr float kReachSlack = std::bit_cast<float>(0x3E19999Au);    // 0.15

constexpr int kUnitTypeHeroes = 0x32;     // CEngineObject::type_ of the player's own units
constexpr int kNoNaturalWeapon = 7;       // natural weapon types from 7 up have no animation bit
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr int kOrderAttack = 3;
constexpr int kOrderSkill = 6;
constexpr std::uint32_t kSkillOnTarget = 0x18A95;
constexpr int kNoSizeClasses = 2;         // UnitStats::movementKind

constexpr std::uint32_t kKeyAction = 0x40000;
constexpr std::uint32_t kKeyAttack = 0x90000;
constexpr std::uint32_t kKeyCast = 0xC0000;
constexpr std::uint32_t kKeyCastStanding = 0x2820000;
constexpr std::uint32_t kKeyCastA = 0x2800000;
constexpr std::uint32_t kKeyCastB = 0x1800000;
constexpr std::uint32_t kKeyNoUnitCast = 0x2890000;
constexpr std::uint32_t kKeyFlag10000 = 0x10000;
constexpr std::uint32_t kKeyFallback = 0x2890000;    // | weapon bit: any attack of the weapon
constexpr std::uint32_t kKeyTransition = 0x240000;
constexpr std::uint32_t kKeyNoWeapon = 0x20;
constexpr std::uint32_t kKeyHigh = 0x1000000;
constexpr std::uint32_t kKeyMiddle = 0x2000000;
constexpr std::uint32_t kKeyLow = 0x3000000;
constexpr std::uint32_t kKeyBlow = 0x800000;
constexpr std::uint32_t kKeyNonHeroLow = 0x3800000;
constexpr std::uint32_t kKeyNonHeroHigh = 0x2800000;
constexpr std::uint32_t kKeyNoTarget = 0x2800000;

// Hit locations picked for a skill order (source 6), uniformly from these 20.
constexpr int kSkillLocations[20] = {0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5};
constexpr int kLocationBody = 1;
constexpr int kLocationCount = 6;

// Printed when not even the fallback attack animation exists.
const char* const kNoAnimationMessage = "Fucking ShITTT!!! ArtIsTs MuSt diE!!! CaN'T FiNd pRoPER ANiMatioN!!!\n";

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// Branch-free selects: `ifLess` when a < b or unordered.
float Select(float a, float b, float ifLess, float otherwise) {
    return std::bit_cast<float>(a >= b ? Bits(otherwise) : Bits(ifLess));
}

CItemWeapon* ActiveWeapon(CUnit* unit) { return unit->GetWeapon(unit->activeWeapon); }

// 1 << the active weapon's type, else of the natural weapon (none from type 7 up).
std::uint32_t WeaponBit(CUnitServer* self) {
    CItemWeapon* weapon = ActiveWeapon(self->unit);
    if (weapon) return 1u << (weapon->GetType() & 31);
    int natural = self->stats.naturalWeapon;
    return natural < kNoNaturalWeapon ? 1u << (natural & 31) : 0u;
}

bool HasRangedWeapon(CUnit* unit) {
    CItemWeapon* weapon = ActiveWeapon(unit);
    return weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow);
}

} // namespace

namespace {

} // namespace

namespace {
// _ftol of a / b, the quotient as the FPU leaves it (at the game's 24- or 53-bit
// precision a double holds it exactly).
int FtolQuotient(double a, double b) { return static_cast<int>(gamecrt::Ftol(a / b)); }

// Ticks of entry `index`'s `frames` (frameCount the whole animation, moment the moment of the action): the
// zero-extended frame count stored as a float, divided by the unit's speed factor for the entry, truncated.
int EntryTicks(CUnit* unit, std::uint32_t index, std::uint32_t FigureAnimation::*frames) {
    FigureAnimation* entry = unit->AnimationEntry(index);
    if (!entry) return 0;
    volatile std::int64_t wide = entry->*frames;
    float frameCount = static_cast<float>(wide);
    double speed = unit->ActionSpeed(entry);
    return FtolQuotient(frameCount, speed);
}

} // namespace

// --- Durations ---------------------------------------------------------------------------------

int CUnitServer::AnimationDuration(std::uint32_t index) { return EntryTicks(unit, index, &FigureAnimation::frameCount); }

int CUnitServer::AnimationMoment(std::uint32_t index) { return EntryTicks(unit, index, &FigureAnimation::moment); }

// --- Movement mode transitions -------------------------------------------------------------

namespace {
// Key bits of the mode being left / entered (mode 2 without flag4 is a hero's own walk).
std::uint32_t LeaveBits(const MoveModeState* from, const CUnit* unit) {
    switch (from->mode) {
    case 0: return 0x30000;
    case 1: return 0x20000;
    case 2: return !from->flag4 && unit->type_ == kUnitTypeHeroes ? 0 : 0x10000;
    case 4: return 0x8000;
    default: return 0;
    }
}

std::uint32_t EnterBits(const MoveModeState* to, const CUnit* unit) {
    switch (to->mode) {
    case 0: return 0x1000000;
    case 1: return 0xC00000;
    case 2: return !to->flag4 && unit->type_ == kUnitTypeHeroes ? 0x400000 : 0x800000;
    case 4: return 0x1400000;
    default: return 0;
    }
}
} // namespace

bool CUnitServer::BuildTransition(AnimationSteps* steps, const MoveModeState* from, const MoveModeState* to) {
    *steps = {};
    if (unit->type_ != kUnitTypeHeroes && to->mode != 4 && from->mode != 4) return false;
    if (from->mode == to->mode && (from->mode != 2 || from->flag4 == to->flag4)) return false;

    std::uint32_t key = WeaponBit(this) | (LeaveBits(from, unit) | kKeyTransition);
    key |= EnterBits(to, unit);
    FigureAnimation* animation = unit->FindAnimation(key, 1);
    if (!animation) return false;
    std::uint8_t index = animation->index;
    int duration = EntryTicks(unit, index, &FigureAnimation::frameCount);
    AnimationStep& added = steps->emplace_back();
    added.animation = index;
    added.duration = duration;
    return true;
}

// --- Orders ----------------------------------------------------------------------------------

namespace {
// The animation key of an order of `kind` with `source` (`steps` stands in for the source of an unknown
// skill, as in the original). Sets *frame when the unit steps out of its movement mode for it (skills, unless
// walking).
std::uint32_t OrderKey(CUnitServer* self, int kind, std::uint32_t source, const AnimationSteps* steps,
                       std::uint32_t* weaponBit, bool* frame) {
    *weaponBit = 0;
    *frame = false;
    if (kind != kOrderSkill) {
        *weaponBit = WeaponBit(self);
        return *weaponBit | source | kKeyAttack;
    }
    bool leavesWalk = self->moveMode.mode != 1;
    switch (source) {
    case 0:
        *frame = leavesWalk;
        return 0x5400000 | kKeyAction;
    case 1:
        *frame = leavesWalk;
        return 0x5000000 | kKeyAction;
    case 2:
        *frame = leavesWalk;
        return 0x4C00000 | kKeyAction;
    case kSkillOnTarget: {
        std::uint32_t key;
        if (self->moveMode.mode == 2) {
            key = HasRangedWeapon(self->unit) ? 0xD000000 : 0xC000000;
        } else {
            key = HasRangedWeapon(self->unit) ? 0x5800000 : 0xD400000;
            *frame = leavesWalk;
        }
        if (!ActiveWeapon(self->unit) && self->stats.naturalWeapon >= kNoNaturalWeapon)
            return key | kKeyNoWeapon | kKeyAction;
        return key | WeaponBit(self) | kKeyAction;
    }
    default:
        // sic: the address of `steps` where the source belongs.
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(steps)) | kKeyAction;
    }
}
} // namespace

void CUnitServer::BuildOrderSteps(AnimationSteps* steps, int* duration, int kind, std::uint32_t source) {
    *steps = {};
    std::uint32_t weaponBit;
    bool frame;
    std::uint32_t key = OrderKey(this, kind, source, steps, &weaponBit, &frame);

    FigureAnimation* animation = unit->FindAnimation(key, 0);
    if ((!animation || (animation->key & weaponBit) != weaponBit) && kind == kOrderAttack) {
        animation = unit->FindAnimation(weaponBit | kKeyFallback, 1);
        if (!animation) {
            gameapi::OutputDebugStringA(kNoAnimationMessage);
            animation = unit->AnimationEntry(0);
        }
    }
    *duration = 0;
    if (!animation) return;

    if (frame) {
        // Step out of the movement mode into standing (mode 1) first.
        AnimationSteps out;
        MoveModeState to{};
        to.mode = 1;
        if (BuildTransition(&out, &moveMode, &to)) {
            steps->push_back(out[0]);
            *duration += out[0].duration;
        }
    }
    std::uint8_t index = animation->index;
    int length = EntryTicks(unit, index, &FigureAnimation::frameCount);
    AnimationStep& added = steps->emplace_back();
    added.animation = animation->index;
    added.duration = length;
    *duration += EntryTicks(unit, animation->index, &FigureAnimation::moment);
    if (frame) {
        AnimationSteps back;
        MoveModeState from{};
        from.mode = 1;
        if (BuildTransition(&back, &from, &moveMode)) steps->push_back(back[0]);
    }
}

// --- Casts -----------------------------------------------------------------------------------

void CUnitServer::BuildCastSteps(AnimationSteps* steps, int* duration, int kind, int value) {
    (void)kind;
    *steps = {};
    std::uint32_t key;
    if (unit->type_ == kUnitTypeHeroes) {
        key = WeaponBit(this) | kKeyCast;
        if (moveMode.mode == 1) {
            key |= kKeyCastStanding;
        } else {
            if (moveMode.flag4) key |= kKeyFlag10000;
            key |= (IsaacRandom::Global().Next() & 1) ? kKeyCastB : kKeyCastA;
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
        AnimationStep& first = steps->emplace_back();
        first.animation = 0xFF;
        first.duration = pause;
        int length = EntryTicks(unit, index, &FigureAnimation::frameCount);
        AnimationStep& second = steps->emplace_back();
        second.animation = index;
        second.duration = length;
        *duration = *duration + pause;
        return;
    }

    // The animation up to its moment, a pause, then the rest of it.
    int moment = EntryTicks(unit, index, &FigureAnimation::moment);
    *duration = moment;
    int pause = value - EntryTicks(unit, index, &FigureAnimation::frameCount);
    AnimationStep& first = steps->emplace_back();
    first.animation = index;
    first.duration = moment;
    AnimationStep& second = steps->emplace_back();
    second.animation = 0xFF;
    second.duration = pause;
    int rest = EntryTicks(unit, index, &FigureAnimation::frameCount) - *duration;
    AnimationStep& third = steps->emplace_back();
    third.animation = 0xFF;
    third.duration = rest;
    steps->SetStepFlags(0);
    (*steps)[1].kind = 1;
    *duration = *duration + pause;
}

// --- Attacks ---------------------------------------------------------------------------------

namespace {
// The height band (key bits) of a blow at height `h` on the attacker's body: low up to 0.4 of its
// height, middle up to 0.8, high up to its top; anywhere else the key stays middle.
std::uint32_t BandAt(double h, float base, float size, float top) {
    if (h >= base) {
        double low = static_cast<double>(size) * kLowerPart + base;
        EI_NAN_AUDIT(low);
        if (h <= low) return kKeyLow;
    }
    double low = static_cast<double>(size) * kLowerPart + base;
    if (h >= low) {
        double middle = static_cast<double>(size) * kUpperPart + base;
        EI_NAN_AUDIT(middle);
        if (h <= middle) return kKeyMiddle;
    }
    double middle = static_cast<double>(size) * kUpperPart + base;
    EI_NAN_AUDIT(top);
    if (h >= middle && h <= top) return kKeyHigh;
    return kKeyMiddle;
}

// A random height in [from, to) in steps of 0.01 (the span in hundredths, rounded), else in the
// overlap [high, low), else `high`.
double RandomHeight(double to, float from, float low, float high) {
    float span = static_cast<float>((to - from) * kHundred);
    if (x87::fistp(span) > 0) {
        std::uint32_t steps = static_cast<std::uint32_t>(x87::fistp(span));
        volatile std::int64_t pick = IsaacRandom::Global().Next() % steps;
        return static_cast<double>(pick) * kPercent + from;
    }
    float span2 = static_cast<float>((static_cast<double>(low) - high) * kHundred);
    if (x87::fistp(span2) > 0) {
        std::uint32_t steps = static_cast<std::uint32_t>(x87::fistp(span2));
        volatile std::int64_t pick = IsaacRandom::Global().Next() % steps;
        return static_cast<double>(pick) * kPercent + high;
    }
    return high;
}
} // namespace

bool CUnitServer::BuildAttackSteps(OrderPlan* plan, AnimationSteps* steps, int* duration, CMapObject* target) {
    CUnit* victim = UnitOrNull(target);   // the target is set
    if (!victim) {
        plan->hitLocation = 1;
        BuildOrderSteps(steps, duration, kOrderAttack, kKeyNoTarget);
        return true;
    }

    // An attack order on the target, as ActionDistance sees it (the target is referenced).
    UnitAction order{};
    order.kind = kOrderAttack;
    order.source = 6;
    order.target = target;
    ++target->refCount_;

    float x = unit->localPosition_.x;
    float y = unit->localPosition_.y;
    float z = unit->localPosition_.z;
    Vector3 aim;
    target->AimPoint(&aim);
    double dz = static_cast<double>(z) - aim.z;
    double dy = static_cast<double>(y) - aim.y;
    double dx = static_cast<double>(x) - aim.x;
    float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
    float orderDistance = static_cast<float>(ActionDistance(&order));
    float reach = Select(distance, orderDistance, distance, orderDistance);

    UnitStats* victimStats = &victim->ServerController()->stats;
    float myBase = static_cast<float>(unit->GroundHeight(x, y) + z);
    float mySize = static_cast<float>(unit->DoubleHeight());
    float theirBase = static_cast<float>(victim->GroundHeight(aim.x, aim.y) + aim.z);
    float theirSize = static_cast<float>(victim->DoubleHeight());

    // Where to hit: a skill picks at random, an attack its source location, else the body; the
    // victim's stats move it to a part that can still be hit.
    int location;
    std::uint32_t source = action.source;
    if (source == 6) {
        int around = kSkillLocations[IsaacRandom::Global().Next() % 20];
        location = victimStats->PickLocation(around);
    } else if (static_cast<int>(source) < 0 || static_cast<int>(source) >= kLocationCount) {
        location = victimStats->PickLocation(kLocationBody);
    } else {
        location = static_cast<int>(source);
    }

    // The height of the blow: the gap when the bodies do not overlap, else a random height on the
    // location within the overlap, measured from the attacker's eye level.
    std::uint32_t key = kKeyMiddle;
    double blow;
    double myTopValue = static_cast<double>(mySize) + myBase;
    float myTop = static_cast<float>(myTopValue);
    EI_NAN_AUDIT(myTopValue, theirBase);
    if (myTopValue < theirBase) {
        blow = static_cast<double>(theirBase) - myTop;
        key = kKeyHigh;
    } else {
        double theirTopValue = static_cast<double>(theirSize) + theirBase;
        float theirTop = static_cast<float>(theirTopValue);
        EI_NAN_AUDIT(theirTopValue, myBase);
        if (theirTopValue < myBase) {
            blow = static_cast<double>(myBase) - theirTopValue;
            key = kKeyLow;
        } else {
            float low = Select(theirTop, myTop, theirTop, myTop);         // top of the overlap
            float high = Select(myBase, theirBase, theirBase, myBase);    // its bottom
            float from = theirBase;
            double to;
            switch (static_cast<std::uint32_t>(location)) {
            case 0: {   // head
                float part = static_cast<float>(static_cast<double>(theirSize) * kUpperPart + theirBase);
                from = Select(high, part, part, high);
                to = Select(theirTop, low, theirTop, low);
                break;
            }
            case 1:
            case 2:
            case 3: {   // body and arms
                float part = static_cast<float>(static_cast<double>(theirSize) * kLowerPart + theirBase);
                from = Select(high, part, part, high);
                float upper = static_cast<float>(static_cast<double>(theirSize) * kUpperPart + theirBase);
                to = Select(upper, low, upper, low);
                break;
            }
            case 4:
            case 5: {   // legs
                from = Select(high, theirBase, theirBase, high);
                float part = static_cast<float>(static_cast<double>(theirSize) * kLowerPart + theirBase);
                to = Select(part, low, part, low);
                break;
            }
            default:
                // sic: the plan's address read as a float.
                to = std::bit_cast<float>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(plan)));
                break;
            }
            double h = RandomHeight(to, from, low, high);
            key = BandAt(h, myBase, mySize, myTop);
            float offset = static_cast<float>((h - myBase) + static_cast<double>(mySize) * kEyeLevel);
            blow = std::bit_cast<float>(Bits(offset) & 0x7FFFFFFFu);
        }
    }

    // Beyond close range the slope of the blow decides instead.
    double slope = blow / reach;
    if (reach > kNearReach) {
        EI_NAN_AUDIT(slope);
        if (slope > kSteepUp) key = kKeyHigh;
        else if (slope < kSteepDown) key = kKeyLow;
        else key = kKeyMiddle;
    }
    key |= kKeyBlow;

    if (unit->type_ != kUnitTypeHeroes) {
        // Other creatures strike low at targets that reach further than the distance between them.
        float myRadius = unit->Radius();
        CMapObject* other = order.target;
        float span = static_cast<float>(static_cast<double>(other->Radius()) + myRadius);
        if (CUnit* otherUnit = UnitOrNull(other)) {
            double scale = (static_cast<double>(std::bit_cast<float>(stats.modelField8)) + kOne) * myRadius;
            span = static_cast<float>(static_cast<double>(otherUnit->standingHeight) + scale);
        }
        key = static_cast<double>(span) - kReachSlack > distance ? kKeyNonHeroLow : kKeyNonHeroHigh;
    }
    plan->hitLocation = location;
    if (order.target) order.target->Release();
    order.target = nullptr;

    BuildOrderSteps(steps, duration, kOrderAttack, key);
    return true;
}

// --- Death and falls -------------------------------------------------------------------------

namespace {
constexpr std::uint32_t kDirectionFront = 0xC00000;   // HitDirection's bits that need no extra key bit
constexpr std::uint32_t kDirectionBack = 0x1000000;
constexpr std::uint32_t kKeyDeath = 0x6000000;
constexpr std::uint32_t kKeyDeathStanding = 0x6030000;
constexpr std::uint32_t kKeyFallWalking = 0x2010000;
constexpr std::uint32_t kKeyFall = 0x4000000;
constexpr std::uint32_t kKeyDying = 0x1C0000;
constexpr std::uint32_t kKeyNotDying = 0x200000;
constexpr std::uint32_t kKeySideMask = 0xC03FFFFF;
constexpr std::uint32_t kKeyNoSide = 0xFE3FFFFF;
constexpr int kStepLying = 2;   // the kind of every step of a fall

// A random side for a hero's fall (0x400000 or 0x800000).
std::uint32_t RandomSide(std::uint32_t key) {
    key &= kKeySideMask;
    return key | ((IsaacRandom::Global().Next() & 1) ? 0x800000u : 0x400000u);
}
} // namespace

void CUnitServer::BuildDeathAnimation(AnimationSteps* steps, std::uint32_t direction, int dying, int fall) {
    bool isDying = static_cast<std::uint8_t>(dying) != 0;
    bool isFall = static_cast<std::uint8_t>(fall) != 0;
    *steps = {};
    std::uint32_t key = WeaponBit(this) | LeaveBits(&moveMode, unit);
    int mode = moveMode.mode;

    if (unit->type_ == kUnitTypeHeroes) {
        if (isDying && mode != 4) {
            if (mode == 1) {
                if (isFall) key = RandomSide(key);
            } else if (mode != 0) {
                key |= direction;
                if (isFall && (key & 1)) key = RandomSide(key);
            }
        }
        key &= ~kKeyFlag10000;
        if (isFall && mode != 0) {
            key |= moveMode.wasMode3 && mode == 2 ? kKeyFallWalking : kKeyFall;
        } else {
            key |= mode == 0 ? kKeyDeathStanding : kKeyDeath;
        }
    } else {
        if (isFall && !isDying) return;
        if (direction == kDirectionFront || direction == kDirectionBack) key |= kKeyDeath;
        else key |= direction | kKeyDeath;
    }
    key |= isDying ? kKeyDying : kKeyNotDying;

    FigureAnimation* animation = unit->FindAnimation(key, 1);
    bool add;
    if (isDying) {
        // (The original reads the dying argument here.)
        if (!animation) animation = unit->FindAnimation(key & kKeyNoSide, 1);
        add = animation != nullptr;
    } else {
        add = moveMode.mode != 0 && animation;
    }
    if (add) {
        std::uint8_t index = animation->index;
        int duration = EntryTicks(unit, index, &FigureAnimation::frameCount);
        AnimationStep& added = steps->emplace_back();
        added.animation = animation->index;
        added.duration = duration;
    }
    if (isFall)
        for (AnimationStep& step : *steps) step.kind = kStepLying;
}
