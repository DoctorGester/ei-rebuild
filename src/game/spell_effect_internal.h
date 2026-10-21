#pragma once

// Shared by spell_effect_start.cpp (casting, 0067EF40-00681B00) and spell_effect_update.cpp
// (missile flight 00681BB0, lasting effects 00682650): forwarders, the x87 sequences the
// original repeats inline, and small accessors. See docs/notes/items_spells.md.

#include "game_strings.h"
#include "spell.h"
#include "world_data.h"

#include "ai_map.h"
#include "combat.h"
#include "console.h"
#include "lib/mfc.h"
#include "map_object.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "originals_ai.h"
#include "originals_effects.h"
#include "originals_gameplay.h"
#include "originals_world.h"
#include "originals_units.h"
#include "originals_library.h"
#include "ei/x87.h"
#include "lib/vecmath.h"
#include "lib/mfc_list.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

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

// The world's height slots the x87 helpers below call through (27 the grid height, 28 the ground).
constexpr int kWorldSlotGridHeight = 27;
constexpr int kWorldSlotGroundHeight = 28;

constexpr std::uint32_t kPointLightFlags = 0x840;
constexpr int kPtrListBlockSize = 10;

// Volumes and radii the casts pass as immediates.
constexpr float kVolume = 10.0f;
constexpr float kHearing = 50.0f;
constexpr float kLoud = 1000.0f;

// Constants the original reads from memory (Rebirth may patch data; read them as the original does).
inline float ConstAt(const float& constant) { return *reinterpret_cast<const volatile float*>(&constant); }
inline const float* ConstPtr(const float& constant) { return &constant; }
constexpr float kDefaultSpeed = 5.0f;                   // immediate in the fireball case
constexpr float kHalfBits = 0.5f;                       // immediate pushed to the footprint builder


// --- Forwarders ----------------------------------------------------------------------------------

// The world server's timed objects: WorldAddTimed (originals_world.h).
// Effect setters (world_effect*.cpp).
// 0067AC70 (not rewritten): the missile's target object id.
// 0067C640: CEffectShowVisibility's two object ids (+0x14C, +0x150).
// CPtrList constructor (block size) and destructor.
inline void* __fastcall PtrListConstruct(void* a0, void*, int a2) { return mfc::impl::ConstructListHeader<mfc::impl::PtrListClass>(a0, a2); }
// The unit controller (+0x240): 00552780 says remark `what`; 00551AE0 tries to be tamed by `tamer`
// with skill `skill` (0 when it failed).
// Footprints of the walls and clouds in the global cell set 007C8478 (thiscall on the set): along a
// direction (005B2A00) or a disc (005B2760); then stamped into / cleared from the AI map (ai_footprint.cpp).
inline FootprintCellSet* SpellFootprint() { return ei::GamePointer<FootprintCellSet>(0x007C8478); }
// The later states' callees.
// 005302F0 CUnit::ModeSpeed (unit.cpp); left unrounded in ST0.
// 005C98C0 (world + 0x2838, the AI events): a noise of `loudness` and `kind` made by `source` at `at`.
inline void __fastcall AiNoise(void* a0, void*, void* a2, float a3, int a4, const Vector3* a5) { static_cast<AIViewer*>(a0)->Noise(static_cast<CUnitServer*>(a2), a3, a4, a5); }
// 005B7A40 CAIMap::IsPassableWithoutSelf (ai_map.cpp); 005C11D0 CSectorMap::Move: originals_ai.h.

// --- x87 sequences ------------------------------------------------------------------------------
// Plain C++ in the x87 order: double intermediates are the original's unrounded register values.

inline float GameConst(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
inline double GameConstDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }

// The spell's power curve after the ratio: pow(min(max(ratio, 1), 8), 0.33) - 0.2, written by the
// original as FYL2X / F2XM1 / FSCALE (x87::flog, x87::fexp; the logarithm is used unrounded). The
// two clamps select the float bits on C0 (1 < r or unordered; r < 8 or unordered), the first one
// stored and reloaded (FLD / FSTP).
inline double ClampPow(float ratio) {
    std::uint32_t bits;
    std::memcpy(&bits, &ratio, sizeof(bits));
    const std::uint32_t lowBits = x87::LoadStoreBits(!(ratio <= 1.0f) ? bits : 0x3F800000u);
    float low;
    std::memcpy(&low, &lowBits, sizeof(low));
    const float clamped = !(low >= 8.0f) ? low : 8.0f;
    return x87::fexp(x87::flog(clamped) * GameConstDouble(0x007439A8)) - GameConstDouble(0x0073CCD0);
}

// The spell's power factor, repeated inline about twenty times in the original: 1 (0073B84C) when
// the prototype's effect (+0x2C) compares equal to 0 (0073B858) or is NaN (FCOM; TEST AH,0x40),
// else ClampPow of damage / effect rounded to float.
inline double PowerFactor(const float* damage, const float* effect) {
    if ((x87::CompareStatus(*effect, GameConst(0x0073B858)) & x87::kC3) != 0) return GameConst(0x0073B84C);
    return ClampPow(static_cast<float>(static_cast<double>(*damage) / *effect));
}

// The fireball explosion's size curve: pow(clamp((r * r * pi) / area, 1, 8), 0.33) - 0.2 with the
// ratio rounded to float, the result rounded to float into *out.
inline void AreaCurve(const float* radius, const float* area, float* out) {
    const float ratio = static_cast<float>(static_cast<double>(*radius) * *radius * GameConst(0x00743964) / *area);
    *out = static_cast<float>(ClampPow(ratio));
}

// *out = (float)((*a / *b) * *c), the quotient unrounded.
inline void DivideMultiply(const float* a, const float* b, const float* c, float* out) {
    *out = static_cast<float>(static_cast<double>(*a) / *b * *c);
}

// The fireworks' fright level: _ftol(((float)timer / duration) * 250 + 50) with FILD / FIDIV,
// unrounded until the game's _ftol truncates it.
inline int FrightLevel(const int* timer, const int* duration) {
    return static_cast<int>(gamecrt::Ftol(static_cast<double>(*timer) / *duration * GameConst(0x0073BF48) + GameConst(0x0074023C)));
}

// *out = (float)(*a * *b + *c), unrounded in between.
inline void MultiplyAdd(const float* a, const float* b, const float* c, float* out) {
    *out = static_cast<float>(static_cast<double>(*a) * *b + *c);
}

// The game's _ftol (006E853C) of a float constant in memory.
inline int FtolOf(const float* value) { return static_cast<int>(gamecrt::Ftol(*value)); }

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
    return static_cast<int>(gamecrt::Ftol(PowerFactor(damage, effect) * *scale));
}

// World slot `slot` (thiscall on the world; x, y as the floats' bits): a ground height left
// unrounded in ST0.
inline double WorldHeight(void* world, int slot, std::uint32_t x, std::uint32_t y) {
    using Slot = double(__fastcall*)(void* self, void* unused, std::uint32_t x, std::uint32_t y);
    return reinterpret_cast<Slot>((*static_cast<void***>(world))[slot])(world, nullptr, x, y);
}

// *out = (float)(height + *add).
inline void HeightPlus(void* world, int slot, std::uint32_t x, std::uint32_t y, const float* add, float* out) {
    *out = static_cast<float>(WorldHeight(world, slot, x, y) + *add);
}

// The same with height - *sub (FSUB).
inline void HeightMinus(void* world, int slot, std::uint32_t x, std::uint32_t y, const float* sub, float* out) {
    *out = static_cast<float>(WorldHeight(world, slot, x, y) - *sub);
}

// The lightning's end: *out = (float)(height + *a * *b).
inline void HeightPlusProduct(void* world, int slot, std::uint32_t x, std::uint32_t y, const float* a, const float* b,
                              float* out) {
    *out = static_cast<float>(WorldHeight(world, slot, x, y) + static_cast<double>(*a) * *b);
}

// *out = (float)sqrt(y*y + x*x).
inline void PlanarLength(const Vector3* v, float* out) {
    *out = static_cast<float>(x87::fsqrt(LengthSquared2DUnrounded<double>(v->y, v->x)));
}

// The fireball's velocity: v *= (speed * 1/15) / sqrt((y*y + x*x) + z*z), the factor unrounded,
// each component rounded to float.
inline void ScaleToSpeed(Vector3* v, const float* speed) {
    const double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(*v));
    const double k = static_cast<double>(*speed) * GameConst(0x00740544) / length;
    v->x = static_cast<float>(k * v->x);
    v->y = static_cast<float>(k * v->y);
    v->z = static_cast<float>(k * v->z);
}

// FISTP((float)((distance / speed) * 15)): the missile's flight time in ticks.
inline int FlightTicks(const float* distance, const float* speed) {
    return x87::fistp(static_cast<float>(static_cast<double>(*distance) / *speed * GameConst(0x0073C518)));
}

// The walls' extent: the normal (-d.y, d.x, 0) in the ground plane scaled to length `radius`:
// k = (float)(radius / sqrt(n.y*n.y + n.x*n.x)), then each component (float)(component * k). The
// normal is written to `out` first (d.x copied as bits) and read back, as the original does.
inline void WallSpan(const Vector3* direction, const float* radius, Vector3* out) {
    const double nx = -static_cast<double>(direction->y);
    out->x = static_cast<float>(nx);
    std::memcpy(&out->y, &direction->x, sizeof(float));
    out->z = 0.0f;
    const double length = x87::fsqrt(LengthSquared2DUnrounded<double>(out->y, nx));
    const float k = static_cast<float>(*radius / length);
    out->x = static_cast<float>(k * nx);
    out->y = static_cast<float>(static_cast<double>(out->y) * k);
    out->z = static_cast<float>(static_cast<double>(out->z) * k);
}

// The light wall's half extent from its lightning's ends: x keeps the difference unrounded before
// halving (0073B86C), y and z are rounded first.
inline void HalfSpan(const CEngineObject* lightning, Vector3* out) {
    const auto* bolt = static_cast<const CEffectLightning*>(lightning);
    const double half = GameConst(0x0073B86C);
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
    std::memcpy(&u, &f, 4);
    return u;
}

inline bool IsUnit(CEngineObject* object) { return object && object->GetClassId() == kClassUnit; }
// Where the object sees from (slot 48) into `scratch`.
inline Vector3* PositionOf(CEngineObject* object, Vector3* scratch) { return ObjectSlots(object)->EyePoint(scratch); }
// A unit's server half (CUnit::controller).
inline CUnitServer* ControllerOf(CEngineObject* unit) { return static_cast<CUnitServer*>(static_cast<CUnit*>(unit)->controller); }

// A new object of class `classId` from the world's timed object manager (key -1).
inline CEngineObject* CreateObject(CSpellEffect* e, int classId) {
    return e->world->TimedObjectManager()->CreateObject(classId, -1);
}

// The counted-pointer assignment the helpers repeat: the old object leaves the world's timed
// objects and is released, the new one is counted.
inline void Assign(CSpellEffect* e, CEngineObject** slot, CEngineObject* object) {
    if (*slot) static_cast<CWorldServer*>(e->world)->RemoveTimedObject((*slot)->id_);
    CEngineObject* old = *slot;
    *slot = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

inline void SetPosition(CEngineObject* object, const Vector3* position) {
    std::memcpy(&static_cast<CTransformObject*>(object)->localPosition_, position, 12);
}

// The fields of the spell prototype the casts read.
inline const char* Prototype(CSpellEffect* e) { return e->Spell()->GetPrototype(); }
inline Vector3 PrototypeColor(CSpellEffect* e) {
    Vector3 color;
    std::memcpy(&color, Prototype(e) + 0x64, 12);
    return color;
}
inline float PrototypeRange(CSpellEffect* e) {
    float range;
    std::memcpy(&range, Prototype(e) + 0x70, 4);
    return range;
}

inline float Power(CSpellEffect* e) {
    float out;
    PowerToFloat(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), &out);
    return out;
}
inline float PowerScaled(CSpellEffect* e, const float& scale) {
    float out;
    PowerScaledToFloat(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), ConstPtr(scale), &out);
    return out;
}

// The lights' rise speed: range / max(prototype +0x74, 1) (the larger picked on C0: 1 < v or NaN).
inline float LightRise(CSpellEffect* e) {
    float value;
    std::memcpy(&value, Prototype(e) + 0x74, 4);
    float range = PrototypeRange(e);
    float divisor = !(value <= 1.0f) ? value : 1.0f;
    return range / divisor;
}

// The same quotient left unrounded and multiplied by the constant at `scale`, then rounded.
inline float LightRiseScaled(CSpellEffect* e, const float& scale) {
    float value;
    std::memcpy(&value, Prototype(e) + 0x74, 4);
    float range = PrototypeRange(e);
    float divisor = !(value <= 1.0f) ? value : 1.0f;
    float out;
    DivideMultiply(&range, &divisor, ConstPtr(scale), &out);
    return out;
}

// Every unit within the radius of the effect's position that IsSpellTarget accepts gets the
// effect's damage (a CPtrList of blocks of 10 on the stack, walked node by node).
inline void HitAreaTargets(CSpellEffect* e) {
    mfc::CTypedPtrList<CEngineObject> units;
    PtrListConstruct(&units, nullptr, kPtrListBlockSize);
    DamageInfo hit;
    e->GetDamage(&hit);
    e->world->Sectors()->FindUnitsInRadius(&e->position, &units, e->radius);
    for (auto* node = units.head; node;) {
        CEngineObject* unit = node->data;
        node = node->next;
        if (static_cast<std::uint8_t>(IsSpellTarget(unit))) e->HitUnit(&hit, unit);
    }
    mfc::impl::DestructList<mfc::impl::PtrListClass>(units);
}

inline float GroundPlus(CSpellEffect* e, int slot, const Vector3& at, const float* add) {
    float out;
    HeightPlus(e->world, slot, Bits(at.x), Bits(at.y), add, &out);
    return out;
}
inline float GroundMinus(CSpellEffect* e, int slot, const Vector3& at, const float* sub) {
    float out;
    HeightMinus(e->world, slot, Bits(at.x), Bits(at.y), sub, &out);
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
