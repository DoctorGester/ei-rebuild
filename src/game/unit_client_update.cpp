// The client controller's tick and motion (UnitClientSpecific, unit_client_models.h): the per-tick update, the
// animation moving through time, the placement on the motion the unit follows, the blink of the party faces and the
// attack figures. See docs/notes/units.md.
#include "unit_client_models.h"

#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"   // AttackParams
#include "core_transform.h"
#include "game_singletons.h"
#include "item.h"
#include "item_db.h"
#include "logic_settings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "object_tables.h"
#include "random.h"
#include "sound.h"
#include "texture.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"
#include "render/render.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
constexpr std::uint32_t kFlagNotDrawn = 8;            // the unit's flags
constexpr int kDeadShownTicks = 50;                   // after the unit's +0x254
constexpr std::uint32_t kKeyMovement = kAnimationMoveMask;
constexpr std::uint32_t kKeyMovementStill = AnimationKeyOf(AnimationMoveBits::Mode0);
constexpr float kFadeStep = 0.06666667f;              // 1/15
constexpr std::int8_t kModeSpecialIdle = -2;          // the unit's +0x255
constexpr int kIdleRestartBelow = 3;                  // FigureAnimationState::queue.count

// Is sound `handle` still playing or waiting to play?
bool SoundBusy(int handle) {
    if (handle == -1) return false;
    SoundManager* sounds = g_clientWorld.sounds;
    return sounds->FindPlaying(handle) || sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

void SetTalking(const Unit* unit, bool talking) {
    EngineObject* serverUnit = g_serverWorld.FindUnit(unit->id);
    if (!serverUnit) return;
    if (talking) serverUnit->flags |= MapObjectFlag::Blocked;
    else serverUnit->flags &= ~MapObjectFlag::Blocked;
}

// The drawn parts take `alpha`.
void SetPartsAlpha(Object3DClientSpecific* half, float alpha) {
    for (Material* part = half->FirstMaterial(); part; part = half->NextMaterial()) part->colors.diffuse.w = alpha;
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

    EI_NAN_AUDIT(fadeIn);
    if (fadeIn < 1.0f) {
        auto* half = unit->AsObject3D()->ClientSpecific();
        for (Material* part = half->FirstMaterial(); part; part = half->NextMaterial()) part->colors.diffuse.w = fadeIn;
        float grown = fadeIn + kFadeStep;   // stored as a float before the compare
        float next = grown;
        fadeIn = MinKeepA(next, 1.0f);   // the select keeps a NaN
    }
    if (flashCountdown > 0 && --flashCountdown == 0)
        unit->AsObject3D()->ClientSpecific()->SetMaterialsLit(false);

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
            if (first->talking) SetTalking(unit, true);
            if (first->kind == AckKind::Attack || first->kind == AckKind::DecidedToAttack) {
                if (!SoundBusy(speechSound) && Speak(first)) speechSound = first->sound;
            } else {
                Speak(first);
            }
        } else if (delay < 0 && !SoundBusy(first->sound)) {
            if (speech.front().talking) SetTalking(unit, false);
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
    double experience = static_cast<double>(static_cast<std::uint32_t>(unit->params.experience));
    // The first logarithm is kept through the second one's call as a double (hi + lo: the 53-bit rounding).
    pc24::Unrounded numerator = pc24::Log(pc24::Div(experience, g_logicSettings.considerModifier));
    float level = pc24::Div(numerator.hi + numerator.lo, pc24::Log(g_logicSettings.considerLogBase));
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
constexpr std::uint32_t kFlagFrozen = 0x10000000;     // the unit's flags
constexpr std::int8_t kNoEntry = -1;                  // Unit::secondEntry
constexpr float kLongStep = 500.0f;
constexpr std::uint32_t kLongStepSpread = 101;        // a long step becomes 500..600
constexpr int kMotionPaused = 1;                      // Unit::motionMode
constexpr int kMotionScaled = 2;
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFFu;
constexpr std::uint32_t kNoPrototype = 0xFFFF;
constexpr std::uint8_t kNaturalWeapons = 7;
constexpr std::uint32_t kKeyNotHero = AnimationKeyOf(AnimationMoveBits::Mode2);
constexpr std::uint32_t kKeySpecialIdle = kAnimationSpecialIdleKey;
} // namespace

void UnitClientSpecific::AdvanceTime(float time) {
    float stored = time - lastTime;   // stored as a float
    float step = stored;
    EI_NAN_AUDIT(step);
    if (step < 0.0f) {   // back in time: only put back on the ground
        Vector3 position = {unit->localPosition.x, unit->localPosition.y, unit->localPosition.z};
        position.z = pc24::Add(unit->GroundHeight(position.x, position.y), unit->heightOffset);
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
    if (unit->flags & kFlagFrozen) return;
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
    float stored = pc24::Sub(time + 1.0f, static_cast<double>(start));   // stored as a float
    float step = stored;
    state = unit->AsObject3D()->animation;
    if (step > 0.0f && state) {
        lastTime = pc24::Add(static_cast<double>(start), step);
        state->Advance(step);
        state = unit->AsObject3D()->animation;
        lastAnimationTime = state->time;
        lastAnimation = state->current;
        return;
    }
    lastTime = static_cast<float>(start);
}

void UnitClientSpecific::StartIdleAnimation(std::uint8_t play) {
    if (unit->flags & kFlagFrozen) return;
    std::uint32_t key = 0;
    std::uint32_t weapon = unit->activeWeapon < 4 ? weaponIds[unit->activeWeapon] : kNoWeapon;
    if (weapon != kNoWeapon) {
        std::uint32_t prototype = weapon >> 16;
        if (prototype != kNoPrototype) {
            // An index beyond the table reads the type at address 0xC (a fault).
            const auto* record =
                itemdb::WeaponAt(prototype);
            key = AnimationWeaponBit(record->type);
        }
    } else if (unit->params.naturalWeapon < kNaturalWeapons) {
        key = AnimationWeaponBit(unit->params.naturalWeapon);
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
// Float maths as the main thread's 24-bit x87 rounds it: atan2, exp, cos and sin through lib/pc24.h
// (FPATAN, VC6's inline exp, FCOS, FSIN).

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
    float toCells = 2.0f;
    Vector3 position = {unit->localPosition.x, unit->localPosition.y, unit->localPosition.z};
    position.x = toCells * position.x;
    position.y = toCells * position.y;
    Vector3 direction;
    float speed;
    int cellX, cellY;
    bool turning;
    World* world = self->OwningWorld();
    unit->motion.Sample(world, OrderOf(&self->motion), time, &turning, &position.x,
                           &direction.x, &speed, &cellX, &cellY);
    float toWorld = 0.5f;
    position.x = toWorld * position.x;
    position.y = toWorld * position.y;
    position.z = pc24::Add(unit->GroundHeight(position.x, position.y), unit->heightOffset);
    self->OwningWorld()->MoveObject(unit, &position.x);
    if (self->snapHeading) {
        self->snapHeading = 0;
        unit->TurnUpTowards(direction, 0);
    }
    if (unit->flags & MapObjectFlag::NoCasting) {
        unit->TurnUpTowards(direction, 1);
    } else if (rate > 0.0f) {   // only above zero (NaN not)
        Vector3 axis = unit->localRotation.AxisY();
        // Both angles are stored as floats. Left unrounded they shift the heading by a float ULP in about one
        // call in ten, and at a heading of +-pi (where jumps from one quaternion sign to the other) that
        // flipped the orientation's sign once in the ui_hud run.
        float current = pc24::Round(pc24::Atan2(-axis.y, -axis.x));
        float target = pc24::Round(pc24::Atan2(direction.y, direction.x));
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
        float weight = pc24::Exp(rate * kDecay);
        float heading = (1.0f - weight) * target + weight * current;
        direction.x = pc24::Round(pc24::Cos(heading));
        direction.y = pc24::Round(pc24::Sin(heading));
        direction.z = 0.0f;
        unit->SetHeading(&direction.x);
    }

    // The animation the figure plays.
    const FigureAnimation* animation = unit->AsObject3D()->animation->current;
    if (!animation) {
        unit->animationSpeed = 1.0f;
        return;
    }
    float length;
    length = animation->stride;
    EI_NAN_AUDIT(length);
    if (length == 0.0f) {
        unit->animationSpeed = static_cast<float>(unit->ActionSpeed(animation));
        return;
    }
    // The database the animation is in: its index back to the first one.
    int index;
    std::memcpy(&index, &animation->index, sizeof(index));
    const auto* database = reinterpret_cast<const AnimationDatabase*>(
        reinterpret_cast<const char*>(animation - index) - offsetof(AnimationDatabase, animations));
    float frames = 1.0f;
    float span = database->length;
    EI_NAN_AUDIT(span);
    if (span != 0.0f)
        frames = ((database->end - database->start) * unit->scale.z + database->start) / span;
    float moved = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(direction.x, direction.y, direction.z));
    float numerator = moved * speed * kAnimationRate;
    float denominator = 2.0f * (frames * length);
    unit->animationSpeed = numerator / denominator;
}

void UnitClientSpecific::PlaceOnPath(float time, float rate) { PlaceOnPathOf(this, time, rate); }

void UnitClientSpecific::MotionChanged() { PlaceOnPath(lastTime, 0.0f); }

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

// The eye rectangle's numbers shared by both: the skin's scale and the face's offset `t` (in mixed units). The
// data's ints enter the operations exactly (pc24's helpers round the result once).
struct EyeFrame {
    float scale;
    float offset;
    int at[2];
};

EyeFrame Frame(const Texture* skin, const Texture* face, const UnitModelRecord* model) {
    EyeFrame frame;
    frame.scale = static_cast<float>(static_cast<double>(skin->width) * kEyeUnit);
    int eyeHeight = static_cast<int>(model->eyeHeight);
    frame.offset = pc24::Sub(pc24::Div(static_cast<double>(face->height), frame.scale),
                             static_cast<double>(eyeHeight + eyeHeight)) / frame.scale;
    frame.at[0] = lib::Truncate(pc24::Mul(static_cast<double>(static_cast<int>(model->eyeLeft)), frame.scale));
    int fromBottom = kSkinUnits - static_cast<int>(model->eyeBottom) - eyeHeight;
    frame.at[1] = lib::Truncate(pc24::Mul(static_cast<double>(fromBottom), frame.scale));
    return frame;
}

} // namespace

// The game blitted `from` of `source` to the rectangle of its size at `at` of `target`, then on each next level of
// both mip chains with each of the eight edges halved (SAR), until either had no further level. Ours hands the
// first level's rectangles to the renderer, which shows them over the skin in its shader on every level.
void BlitMipChain(Texture* target, const int* at, Texture* source, const Rect* from) {
    const int to[4] = {at[0], at[1], at[0] + (from->right - from->left), at[1] + (from->bottom - from->top)};
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
    Rect closed = {0, lib::Truncate(frame.offset), lib::Truncate(pc24::Mul(static_cast<double>(static_cast<int>(model->eyeWidth)), scale)),
                   lib::Truncate(pc24::Add(static_cast<double>(eyeHeight), frame.offset) * scale)};
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
    Rect open = {0, lib::Truncate(pc24::Add(static_cast<double>(eyeHeight), frame.offset) * scale),
                 lib::Truncate(pc24::Mul(static_cast<double>(static_cast<int>(model->eyeWidth)), scale)),
                 lib::Truncate(pc24::Add(static_cast<double>(eyeHeight + eyeHeight), frame.offset) * scale)};
    BlitMipChain(skin, frame.at, face, &open);
    eyesClosed = 0;
}

// --- Attack figures ----------------------------------------------------------------------------------

// The client's attack figures of a party unit (UnitClientSpecific::GetAttackParams) and the copy of the
// damage scale it uses. See unit_client_models.h and docs/notes/units.md ("Rewritten").

namespace {
constexpr int kTypeNoWeapon = 8;    // AttackParams::weaponType without a weapon in hand
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
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
        out->weaponType = WeaponType(weapon);
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
            out->weaponType = WeaponType(weapon);
            out->damageType = WeaponDamageType(weapon);
        } else {
            out->weaponType = kTypeNoWeapon;
            auto* model = ownerObject->GetPrototypeModel();
            // The first damage type whose weight is not zero; left as it was when there is none.
            for (int i = 0; i < kDamageTypes; ++i) {
                float weight = model->damageWeights[i];
                EI_NAN_AUDIT(weight);
                if (weight != 0.0f) {
                    out->damageType = i;
                    break;
                }
            }
        }
    }
    if (out->weaponType == kWeaponBow || out->weaponType == kWeaponCrossbow) out->defence = 0;
    out->damageMax = ScaleDamageByEffects(ownerObject, out->damageMax);   // (not read)
    out->damageMin = ScaleDamageByEffects(ownerObject, out->damageMin);
    out->attackSpread = params->combat[2];
    out->ScaleForDifficulty(keyed);
}
