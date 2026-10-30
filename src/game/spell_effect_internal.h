#pragma once

// Shared by the spell effects' start and update: helpers, the original's maths repeated inline, and small
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
#include "map_object.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"


namespace spellfx {

// --- Slots ---------------------------------------------------------------------------------------

constexpr double (World::*kGridHeight)(float, float) = &World::GridHeightAt;
constexpr double (World::*kRawSurfaceHeight)(float, float) = &World::RawSurfaceHeightAt;

constexpr std::uint32_t kPointLightFlags = lib::Bits(LightFlag::Specular | LightFlag::Strong);

// Volumes and radii the casts pass as immediates.
constexpr float kVolume = 10.0f;
constexpr float kHearing = 50.0f;
constexpr float kLoud = 1000.0f;

// Constants read from memory (Rebirth may patch data, so they are read, not folded).
inline float ConstAt(const float& constant) { return *reinterpret_cast<const volatile float*>(&constant); }
inline const float* ConstPtr(const float& constant) { return &constant; }
constexpr float kDefaultSpeed = 5.0f;                   // immediate in the fireball case

// Footprints of the walls and clouds, along a direction or a disc, then stamped into / cleared from the AI
// map.
extern FootprintCellSet g_spellFootprint;

// --- The original's maths -----------------------------------------------------------------------
// Float maths in the original's order: its 24-bit x87 (PC24) rounds each operation as a float does. The logarithm,
// the exponential, a ground height (a double from the world) and an int quotient go through lib/pc24.h.

// The spell's power curve after the ratio: pow(min(max(ratio, 1), 8), 0.33) - 0.2, written by the original as
// FYL2X / F2XM1 / FSCALE (VC6's inline log and exp; the logarithm is used unrounded). The two clamps keep a NaN
// ratio (1 < r or unordered; r < 8 or unordered).
inline float ClampPow(float ratio) {
    const float low = MaxKeepB(1.0f, ratio);
    const float clamped = MinKeepA(low, 8.0f);
    return pc24::Exp(pc24::Mul(pc24::Log(clamped), 0.33f)) - 0.2f;
}

// The spell's power factor(repeated inline about twenty times in the game): 1 when the prototype's effect
// compares equal to 0, else ClampPow of damage / effect.
inline float PowerFactor(const float* damage, const float* effect) {
    EI_NAN_AUDIT(*effect);
    if (*effect == 0.0f) return 1.0f;
    return ClampPow(*damage / *effect);
}

// The fireball explosion's size curve: pow(clamp((r * r * pi) / area, 1, 8), 0.33) - 0.2 into *out.
inline void AreaCurve(const float* radius, const float* area, float* out) {
    *out = ClampPow(*radius * *radius * 3.1415927f / *area);
}

// *out = (*a / *b) * *c.
inline void DivideMultiply(const float* a, const float* b, const float* c, float* out) { *out = *a / *b * *c; }

// The fireworks' fright level: _ftol(((float)timer / duration) * 250 + 50), the ints divided exactly and the
// quotient rounded once; a duration of 0 gives _ftol's 0 for a NaN, 0x80000000 for an infinity's low half.
inline int FrightLevel(const int* timer, const int* duration) {
    const float level = pc24::Div(static_cast<double>(*timer), static_cast<double>(*duration)) * 250.0f + 50.0f;
    return static_cast<int>(static_cast<std::int64_t>(level));
}

// *out = *a * *b + *c.
inline void MultiplyAdd(const float* a, const float* b, const float* c, float* out) { *out = *a * *b + *c; }

// The game's _ftol of a float constant in memory.
inline int FtolOf(const float* value) { return static_cast<int>(*value); }

// The factor as a float.
inline void PowerToFloat(const float* damage, const float* effect, float* out) { *out = PowerFactor(damage, effect); }

// The factor times *scale.
inline void PowerScaledToFloat(const float* damage, const float* effect, const float* scale, float* out) {
    *out = PowerFactor(damage, effect) * *scale;
}

// The factor times *scale, truncated by the game's _ftol (0 for a NaN damage).
inline int PowerScaledToInt(const float* damage, const float* effect, const float* scale) {
    return static_cast<int>(static_cast<std::int64_t>(PowerFactor(damage, effect) * *scale));
}

// A world virtual `height` (kGridHeight, kRawSurfaceHeight) at (x, y): a ground height, a double.
inline double WorldHeight(World* world, double (World::*height)(float, float), float x, float y) {
    return (world->*height)(x, y);
}

// *out = height + *add, rounded once.
inline void HeightPlus(World* world, double (World::*height)(float, float), float x, float y, const float* add, float* out) {
    *out = pc24::Add(WorldHeight(world, height, x, y), *add);
}

// The same with height - *sub.
inline void HeightMinus(World* world, double (World::*height)(float, float), float x, float y, const float* sub, float* out) {
    *out = pc24::Sub(WorldHeight(world, height, x, y), *sub);
}

// The lightning's end: *out = height + *a * *b.
inline void HeightPlusProduct(World* world, double (World::*height)(float, float), float x, float y, const float* a, const float* b,
                              float* out) {
    *out = pc24::Add(WorldHeight(world, height, x, y), *a * *b);
}

// *out = sqrt(y*y + x*x).
inline void PlanarLength(const Vector3& v, float* out) { *out = std::sqrt(LengthSquared2DUnrounded<float>(v.y, v.x)); }

// The fireball's velocity: v *= (speed * 1/15) / sqrt((y*y + x*x) + z*z).
inline void ScaleToSpeed(Vector3* v, const float* speed) {
    const float length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, float>(*v));
    const float k = *speed * 0.06666667f / length;
    v->x = k * v->x;
    v->y = k * v->y;
    v->z = k * v->z;
}

// (distance / speed) * 15 rounded to nearest: the missile's flight time in ticks.
inline int FlightTicks(const float* distance, const float* speed) { return lib::RoundEven(*distance / *speed * 15.0f); }

// The walls' extent: the normal (-d.y, d.x, 0) in the ground plane scaled to length `radius`: k =
// radius / sqrt(n.y*n.y + n.x*n.x), then each component times k. The normal is written to `out` first and read back.
inline void WallSpan(const Vector3& direction, const float* radius, Vector3* out) {
    const float nx = -direction.y;
    out->x = nx;
    out->y = direction.x;
    out->z = 0.0f;
    const float k = *radius / std::sqrt(LengthSquared2DUnrounded<float>(out->y, nx));
    out->x = k * nx;
    out->y = out->y * k;
    out->z = out->z * k;
}

// The light wall's half extent from its lightning's ends.
inline void HalfSpan(const EffectLightning* bolt, Vector3* out) {
    const float dx = bolt->to.x - bolt->from.x;
    out->y = bolt->to.y - bolt->from.y;
    out->z = bolt->to.z - bolt->from.z;
    out->x = dx * 0.5f;
    out->y = out->y * 0.5f;
    out->z = out->z * 0.5f;
}

// --- Small helpers ------------------------------------------------------------------------------

inline std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    u = std::bit_cast<std::uint32_t>(f);
    return u;
}

inline bool IsUnit(EngineObject* object) { return object && object->GetClassId() == ClassId::Unit; }
// Where the object sees from into `scratch`.
inline Vector3* EyePointOf(MapObject* object, Vector3* scratch) { return object->EyePoint(scratch); }
// A unit's server half (Unit::controller).
inline UnitServer* ControllerOf(Unit* unit) { return unit->ServerController(); }
// The target as a unit (the callers checked its class, or the game does not).
inline Unit* TargetUnit(SpellEffect* e) { return static_cast<Unit*>(e->target.Get()); }

// From the world's timed object manager.
template <class T>
T* CreateObject(SpellEffect* e, ClassId classId) {
    return e->world->TimedObjectManager()->CreateObjectAs<T>(classId, -1);
}

// The counted-pointer assignment the helpers repeat: the old object leaves the world's timed
// objects and is released, the new one is counted.
template <class T>
void Assign(SpellEffect* e, T** slot, T* object) {
    if (*slot) e->world->RemoveTimedObject((*slot)->id);
    T* old = *slot;
    *slot = object;
    if (object) object->AddRef();
    if (old) old->Release();
}

inline void SetPosition(TransformObject* object, const Vector3& position) {
    object->localPosition = position;
}

// The fields of the spell prototype the casts read.
inline const itemdb::SpellRecord* Prototype(SpellEffect* e) { return e->HeldSpell()->GetPrototype(); }
inline Vector3 PrototypeColor(SpellEffect* e) {
    Vector3 color;
    std::memcpy(&color, &Prototype(e)->extraValues[0], 12);
    return color;
}
inline float PrototypeRange(SpellEffect* e) {
    float range;
    range = std::bit_cast<float>(Prototype(e)->extraValues[3]);
    return range;
}

inline float Power(SpellEffect* e) {
    float out;
    PowerToFloat(&e->HeldSpell()->damage, &Prototype(e)->damage, &out);
    return out;
}
inline float PowerScaled(SpellEffect* e, const float& scale) {
    float out;
    PowerScaledToFloat(&e->HeldSpell()->damage, &Prototype(e)->damage, ConstPtr(scale), &out);
    return out;
}

// The lights' rise speed: range / max(extraValues[4], 1) (the larger picked on C0: 1 < v or NaN).
inline float LightRise(SpellEffect* e) {
    float value;
    value = std::bit_cast<float>(Prototype(e)->extraValues[4]);
    float range = PrototypeRange(e);
    float divisor = MaxKeepB(1.0f, value);
    return range / divisor;
}

// The same quotient left unrounded and multiplied by the constant at `scale`, then rounded.
inline float LightRiseScaled(SpellEffect* e, const float& scale) {
    float value;
    value = std::bit_cast<float>(Prototype(e)->extraValues[4]);
    float range = PrototypeRange(e);
    float divisor = MaxKeepB(1.0f, value);
    float out;
    DivideMultiply(&range, &divisor, ConstPtr(scale), &out);
    return out;
}

// Every unit within the radius of the effect's position that IsSpellTarget accepts gets the
// effect's damage (a list on the stack, walked node by node).
inline void HitAreaTargets(SpellEffect* e) {
    DamageInfo hit;
    e->GetDamage(&hit);
    for (Unit* unit : e->world->aiMap.sectorMap.FindUnitsInRadius(e->position, e->HeldSpell()->radius)) {
        if (static_cast<std::uint8_t>(IsSpellTarget(unit))) e->HitUnit(&hit, unit);
    }
}

inline float GroundPlus(SpellEffect* e, double (World::*height)(float, float), const Vector3& at, const float* add) {
    float out;
    HeightPlus(e->world, height, at.x, at.y, add, &out);
    return out;
}
inline float GroundMinus(SpellEffect* e, double (World::*height)(float, float), const Vector3& at, const float* sub) {
    float out;
    HeightMinus(e->world, height, at.x, at.y, sub, &out);
    return out;
}

// The light every lasting spell puts at its point: the prototype's colour and range.
inline void PrototypeLight(SpellEffect* e, const Vector3& at, float range, const Vector3& velocity) {
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(at, color, range, velocity);
}

enum class Next { Done, Set, Leave };   // state 3; state set by the case; leave the state alone

inline const Vector3 kZeroVector{0.0f, 0.0f, 0.0f};

} // namespace spellfx
