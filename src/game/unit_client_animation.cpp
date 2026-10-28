// The client controller moving a unit's animation through time (CUnitClientSpecific's RestartAnimation and
// AdvanceTime, the idle animation) and the client half's pulses of invisible and stunned units. See
// unit_client_models.h, map_object_shadow.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "item_db.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "random.h"
#include "unit.h"
#include "world.h"

#include "lib/nan_audit.h"

#include <cmath>

namespace {
constexpr std::uint32_t kFlagFrozen = 0x10000000;     // the unit's flags_
constexpr std::int8_t kModeSpecialIdle = -2;          // CUnit::secondEntry
constexpr std::int8_t kNoEntry = -1;                  // CUnit::secondEntry
constexpr float kLongStep = 500.0f;
constexpr std::uint32_t kLongStepSpread = 101;        // a long step becomes 500..600
constexpr int kMotionPaused = 1;                      // CUnit::motionMode
constexpr int kMotionScaled = 2;
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFFu;
constexpr std::uint32_t kNoPrototype = 0xFFFF;
constexpr std::uint8_t kNaturalWeapons = 7;
constexpr std::uint32_t kKeyNotHero = 0x10000;
constexpr std::uint32_t kKeySpecialIdle = 0x80580000;
constexpr double kPulsePeriod = 30.0;
constexpr float kPulseFold = 15.0f;
constexpr float kPulseFull = 30.0f;
constexpr float kPulseScale = 0.06666667f;            // 1/15
constexpr float kPulseBase = 0.2f;
constexpr std::uint32_t kPartTinted = 0x10;           // CFigureMesh::flags_

// The pulse's value after the clock moved on by `frames`.
float Pulse(float& clock, float frames) {
    float now = frames + clock;   // stored as a float, and to the clock
    clock = now;
    double phase = std::fmod(static_cast<double>(now), kPulsePeriod);
    if (phase > kPulseFold) phase = kPulseFull - phase;
    float value = static_cast<float>(phase * kPulseScale * 0.5f + kPulseBase);
    return value;
}
} // namespace

void CObject3DClientSpecific::InvisiblePulse(float frames) {
    float value = Pulse(pulseClock, frames);
    for (int i = 0; i < std::ssize(materials); ++i) materials[i]->colors.diffuse.w = value;
}

void CObject3DClientSpecific::StunnedPulse(float frames) {
    float value = Pulse(pulseClock, frames);
    for (int i = 0; i < std::ssize(materials); ++i) {
        CMaterial* part = materials[i];
        part->colors.specular.x = value;
        part->colors.specular.y = 0.0f;
        part->colors.specular.z = 0.0f;
        part->flags_ |= kPartTinted;
    }
}

void CUnitClientSpecific::AdvanceTime(float time) {
    float stored = time - lastTime;   // stored as a float
    float step = stored;
    EI_NAN_AUDIT(step);
    if (step < 0.0f) {   // back in time: only put back on the ground
        Vector3 position = {unit->localPosition_.x, unit->localPosition_.y, unit->localPosition_.z};
        position.z = static_cast<float>(unit->GroundHeight(position.x, position.y) +
                                        unit->heightOffset);
        ObjectWorld(this)->MoveObject(unit, &position.x);
        return;
    }
    if (step > kLongStep) step = static_cast<float>(g_random.Next() % kLongStepSpread + 500u);
    lastTime = time;
    if (unit->InvisibleSlot()) unit->ClientSpecific()->InvisiblePulse(step);
    if (unit->MapObject()->IsStunned()) unit->ClientSpecific()->StunnedPulse(step);
    if (unit->pathTimeShift == 0) PlaceOnPath(time, step);
    else unit->animationSpeed = 1.0f;
    if (unit->motionMode == kMotionScaled) {
        float scaled = step * unit->animationSpeed;
        step = scaled;
    }
    CFigureAnimationState* state = unit->Object3D()->animation;
    if (state && unit->motionMode != kMotionPaused) state->Advance(step);
}

void CUnitClientSpecific::RestartAnimation(float time) {
    if (unit->flags_ & kFlagFrozen) return;
    CFigureAnimationState* state = unit->Object3D()->animation;
    state->queue.clear();
    if (unit->secondEntry == kModeSpecialIdle) {
        StartIdleAnimation(1);
    } else {
        FigureAnimation* first = nullptr;
        FigureAnimation* second = nullptr;
        if (unit->animationEntry != kNoEntry)
            first = unit->AnimationEntry(static_cast<std::uint8_t>(unit->animationEntry));
        if (unit->secondEntry != kNoEntry)
            second = unit->AnimationEntry(static_cast<std::uint8_t>(unit->secondEntry));
        if (first) unit->Object3D()->animation->Play(first);
        if (second) {
            if (!first) unit->Object3D()->animation->Play(second);
            else unit->Object3D()->animation->Queue(second);
        }
    }
    double start = unit->animationStart;
    float stored = static_cast<float>((time + 1.0f) - start);   // stored as a float
    float step = stored;
    state = unit->Object3D()->animation;
    if (step > 0.0f && state) {
        lastTime = static_cast<float>(start + step);
        state->Advance(step);
        state = unit->Object3D()->animation;
        lastAnimationTime = state->time;
        lastAnimation = state->current;
        return;
    }
    lastTime = static_cast<float>(start);
}

void CUnitClientSpecific::StartIdleAnimation(std::uint8_t play) {
    if (unit->flags_ & kFlagFrozen) return;
    std::uint32_t key = 0;
    std::uint32_t weapon = unit->activeWeapon < 4 ? weaponIds[unit->activeWeapon] : kNoWeapon;
    if (weapon != kNoWeapon) {
        std::uint32_t prototype = weapon >> 16;
        if (prototype != kNoPrototype) {
            // An index beyond the table reads the type at address 0xC (a fault).
            const auto* record =
                itemdb::WeaponAt(prototype);
            key = 1u << (record->type & 0x1F);
        }
    } else if (unit->params.naturalWeapon < kNaturalWeapons) {
        key = 1u << unit->params.naturalWeapon;
    }
    if (unit->type_ != kUnitTypeHero) key |= kKeyNotHero;
    if (unit->secondEntry == kModeSpecialIdle) key |= kKeySpecialIdle;
    FigureAnimation* animation = unit->FindAnimation(key, 1);
    if (!animation) return;
    if (play) unit->Object3D()->animation->Play(animation);
    else unit->Object3D()->animation->Queue(animation);
}
