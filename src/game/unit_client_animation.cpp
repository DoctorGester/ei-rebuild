// The client controller moving a unit's animation through time (CUnitClientSpecific slots 14 and
// 40, the idle animation) and the client half's pulses of invisible and stunned units. See
// unit_client_models.h, map_object_shadow.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "item_db.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "random.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_units.h"

#include <cmath>

namespace {
constexpr std::uint32_t kFlagFrozen = 0x10000000;     // the unit's flags_
constexpr std::int8_t kModeSpecialIdle = -2;          // the unit's +0x255
constexpr std::int8_t kNoEntry = -1;                  // +0x254 / +0x255
constexpr float kLongStep = 500.0f;                   // 0073BAB4
constexpr std::uint32_t kLongStepSpread = 101;        // a long step becomes 500..600
constexpr int kMotionPaused = 1;                      // the unit's +0x25C
constexpr int kMotionScaled = 2;
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFFu;
constexpr std::uint32_t kNoPrototype = 0xFFFF;
constexpr std::uint8_t kNaturalWeapons = 7;
constexpr std::uint32_t kKeyNotHero = 0x10000;
constexpr std::uint32_t kKeySpecialIdle = 0x80580000;
constexpr double kPulsePeriod = 30.0;                 // 0073DF70
constexpr float kPulseFold = 15.0f;                   // 0073C518
constexpr float kPulseFull = 30.0f;                   // 0073BAF0
constexpr float kPulseScale = 0.06666667f;            // 0073DF6C, 1/15
constexpr float kHalf = 0.5f;                         // 0073B86C
constexpr float kPulseBase = 0.2f;                    // 0073BAD4
constexpr std::uint32_t kPartTinted = 0x10;           // FigurePart::flags_

// 00535850 (unit_client_path.cpp, rewritten; through its game address so its wrapper sees the
// call; RET 8). Places the unit on its path at `time`, turning by `rate`.
__declspec(naked) void __fastcall CallPlaceOnPath(CUnitClientSpecific*, void*, float /*time*/, float /*rate*/)
    EI_ORIGINAL(0x00535850)
// 0051CC20 / 0051CCD0 (rewritten below; through their game addresses).
__declspec(naked) void __fastcall CallInvisiblePulse(CObject3DClientSpecific*, void*, float) EI_ORIGINAL(0x0051CC20)
__declspec(naked) void __fastcall CallStunnedPulse(CObject3DClientSpecific*, void*, float) EI_ORIGINAL(0x0051CCD0)

CObject3DClientSpecific* ClientHalf(CUnit* unit) {
    return static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached);
}

// The pulse's value after the clock moved on by `frames`.
float Pulse(float& clock, float frames) {
    volatile float now = frames + clock;   // FST to a float local, FSTP to the clock
    clock = now;
    double phase = std::fmod(static_cast<double>(now), kPulsePeriod);
    if (phase > kPulseFold) phase = kPulseFull - phase;
    volatile float value = static_cast<float>(phase * kPulseScale * kHalf + kPulseBase);
    return value;
}
} // namespace

void CObject3DClientSpecific::InvisiblePulse(float frames) {
    float value = Pulse(pulseClock, frames);
    for (int i = 0; i < drawnPartCount; ++i) drawnParts[i]->alpha = value;
}
EI_REPLACE_METHOD(0x0051CC20, CObject3DClientSpecific::InvisiblePulse);

void CObject3DClientSpecific::StunnedPulse(float frames) {
    float value = Pulse(pulseClock, frames);
    for (int i = 0; i < drawnPartCount; ++i) {
        FigurePart* part = drawnParts[i];
        part->tint[0] = value;
        part->tint[1] = 0.0f;
        part->tint[2] = 0.0f;
        part->flags_ |= kPartTinted;
    }
}
EI_REPLACE_METHOD(0x0051CCD0, CObject3DClientSpecific::StunnedPulse);

void CUnitClientSpecific::AdvanceTime(float time) {
    volatile float stored = time - lastTime;   // FST to a float local
    float step = stored;
    if (!(step >= 0.0f)) {   // back in time (or NaN): only put back on the ground
        float position[3] = {unit->positionX, unit->positionY, unit->positionZ};
        position[2] = static_cast<float>(UnitGroundHeightOriginal(unit, nullptr, position[0], position[1]) +
                                         unit->heightOffset);
        static_cast<CWorld*>(ObjectWorld(this))->MoveObject(unit, position);
        return;
    }
    if (step > kLongStep) step = static_cast<float>(IsaacRandom::Global().Next() % kLongStepSpread + 500u);
    lastTime = time;
    if (ObjectSlots(unit)->InvisibleSlot()) CallInvisiblePulse(ClientHalf(unit), nullptr, step);
    if (unit->MapObject()->IsStunned()) CallStunnedPulse(ClientHalf(unit), nullptr, step);
    if (unit->unknown_260 == 0) CallPlaceOnPath(this, nullptr, time, step);
    else unit->animationSpeed = 1.0f;
    if (unit->unknown_25C == kMotionScaled) {
        volatile float scaled = step * unit->animationSpeed;
        step = scaled;
    }
    CFigureAnimationState* state = unit->Object3D()->animation;
    if (state && unit->unknown_25C != kMotionPaused) FigureAdvanceOriginal(state, nullptr, step);
}
EI_REPLACE_METHOD(0x00535B30, CUnitClientSpecific::AdvanceTime);

void CUnitClientSpecific::RestartAnimation(float time) {
    if (unit->flags_ & kFlagFrozen) return;
    CFigureAnimationState* state = unit->Object3D()->animation;
    state->queue.RemoveAll();
    if (unit->unknown_255 == kModeSpecialIdle) {
        ClientStartIdleAnimationOriginal(this, nullptr, 1);
    } else {
        char* first = nullptr;
        char* second = nullptr;
        if (unit->unknown_254 != kNoEntry)
            first = UnitAnimationEntry(unit, nullptr, static_cast<std::uint8_t>(unit->unknown_254));
        if (unit->unknown_255 != kNoEntry)
            second = UnitAnimationEntry(unit, nullptr, static_cast<std::uint8_t>(unit->unknown_255));
        if (first) FigurePlayOriginal(unit->Object3D()->animation, nullptr, first);
        if (second) {
            if (!first) FigurePlayOriginal(unit->Object3D()->animation, nullptr, second);
            else FigureQueueOriginal(unit->Object3D()->animation, nullptr, second);
        }
    }
    double start = unit->unknown_258;
    volatile float stored = static_cast<float>((time + 1.0f) - start);   // FST to the argument's slot
    float step = stored;
    state = unit->Object3D()->animation;
    if (step > 0.0f && state) {
        lastTime = static_cast<float>(start + step);
        FigureAdvanceOriginal(state, nullptr, step);
        state = unit->Object3D()->animation;
        lastAnimationTime = state->time;
        lastAnimation = state->current;
        return;
    }
    lastTime = static_cast<float>(start);
}
EI_REPLACE_METHOD(0x005352B0, CUnitClientSpecific::RestartAnimation);

void CUnitClientSpecific::StartIdleAnimation(std::uint8_t play) {
    if (unit->flags_ & kFlagFrozen) return;
    std::uint32_t key = 0;
    std::uint32_t weapon = unit->activeWeapon < 4 ? weaponIds[unit->activeWeapon] : kNoWeapon;
    if (weapon != kNoWeapon) {
        std::uint32_t prototype = weapon >> 16;
        if (prototype != kNoPrototype) {
            // An index beyond the table reads the type at address 0xC in the original too (a fault).
            const auto* record =
                reinterpret_cast<const itemdb::WeaponRecord*>(itemdb::Weapons().At(prototype, itemdb::kWeaponSize));
            key = 1u << (record->type & 0x1F);
        }
    } else if (unit->Params()->naturalWeapon < kNaturalWeapons) {
        key = 1u << unit->Params()->naturalWeapon;
    }
    if (unit->type_ != kUnitTypeHero) key |= kKeyNotHero;
    if (unit->unknown_255 == kModeSpecialIdle) key |= kKeySpecialIdle;
    char* animation = UnitFindAnimation(unit, nullptr, key, 1);
    if (!animation) return;
    if (play) FigurePlayOriginal(unit->Object3D()->animation, nullptr, animation);
    else FigureQueueOriginal(unit->Object3D()->animation, nullptr, animation);
}
EI_REPLACE_METHOD(0x00535480, CUnitClientSpecific::StartIdleAnimation);
