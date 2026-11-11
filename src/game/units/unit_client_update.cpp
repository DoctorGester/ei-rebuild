// The client controller's tick and motion (UnitClientSpecific, unit_client_models.h): the per-tick update, the
// animation moving through time, the placement on the motion the unit follows, the blink of the party faces and the
// attack figures. See docs/notes/units.md.
#include "game/units/unit_client_models.h"

#include "game/ai/ai_map.h"
#include "game/ai/ai_motion.h"
#include "game/combat/combat.h"   // AttackParams
#include "engine/core/core_transform.h"
#include "engine/scene/drawn_motion.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/app/logic_settings.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/data/text_manager.h"
#include "engine/core/random.h"
#include "game/sound/sound.h"
#include "engine/models/texture.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"
#include "render/render.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {
constexpr std::uint32_t kFlagNotDrawn = 8;            // the unit's flags
constexpr int kDeadShownTicks = 50;                   // after the unit's +0x254
constexpr std::uint32_t kKeyMovement = kAnimationMoveMask;
constexpr std::uint32_t kKeyMovementStill = AnimationKeyOf(AnimationMoveBits::Lie);
constexpr float kFadeStep = 0.06666667f;              // 1/15
constexpr std::int8_t kModeSpecialIdle = -2;          // the unit's +0x255
constexpr int kIdleRestartBelow = 3;                  // FigureAnimationState::queue.count

// The drawn parts take `alpha`.
void SetPartsAlpha(Object3DClientSpecific* half, float alpha) {
    for (Material* part : half->materials) part->colors.diffuse.w = alpha;
}

} // namespace

bool UnitClientSpecific::RunTick() {
    if (unit->pathTimeShift == 1) motion.ShiftTime(1);
    if (unit->motionMode == 1) ++unit->animationStart;
    const FigureAnimation* current = unit->AsObject3D()->animation->current;
    if (current) ShowWeaponParts((current->key & kAnimationVariantBits) == 0);

    bool drawn;
    if (unit->dead && OwningWorld()->tick >
                          static_cast<std::uint8_t>(unit->animationEntry) + kDeadShownTicks) {
        drawn = false;
    } else {
        drawn =
            current && ((current->key & kKeyMovement) != kKeyMovementStill || current->Kind() == AnimationKind::Cross);
    }
    if (drawn) unit->flags &= ~kFlagNotDrawn;
    else unit->flags |= kFlagNotDrawn;

    if (fadeIn < 1.0f) {
        auto* half = unit->AsObject3D()->ClientSpecific();
        for (Material* part : half->materials) part->colors.diffuse.w = fadeIn;
        float grown = fadeIn + kFadeStep;   // stored as a float before the compare
        float next = grown;
        fadeIn = MinKeepA(next, 1.0f);   // the select keeps a NaN
    }
    if (flashCountdown > 0 && --flashCountdown == 0)
        unit->AsObject3D()->ClientSpecific()->SetLit(false);

    if (blinks) {
        if (blinkTimer > 0) {
            --blinkTimer;
        } else {
            blinkTimer = ClientRandBelow(8) + 2;
            if (eyesClosed == 0) BlinkClose(0);
            else BlinkOpen(0);
        }
    }
    UpdateArrow();
    PlayStepSounds();

    if (!speech.empty()) {
        ClientSpeech* first = &speech.front();
        int delay = --first->delay;
        if (delay == 0) {
            if (first->talking) SetTalking(unit->id, true);
            if (first->kind == AckKind::Attack || first->kind == AckKind::DecidedToAttack) {
                if (!g_clientWorld.sounds->IsPlaying(speechSound) && Speak(first)) speechSound = first->sound;
            } else {
                Speak(first);
            }
        } else if (delay < 0 && !g_clientWorld.sounds->IsPlaying(first->sound)) {
            if (speech.front().talking) SetTalking(unit->id, false);
            speech.pop_front();
        }
    }
    if (unit->secondEntry == kModeSpecialIdle && std::ssize(unit->AsObject3D()->animation->queue) < kIdleRestartBelow)
        StartIdleAnimation(0);
    return true;
}

// --- Fade-in, level and wounds slots ----------------------------------------------------------

int UnitClientSpecific::FiguresLoaded() {
    SetPartsAlpha(unit->ClientSpecific(), fadeIn);
    return 1;
}

int UnitClientSpecific::RestartFadeIn() {
    fadeIn = 0.0f;
    SetPartsAlpha(unit->ClientSpecific(), fadeIn);
    return 0;
}

int UnitClientSpecific::Level() {
    float experience = static_cast<float>(static_cast<std::uint32_t>(unit->params.experience));
    float numerator = lib::Log(experience / g_logicSettings.considerModifier);
    float level = numerator / lib::Log(g_logicSettings.considerLogBase);
    // The rounding gives the integer indefinite 0x80000000 for an infinite, NaN or out-of-range value (the
    // CRT's lrint gives 0). Experience 0 reaches it: ln 0 = -inf.
    return lib::RoundEven(level);
}

void UnitClientSpecific::RefreshWounds() {
    if (ComposeWoundsTexture()) {
        char name[0x80];
        std::snprintf(name, sizeof(name), "mo%dw", unit->id);
        unit->ClientSpecific()->ShowWounds(name);
    } else {
        unit->ClientSpecific()->HideWounds();
    }
}

// --- Animation through time --------------------------------------------------------------------------

// The client controller moving a unit's animation through time (UnitClientSpecific's RestartAnimation and
// AdvanceTime, the idle animation). See unit_client_models.h, map_object_shadow.h and docs/notes/units.md.

namespace {
constexpr std::int8_t kNoEntry = -1;                  // Unit::secondEntry
constexpr float kLongStep = 500.0f;
constexpr std::uint32_t kLongStepSpread = 101;        // a long step becomes 500..600
constexpr int kMotionPaused = 1;                      // Unit::motionMode
constexpr int kMotionScaled = 2;
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFFu;
constexpr std::uint32_t kKeyNotHero = AnimationKeyOf(AnimationMoveBits::Walk);
constexpr std::uint32_t kKeySpecialIdle = kAnimationSpecialIdleKey;
} // namespace

void UnitClientSpecific::AdvanceTime(float time) {
    float stored = time - lastTime;
    float step = stored;
    if (step < 0.0f) {   // back in time: only put back on the ground
        Vector3 position = unit->localPosition;
        position.z = unit->GroundHeight(position.x, position.y) + unit->heightOffset;
        OwningWorld()->MoveObject(unit, &position.x);
        return;
    }
    if (step > kLongStep) step = static_cast<float>(g_clientRandom.Next() % kLongStepSpread + 500u);
    lastTime = time;
    if (unit->InvisibleSlot()) unit->ClientSpecific()->InvisiblePulse(step);
    if (unit->AsMapObject()->IsStunned()) unit->ClientSpecific()->StunnedPulse(step);
    if (unit->pathTimeShift == 0) PlaceOnPath(time, step);
    else unit->animationSpeed = 1.0f;
    if (unit->motionMode == kMotionScaled) {
        float scaled = step * unit->animationSpeed;
        step = scaled;
    }
    FigureAnimationState* state = unit->AsObject3D()->animation;
    if (state && unit->motionMode != kMotionPaused) state->Advance(step);
}

void UnitClientSpecific::RestartAnimation(float time) {
    if (unit->flags & MapObjectFlag::NoCasting) return;
    FigureAnimationState* state = unit->AsObject3D()->animation;
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
        if (first) unit->AsObject3D()->animation->Play(first);
        if (second) {
            if (!first) unit->AsObject3D()->animation->Play(second);
            else unit->AsObject3D()->animation->Queue(second);
        }
    }
    int start = unit->animationStart;
    float stored = time + 1.0f - static_cast<float>(start);
    float step = stored;
    state = unit->AsObject3D()->animation;
    if (step > 0.0f && state) {
        lastTime = static_cast<float>(start) + step;
        state->Advance(step);
        state = unit->AsObject3D()->animation;
        lastAnimationTime = state->time;
        lastAnimation = state->current;
        return;
    }
    lastTime = static_cast<float>(start);
}

void UnitClientSpecific::StartIdleAnimation(std::uint8_t play) {
    if (unit->flags & MapObjectFlag::NoCasting) return;
    std::uint32_t key = 0;
    std::uint32_t weapon = unit->activeWeapon < 4 ? weaponIds[unit->activeWeapon] : kNoWeapon;
    if (weapon != kNoWeapon) {
        // No record (a kNoIndex half, an id of another database): no weapon bit.
        if (const auto* record = itemdb::WeaponAt(weapon >> 16))
            key = AnimationWeaponBit(static_cast<WeaponType>(record->type));
    } else if (unit->params.naturalWeapon < kAnimatedWeaponTypes) {
        key = AnimationWeaponBit(static_cast<WeaponType>(unit->params.naturalWeapon));
    }
    if (unit->MapType() != MapObjectType::Hero) key |= kKeyNotHero;
    if (unit->secondEntry == kModeSpecialIdle) key |= kKeySpecialIdle;
    FigureAnimation* animation = unit->FindAnimation(key, 1, AnimationRandom::Client);
    if (!animation) return;
    if (play) unit->AsObject3D()->animation->Play(animation);
    else unit->AsObject3D()->animation->Queue(animation);
}

// --- Placement on the path ---------------------------------------------------------------------------

// A party unit's client controller (UnitClientSpecific, class 0x3D) placing its unit on the
// motion it follows. See docs/notes/units.md ("Client path placement").

namespace {

constexpr float kHalfTurn = 3.1416f;
constexpr float kDecay = -0.3f;
constexpr float kAnimationRate = 15.0f;
constexpr float kFullTurn = /*007AF330*/ 6.2831855f;   // a static initialiser: pi + pi

} // namespace

// Samples the motion at `time`, moves the unit there (on the ground, heightOffset above), snaps its facing
// once when snapHeading is set, then turns it: at once with MapObjectFlag::NoCasting, else blended toward the
// motion's direction with weight exp(-0.3 rate) on the old heading when `rate` > 0; finally animationSpeed
// from the motion's speed and the figure's current animation.
static void PlaceOnPathOf(UnitClientSpecific* self, float time, float rate) {
    Unit* unit = self->unit;
    if (unit->flags & MapObjectFlag::Preview) return;
    World* world = self->OwningWorld();
    MotionSample sample = unit->motion.Sample(world, OrderOf(&self->motion), time);
    Vector3 direction = sample.direction;
    float toWorld = 0.5f;
    Vector3 position = WithZ(toWorld * sample.position, 0.0f);
    position.z = unit->GroundHeight(position.x, position.y) + unit->heightOffset;
    self->OwningWorld()->MoveObject(unit, &position.x);
    if (self->snapHeading) {
        self->snapHeading = 0;
        unit->TurnUpTowards(direction, 0);
    }
    if (unit->flags & MapObjectFlag::NoCasting) {
        unit->TurnUpTowards(direction, 1);
    } else if (rate > 0.0f) {   // only above zero (NaN not)
        Vector3 axis = unit->localRotation.AxisY();
        float current = lib::Atan2(-axis.y, -axis.x);
        float target = lib::Atan2(direction.y, direction.x);
        for (;;) {
            float gap = target - current;
            gap = gap < 0 ? -gap : gap;
            if (!(gap > kHalfTurn)) break;   // (a NaN gap ends the loop too)
            // Bring the smaller angle up by a full turn until they are within half a turn.
            if (target > current)
                current = kFullTurn + current;
            else
                target = kFullTurn + target;
        }
        float weight = lib::Exp(rate * kDecay);
        float heading = (1.0f - weight) * target + weight * current;
        direction.x = lib::Cos(heading);
        direction.y = lib::Sin(heading);
        direction.z = 0.0f;
        unit->SetHeading(direction);
    }

    // The animation the figure plays.
    const FigureAnimation* animation = unit->AsObject3D()->animation->current;
    if (!animation) {
        unit->animationSpeed = 1.0f;
        return;
    }
    float length;
    length = animation->stride;
    if (length == 0.0f) {
        unit->animationSpeed = unit->ActionSpeed(animation);
        return;
    }
    float frames = animation->database->TimeFactor(unit->scale.z);
    float moved = std::sqrt(LengthSquared(direction));
    float numerator = moved * sample.speed * kAnimationRate;
    float denominator = 2.0f * (frames * length);
    unit->animationSpeed = numerator / denominator;
}

void UnitClientSpecific::PlaceOnPath(float time, float rate) { PlaceOnPathOf(this, time, rate); }

void UnitClientSpecific::MotionChanged() {
    drawn::NotePathChanged(unit);
    PlaceOnPath(lastTime, 0.0f);
}

// --- The blink of the faces --------------------------------------------------------------------------

// The blink of the party units' faces (UnitClientSpecific, unit_client_models.h): the face
// texture ("<model>mouth") holds the open and the closed state, which is copied over a
// rectangle of the unit's skin texture with DirectDraw blits down the mip chain.
// No texture has a DirectDraw surface (surface.h; the skins are decoded by the new renderer):
// the new renderer draws the face's rectangle over the skin's in its shader instead
// (render::TexturePatch, docs/notes/units.md); the skin's texture is not changed.

namespace {

constexpr float kEyeUnit = 0.00390625f;    // 1/256 of the skin's width
constexpr int kSkinUnits = 0x100;

// The skin texture: the first texture of the unit's client half (its material), or null.
Texture* SkinOf(Unit* unit) {
    auto* half = unit->AsObject3D()->ClientSpecific();
    if (!half->partSets[0]) return nullptr;
    return half->partSets[0]->texture;
}

// The eye rectangle's numbers shared by both: the skin's scale and the face's offset `t` (in mixed units).
struct EyeFrame {
    float scale = 0.0f;
    float offset = 0.0f;
    int at[2] = {};
};

EyeFrame Frame(const Texture* skin, const Texture* face, const UnitModelRecord* model) {
    EyeFrame frame;
    frame.scale = static_cast<float>(skin->width) * kEyeUnit;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    frame.offset = (static_cast<float>(face->height) / frame.scale - static_cast<float>(eyeHeight + eyeHeight)) / frame.scale;
    frame.at[0] = lib::Truncate(static_cast<float>(static_cast<int>(model->eyeLeft)) * frame.scale);
    int fromBottom = kSkinUnits - static_cast<int>(model->eyeBottom) - eyeHeight;
    frame.at[1] = lib::Truncate(static_cast<float>(fromBottom) * frame.scale);
    return frame;
}

} // namespace

// The game blitted `from` of `source` to the rectangle of its size at `at` of `target`, then on each next level of
// both mip chains with each of the eight edges halved (SAR), until either had no further level. Ours hands the
// first level's rectangles to the renderer, which shows them over the skin in its shader on every level.
void BlitMipChain(Texture* target, const int* at, Texture* source, const Rect* from) {
    const int to[4] = {at[0], at[1], at[0] + (from->Width()), at[1] + (from->Height())};
    const int rectangle[4] = {from->left, from->top, from->right, from->bottom};
    render::TexturePatch(target->surface, to, source->surface, rectangle);
}

void UnitClientSpecific::BlinkClose(bool force) {
    if (!faceTexture) return;
    if (!force && eyesClosed == 1) return;
    Texture* skin = SkinOf(unit);
    if (!skin) return;
    auto* model = unit->GetPrototypeModel();
    Texture* face = faceTexture;
    EyeFrame frame = Frame(skin, face, model);
    float scale = frame.scale;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    Rect closed = {0, lib::Truncate(frame.offset), lib::Truncate(static_cast<float>(static_cast<int>(model->eyeWidth)) * scale),
                   lib::Truncate((static_cast<float>(eyeHeight) + frame.offset) * scale)};
    BlitMipChain(skin, frame.at, face, &closed);
    eyesClosed = 1;
}

void UnitClientSpecific::BlinkOpen(bool force) {
    if (!faceTexture) return;
    if (!force && eyesClosed == 0) return;
    Texture* skin = SkinOf(unit);
    if (!skin) return;
    auto* model = unit->GetPrototypeModel();
    Texture* face = faceTexture;
    EyeFrame frame = Frame(skin, face, model);
    float scale = frame.scale;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    Rect open = {0, lib::Truncate((static_cast<float>(eyeHeight) + frame.offset) * scale),
                 lib::Truncate(static_cast<float>(static_cast<int>(model->eyeWidth)) * scale),
                 lib::Truncate((static_cast<float>(eyeHeight + eyeHeight) + frame.offset) * scale)};
    BlitMipChain(skin, frame.at, face, &open);
    eyesClosed = 0;
}

// --- Attack figures ----------------------------------------------------------------------------------

// The client's attack figures of a party unit (UnitClientSpecific::GetAttackParams) and the copy of the
// damage scale it uses. See unit_client_models.h and docs/notes/units.md ("Client controller").

namespace {
constexpr int kDamageTypes = 7;

// The weapon id of the controller's slot `slot`, or none for a slot beyond 3.
std::uint32_t WeaponIn(const UnitClientSpecific* self, std::uint32_t slot) {
    return slot < 4 ? self->weaponIds[slot] : kNoWeapon;
}
} // namespace

void UnitClientSpecific::GetAttackParams(AttackParams* out) {
    Unit* ownerObject = unit;
    UnitParams* params = &ownerObject->params;
    int keyed = IsTextKey(ownerObject->id);
    std::uint32_t weapon = WeaponIn(this, ownerObject->activeWeapon);
    if (keyed && weapon != kNoWeapon) {
        out->damageMin = WeaponMinDamage(weapon);
        out->damageMax = WeaponMaxDamage(weapon);
        out->weaponType = WeaponTypeOf(weapon);
        out->damageType = WeaponDamageType(weapon);
        out->speed = WeaponWeight(weapon);
        const itemdb::WeaponRecord* record = itemdb::WeaponAt(PrototypeIndex(weapon));
        // The bonus is stored to a float before the weapon's figure is added; the sum truncates.
        float bonus = params->WeaponSkillBonus(out->weaponType, record);
        out->attack = static_cast<int>(WeaponAttackBonus(weapon) + bonus);
        bonus = params->WeaponSkillBonus(out->weaponType, record);
        out->defence = static_cast<int>(WeaponDefenceBonus(weapon) + bonus);
    } else {
        // The packed parameters (bytes).
        out->damageMin = params->damage;
        out->damageMax = static_cast<float>(params->damageSpread + params->damage);
        out->attack = params->combat[1];
        out->defence = params->combat[3];
        out->speed = params->combat[4];
        weapon = WeaponIn(this, ownerObject->activeWeapon);
        if (weapon != kNoWeapon) {
            out->weaponType = WeaponTypeOf(weapon);
            out->damageType = WeaponDamageType(weapon);
        } else {
            out->weaponType = WeaponType::NoWeapon;
            auto* model = ownerObject->GetPrototypeModel();
            // The first damage type whose weight is not zero, none when every weight is zero.
            out->damageType = kNoDamageType;
            for (int i = 0; i < kDamageTypes; ++i) {
                float weight = model->damageWeights[i];
                if (weight != 0.0f) {
                    out->damageType = i;
                    break;
                }
            }
        }
    }
    if (IsRanged(out->weaponType)) out->defence = 0;
    out->damageMax = ScaleDamageByEffects(ownerObject, out->damageMax);   // (not read)
    out->damageMin = ScaleDamageByEffects(ownerObject, out->damageMin);
    out->attackSpread = params->combat[2];
    out->ScaleForDifficulty(keyed);
}
