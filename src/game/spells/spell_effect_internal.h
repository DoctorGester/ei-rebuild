#pragma once

// Shared by the spell effects' start and update: helpers, the original's maths repeated inline, and small
// accessors. See docs/notes/items_spells.md.

#include "game/ai/ai_map.h"
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/objects/map_object.h"
#include "engine/scene/light.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/spells/spell_db.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <type_traits>

namespace spellfx {

// --- Constants ----------------------------------------------------------------------------------

constexpr std::uint32_t kPointLightFlags = lib::Bits(LightFlag::Specular | LightFlag::Strong);

// The volumes and radii of the casts' sounds.
constexpr float kVolume = 10.0f;
constexpr float kHearing = 50.0f;
constexpr float kLoud = 1000.0f;

constexpr float kDefaultSpeed = 5.0f;           // a flight's speed (a second) when the spell has none
constexpr float kBoltWidthScale = 5.0f;         // a lightning's width per power
constexpr float kTargetHeight = 1.2f;           // a lightning ends at the target's top x this above the ground
constexpr float kBuffScale = 16.0f;             // a buff's strength per power
constexpr float kPossessWake = 2.0f;            // possession wakes the units within reach x this + kPossessWakeBase
constexpr float kPossessWakeBase = 20.0f;

// The cells of a cast's footprint (SpellEffect::Block), along a direction or a disc, then stamped into / cleared from
// the AI map.
extern FootprintCellSet g_spellFootprint;

// --- Maths ---------------------------------------------------------------------------------------

// The spell's power curve after the ratio: pow(min(max(ratio, 1), 8), 0.33) - 0.2. The two clamps keep a NaN
// ratio (1 < r or unordered; r < 8 or unordered).
inline float ClampPow(float ratio) {
    const float low = MaxKeepB(1.0f, ratio);
    const float clamped = MinKeepA(low, 8.0f);
    return lib::Exp(lib::Log(clamped) * 0.33f) - 0.2f;
}

// The spell's power factor: 1 when the prototype's effect compares equal to 0, else ClampPow of damage / effect.
inline float PowerFactor(float damage, float effect) {
    if (effect == 0.0f) return 1.0f;
    return ClampPow(damage / effect);
}

// The area power (the fireball blast's size): pow(clamp((r * r * pi) / area, 1, 8), 0.33) - 0.2.
inline float AreaCurve(float radius, float area) { return ClampPow(radius * radius * 3.1415927f / area); }

// A flight's velocity a tick at `speed` a second: v * ((speed * 1/15) / sqrt((y*y + x*x) + z*z)).
inline Vector3 ScaledToSpeed(Vector3 v, float speed) {
    const float length = std::sqrt(LengthSquared(v));
    const float k = speed * 0.06666667f / length;
    v *= k;
    return v;
}

// (distance / speed) * 15 rounded to nearest: the missile's flight time in ticks.
inline int FlightTicks(float distance, float speed) { return lib::RoundEven(distance / speed * 15.0f); }

// The direction from `from` to a wall's centre; straight along x when it has no ground part (both 0).
inline Vector3 WallDirection(const Vector3& centre, const Vector3& from) {
    Vector3 d = centre - from;
    if (XY(d) == Vector2{0.0f, 0.0f}) d.x = 1.0f;
    return d;
}

// The walls' extent: the normal (-d.y, d.x, 0) in the ground plane scaled to length `radius` (each component times
// radius / |n|).
inline Vector3 WallSpan(const Vector3& direction, float radius) {
    Vector3 span{-direction.y, direction.x, 0.0f};
    span *= radius / Length2D(XY(span));
    return span;
}

// --- Small helpers ------------------------------------------------------------------------------

inline bool IsUnit(EngineObject* object) { return object && object->GetClassId() == ClassId::Unit; }
// A unit's server half (Unit::controller).
inline UnitServer* ControllerOf(Unit* unit) { return unit->ServerController(); }

// From the world's timed object manager.
template <class T>
T* CreateObject(SpellEffect* e, ClassId classId) {
    return e->world->TimedObjectManager()->CreateObjectAs<T>(classId, -1);
}

// `slot` holds `object`; the object it held leaves the world's timed objects and is released.
template <class T>
void Hold(SpellEffect* e, Ref<T>& slot, std::type_identity_t<T>* object) {
    if (slot) e->world->RemoveTimedObject(slot->id);
    slot.Assign(object);
}

inline void SetPosition(TransformObject* object, const Vector3& position) {
    object->localPosition = position;
}

// The spell's prototype.
inline const itemdb::SpellRecord* Prototype(const SpellEffect* e) { return e->HeldSpell()->GetPrototype(); }

inline float Power(const SpellEffect* e) { return PowerFactor(e->HeldSpell()->damage, Prototype(e)->damage); }

// The lights' rise speed: lightRange / max(lightRiseDivisor, 1), a NaN divisor kept.
inline float LightRise(const SpellEffect* e) {
    const itemdb::SpellRecord* prototype = Prototype(e);
    return prototype->lightRange / MaxKeepB(1.0f, prototype->lightRiseDivisor);
}

// SpellEffect::state.
constexpr int kStateCasting = 0;    // Start runs
constexpr int kStateMissile = 1;    // the missile flies (a save's from before its spell moved onto a script)
constexpr int kStateLasting = 2;    // the effect lasts, `timer` ticks
constexpr int kStateEnded = 3;      // the effects' objects go and the spell effect is removed
constexpr int kStateScripted = 4;   // a scripted spell's cast lives (ScriptSpellHost)

// The effect lasts for the spell's duration (the lasting spells that stand where they were cast).
inline void EnterLasting(SpellEffect* e) {
    e->state = kStateLasting;
    e->timer = static_cast<int>(e->HeldSpell()->duration);
}

inline const Vector3 kZeroVector{0.0f, 0.0f, 0.0f};

} // namespace spellfx
