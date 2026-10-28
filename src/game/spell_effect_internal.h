#pragma once

// Shared by the spell effects' start and update: helpers, the x87 sequences repeated inline, and small
// accessors. See docs/notes/items_spells.md.

#include "spell.h"
#include "world_data.h"

#include "ai_map.h"
#include "combat.h"
#include "console.h"
#include "lib/crt.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include "lib/static_object.h"
#include "map_object.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <list>

namespace spellfx {

// --- Classes and slots ---------------------------------------------------------------------------

constexpr int kClassSound = 0x48;
constexpr int kClassParticle = 0x4A;
constexpr int kClassPointLight = 0x4B;
constexpr int kClassLightning = 0x4D;
constexpr int kClassArrow = 0x4E;
constexpr int kClassWall = 0x5A;
constexpr int kClassShowVisibility = 0x5E;
constexpr int kClassSoundSequence = 99;
constexpr int kClassUnit = 0x50;

constexpr double (CWorld::*kGridHeight)(float, float) = &CWorld::GridHeightAt;
constexpr double (CWorld::*kRawSurfaceHeight)(float, float) = &CWorld::RawSurfaceHeightAt;

constexpr std::uint32_t kPointLightFlags = 0x840;

// Volumes and radii the casts pass as immediates.
constexpr float kVolume = 10.0f;
constexpr float kHearing = 50.0f;
constexpr float kLoud = 1000.0f;

// Constants read from memory (Rebirth may patch data, so they are read, not folded).
inline float ConstAt(const float& constant) { return *reinterpret_cast<const volatile float*>(&constant); }
inline const float* ConstPtr(const float& constant) { return &constant; }
constexpr float kDefaultSpeed = 5.0f;                   // immediate in the fireball case
constexpr float kHalfBits = 0.5f;                       // immediate pushed to the footprint builder

// Footprints of the walls and clouds, along a direction or a disc, then stamped into / cleared from the AI
// map.
extern lib::StaticObject<FootprintCellSet> g_spellFootprint;

// --- x87 sequences ------------------------------------------------------------------------------
// Plain C++ in the x87 order: double intermediates are the unrounded register values.

// The spell's power curve after the ratio: pow(min(max(ratio, 1), 8), 0.33) - 0.2, written by the
// original as FYL2X / F2XM1 / FSCALE (x87::flog, x87::fexp; the logarithm is used unrounded). The
// two clamps select the float bits on C0 (1 < r or unordered; r < 8 or unordered), the first one
// stored and reloaded (the reload is a plain copy here).
inline double ClampPow(float ratio) {
    std::uint32_t bits;
    bits = std::bit_cast<std::uint32_t>(ratio);
    const std::uint32_t lowBits = ratio <= 1.0f ? 0x3F800000u : bits;
    float low;
    low = std::bit_cast<float>(lowBits);
    const float clamped = low >= 8.0f ? 8.0f : low;
    return x87::fexp(x87::flog(clamped) * 0.33000001311302185) - 0.20000000298023224;
}

// The spell's power factor(repeated inline about twenty times in the game): 1 when the prototype's effect
// compares equal to 0, else ClampPow of damage / effect rounded to float.
inline double PowerFactor(const float* damage, const float* effect) {
    EI_NAN_AUDIT(*effect);
    if (*effect == 0.0f) return 1.0f;
    return ClampPow(static_cast<float>(static_cast<double>(*damage) / *effect));
}

// The fireball explosion's size curve: pow(clamp((r * r * pi) / area, 1, 8), 0.33) - 0.2 with the
// ratio rounded to float, the result rounded to float into *out.
inline void AreaCurve(const float* radius, const float* area, float* out) {
    const float ratio = static_cast<float>(static_cast<double>(*radius) * *radius * 3.1415927f / *area);
    *out = static_cast<float>(ClampPow(ratio));
}

// *out = (float)((*a / *b) * *c), the quotient unrounded.
inline void DivideMultiply(const float* a, const float* b, const float* c, float* out) {
    *out = static_cast<float>(static_cast<double>(*a) / *b * *c);
}

// The fireworks' fright level: _ftol(((float)timer / duration) * 250 + 50),
// unrounded until the game's _ftol truncates it.
inline int FrightLevel(const int* timer, const int* duration) {
    return static_cast<int>(libcrt::Ftol(static_cast<double>(*timer) / *duration * 250.0f + 50.0f));
}

// *out = (float)(*a * *b + *c), unrounded in between.
inline void MultiplyAdd(const float* a, const float* b, const float* c, float* out) {
    *out = static_cast<float>(static_cast<double>(*a) * *b + *c);
}

// The game's _ftol of a float constant in memory.
inline int FtolOf(const float* value) { return static_cast<int>(libcrt::Ftol(*value)); }

// The factor rounded to float.
inline void PowerToFloat(const float* damage, const float* effect, float* out) {
    *out = static_cast<float>(PowerFactor(damage, effect));
}

// The factor times *scale (unrounded product), rounded to float.
inline void PowerScaledToFloat(const float* damage, const float* effect, const float* scale, float* out) {
    *out = static_cast<float>(PowerFactor(damage, effect) * *scale);
}

// The factor times *scale, truncated by the game's _ftol.
inline int PowerScaledToInt(const float* damage, const float* effect, const float* scale) {
    return static_cast<int>(libcrt::Ftol(PowerFactor(damage, effect) * *scale));
}

// A world virtual `height` (kGridHeight, kRawSurfaceHeight) at (x, y): a ground height left unrounded.
inline double WorldHeight(CWorld* world, double (CWorld::*height)(float, float), float x, float y) {
    return (world->*height)(x, y);
}

// *out = (float)(height + *add).
inline void HeightPlus(CWorld* world, double (CWorld::*height)(float, float), float x, float y, const float* add, float* out) {
    *out = static_cast<float>(WorldHeight(world, height, x, y) + *add);
}

// The same with height - *sub.
inline void HeightMinus(CWorld* world, double (CWorld::*height)(float, float), float x, float y, const float* sub, float* out) {
    *out = static_cast<float>(WorldHeight(world, height, x, y) - *sub);
}

// The lightning's end: *out = (float)(height + *a * *b).
inline void HeightPlusProduct(CWorld* world, double (CWorld::*height)(float, float), float x, float y, const float* a, const float* b,
                              float* out) {
    *out = static_cast<float>(WorldHeight(world, height, x, y) + static_cast<double>(*a) * *b);
}

// *out = (float)sqrt(y*y + x*x).
inline void PlanarLength(const Vector3* v, float* out) {
    *out = static_cast<float>(std::sqrt(LengthSquared2DUnrounded<double>(v->y, v->x)));
}

// The fireball's velocity: v *= (speed * 1/15) / sqrt((y*y + x*x) + z*z), the factor unrounded,
// each component rounded to float.
inline void ScaleToSpeed(Vector3* v, const float* speed) {
    const double length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(*v));
    const double k = static_cast<double>(*speed) * 0.06666667f / length;
    v->x = static_cast<float>(k * v->x);
    v->y = static_cast<float>(k * v->y);
    v->z = static_cast<float>(k * v->z);
}

// (float)((distance / speed) * 15) rounded to nearest: the missile's flight time in ticks.
inline int FlightTicks(const float* distance, const float* speed) {
    return x87::fistp(static_cast<float>(static_cast<double>(*distance) / *speed * 15.0f));
}

// The walls' extent: the normal (-d.y, d.x, 0) in the ground plane scaled to length `radius`: k =
// (float)(radius / sqrt(n.y*n.y + n.x*n.x)), then each component (float)(component * k). The normal is
// written to `out` first (d.x copied as bits) and read back.
inline void WallSpan(const Vector3* direction, const float* radius, Vector3* out) {
    const double nx = -static_cast<double>(direction->y);
    out->x = static_cast<float>(nx);
    out->y = direction->x;
    out->z = 0.0f;
    const double length = std::sqrt(LengthSquared2DUnrounded<double>(out->y, nx));
    const float k = static_cast<float>(*radius / length);
    out->x = static_cast<float>(k * nx);
    out->y = static_cast<float>(static_cast<double>(out->y) * k);
    out->z = static_cast<float>(static_cast<double>(out->z) * k);
}

// The light wall's half extent from its lightning's ends: x keeps the difference unrounded before
// halving, y and z are rounded first.
inline void HalfSpan(const CEffectLightning* bolt, Vector3* out) {
    const double half = 0.5;
    const double dx = static_cast<double>(bolt->to_.x) - bolt->from_.x;
    out->y = static_cast<float>(static_cast<double>(bolt->to_.y) - bolt->from_.y);
    out->z = static_cast<float>(static_cast<double>(bolt->to_.z) - bolt->from_.z);
    out->x = static_cast<float>(dx * half);
    out->y = static_cast<float>(static_cast<double>(out->y) * half);
    out->z = static_cast<float>(static_cast<double>(out->z) * half);
}

// --- Small helpers ------------------------------------------------------------------------------

inline std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    u = std::bit_cast<std::uint32_t>(f);
    return u;
}

inline bool IsUnit(CEngineObject* object) { return object && object->GetClassId() == kClassUnit; }
// Where the object sees from into `scratch`.
inline Vector3* PositionOf(CMapObject* object, Vector3* scratch) { return object->EyePoint(scratch); }
// A unit's server half (CUnit::controller).
inline CUnitServer* ControllerOf(CUnit* unit) { return unit->ServerController(); }
// The target as a unit (the callers checked its class, or the game does not).
inline CUnit* TargetUnit(CSpellEffect* e) { return static_cast<CUnit*>(e->target.object); }

// From the world's timed object manager.
template <class T>
T* CreateObject(CSpellEffect* e, int classId) {
    return e->world->TimedObjectManager()->CreateObjectAs<T>(classId, -1);
}

// The counted-pointer assignment the helpers repeat: the old object leaves the world's timed
// objects and is released, the new one is counted.
template <class T>
void Assign(CSpellEffect* e, T** slot, T* object) {
    if (*slot) e->world->RemoveTimedObject((*slot)->id_);
    T* old = *slot;
    *slot = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

inline void SetPosition(CTransformObject* object, const Vector3* position) {
    object->localPosition_ = *position;
}

// The fields of the spell prototype the casts read.
inline const itemdb::SpellRecord* Prototype(CSpellEffect* e) { return e->Spell()->GetPrototype(); }
inline Vector3 PrototypeColor(CSpellEffect* e) {
    Vector3 color;
    std::memcpy(&color, &Prototype(e)->extraValues[0], 12);
    return color;
}
inline float PrototypeRange(CSpellEffect* e) {
    float range;
    range = std::bit_cast<float>(Prototype(e)->extraValues[3]);
    return range;
}

inline float Power(CSpellEffect* e) {
    float out;
    PowerToFloat(&e->Spell()->damage_, &Prototype(e)->damage, &out);
    return out;
}
inline float PowerScaled(CSpellEffect* e, const float& scale) {
    float out;
    PowerScaledToFloat(&e->Spell()->damage_, &Prototype(e)->damage, ConstPtr(scale), &out);
    return out;
}

// The lights' rise speed: range / max(extraValues[4], 1) (the larger picked on C0: 1 < v or NaN).
inline float LightRise(CSpellEffect* e) {
    float value;
    value = std::bit_cast<float>(Prototype(e)->extraValues[4]);
    float range = PrototypeRange(e);
    float divisor = value <= 1.0f ? 1.0f : value;
    return range / divisor;
}

// The same quotient left unrounded and multiplied by the constant at `scale`, then rounded.
inline float LightRiseScaled(CSpellEffect* e, const float& scale) {
    float value;
    value = std::bit_cast<float>(Prototype(e)->extraValues[4]);
    float range = PrototypeRange(e);
    float divisor = value <= 1.0f ? 1.0f : value;
    float out;
    DivideMultiply(&range, &divisor, ConstPtr(scale), &out);
    return out;
}

// Every unit within the radius of the effect's position that IsSpellTarget accepts gets the
// effect's damage (a list on the stack, walked node by node).
inline void HitAreaTargets(CSpellEffect* e) {
    std::list<CUnit*> units;
    DamageInfo hit;
    e->GetDamage(&hit);
    e->world->aiMap_.sectorMap.FindUnitsInRadius(&e->position, &units, e->Spell()->radius_);
    for (auto node = units.begin(); node != units.end();) {
        CUnit* unit = *node;
        ++node;
        if (static_cast<std::uint8_t>(IsSpellTarget(unit))) e->HitUnit(&hit, unit);
    }
}

inline float GroundPlus(CSpellEffect* e, double (CWorld::*height)(float, float), const Vector3& at, const float* add) {
    float out;
    HeightPlus(e->world, height, at.x, at.y, add, &out);
    return out;
}
inline float GroundMinus(CSpellEffect* e, double (CWorld::*height)(float, float), const Vector3& at, const float* sub) {
    float out;
    HeightMinus(e->world, height, at.x, at.y, sub, &out);
    return out;
}

// The light every lasting spell puts at its point: the prototype's colour and range.
inline void PrototypeLight(CSpellEffect* e, const Vector3* at, float range, const Vector3* velocity) {
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(at, &color, range, velocity);
}

enum class Next { Done, Set, Leave };   // state 3; state set by the case; leave the state alone

inline const Vector3 kZeroVector{0.0f, 0.0f, 0.0f};

} // namespace spellfx
