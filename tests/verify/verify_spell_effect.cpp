// Checks for the spell effect hit helpers (spell_effect.cpp).
#include "verify.h"

#include "../../src/game/spell.h"

#include "ei/vcall.h"

#include <cstring>

namespace {
// The spell parameters of the effect (+0x4C..+0x60), which the hit helpers must leave alone.
// (Target and caster are not compared: a unit dying from the hit may drop references.)
struct EffectState {
    char bytes[0x60 - 0x4C];
    explicit EffectState(const CSpellEffect* effect) {
        std::memcpy(bytes, reinterpret_cast<const char*>(effect) + 0x4C, sizeof(bytes));
    }
    bool operator==(const EffectState& o) const { return std::memcmp(bytes, o.bytes, sizeof(bytes)) == 0; }
};
} // namespace

// 00681B80: MOV EAX,[ECX+0x324] (6 bytes). Pure.
static int(__fastcall* s_isSpellTarget)(CEngineObject*);
static int __fastcall IsSpellTargetVerify(CEngineObject* unit) {
    int expected = s_isSpellTarget(unit);
    ei::verify::Check("IsSpellTarget", expected, IsSpellTarget(unit), "unit %p", unit);
    return expected;
}
EI_VERIFY(0x00681B80, IsSpellTargetVerify, 6, s_isSpellTarget);

// 006835B0, 00683600, 00683730 deal damage (CUnit slot 31): ours only, run once. The check is
// that the effect's spell parameters are left unchanged; the damage shows in the scenario's HP checks.
static void* s_hitUnit;
static void __fastcall HitUnitVerify(CSpellEffect* self, void*, DamageInfo* hit, CEngineObject* unit) {
    EffectState before(self);
    self->HitUnit(hit, unit);
    ei::verify::Check("CSpellEffect::HitUnit", true, EffectState(self) == before, "spell %08X unit %p", self->spellId,
                      unit);
}
EI_VERIFY(0x006835B0, HitUnitVerify, 0, s_hitUnit);

static void* s_hitRadius;
static void __fastcall HitUnitsInRadiusVerify(CSpellEffect* self) {
    EffectState before(self);
    self->HitUnitsInRadius();
    ei::verify::Check("CSpellEffect::HitUnitsInRadius", true, EffectState(self) == before, "spell %08X radius %g",
                      self->spellId, self->radius);
}
EI_VERIFY(0x00683600, HitUnitsInRadiusVerify, 0, s_hitRadius);

static void* s_hitAlong;
static void __fastcall HitUnitsAlongVerify(CSpellEffect* self, void*, const Vector3* direction) {
    EffectState before(self);
    self->HitUnitsAlong(direction);
    ei::verify::Check("CSpellEffect::HitUnitsAlong", true, EffectState(self) == before, "spell %08X dir (%g, %g)",
                      self->spellId, direction->x, direction->y);
}
EI_VERIFY(0x00683730, HitUnitsAlongVerify, 0, s_hitAlong);

// --- The casting state and its effect-object helpers (spell_effect_start.cpp) -------------------
// All of them create world objects or deal damage: ours only, then the results are checked.

namespace {
int ClassIdOf(const CEngineObject* object) { return ei::VCall<int>(object, 12); }
const char* At(const void* object, int offset) { return static_cast<const char*>(object) + offset; }
} // namespace

// 0067F870: the spell parameters are left alone and the next state is 0..3.
static void* s_start;
static void __fastcall StartVerify(CSpellEffect* self, void*, int type) {
    EffectState before(self);
    self->Start(type);
    bool stateOk = self->state >= 0 && self->state <= 3;
    ei::verify::Check("CSpellEffect::Start", true, stateOk && EffectState(self) == before, "type %d state %d", type,
                      self->state);
}
EI_VERIFY(0x0067F870, StartVerify, 0, s_start);

// 0067EF40: the visual is the object.
static void* s_setVisual;
static void __fastcall SetVisualVerify(CSpellEffect* self, void*, CEngineObject* object) {
    self->SetVisual(object);
    ei::verify::Check("CSpellEffect::SetVisual", object, self->visual, "spell %08X", self->spellId);
}
EI_VERIFY(0x0067EF40, SetVisualVerify, 0, s_setVisual);

// 0067EFC0: the stamped flag follows `stamp` for the four footprint types with a visual.
static void* s_footprint;
static void __fastcall UpdateFootprintVerify(CSpellEffect* self, void*, int type, int stamp) {
    std::uint8_t before = self->footprintStamped;
    bool applies = self->visual && (type == 6 || type == 7 || type == 8 || type == 0x27);
    self->UpdateFootprint(type, stamp);
    std::uint8_t expected = applies ? static_cast<std::uint8_t>(stamp ? 1 : 0) : before;
    ei::verify::Check("CSpellEffect::UpdateFootprint", expected, self->footprintStamped, "type %d stamp %d", type,
                      stamp);
}
EI_VERIFY(0x0067EFC0, UpdateFootprintVerify, 0, s_footprint);

// 0067F170: the visual is a missile (class 0x4E) with the particle type.
static void* s_arrow;
static void __fastcall CreateArrowVerify(CSpellEffect* self, void*, const Vector3* from, CEngineObject* target, int type,
                                         float power, const char* sound, const Vector3* color, float range) {
    self->CreateArrow(from, target, type, power, sound, color, range);
    bool ok = self->visual && ClassIdOf(self->visual) == 0x4E &&
              *reinterpret_cast<const int*>(At(self->visual, 0x6B4)) == type;
    ei::verify::Check("CSpellEffect::CreateArrow", true, ok, "type %X", type);
}
EI_VERIFY(0x0067F170, CreateArrowVerify, 0, s_arrow);

// 0067F260: a particle (0x4A) or wall (0x5A) of the type; the visual when kept.
static void* s_particle;
static CEngineObject* __fastcall CreateParticleVerify(CSpellEffect* self, void*, int keep, int type, const Vector3* at,
                                                      float size, float alpha, const Vector3* velocity,
                                                      CEngineObject* attachTo) {
    CEngineObject* particle = self->CreateParticle(keep, type, at, size, alpha, velocity, attachTo);
    int classId = ClassIdOf(particle);
    bool ok = (classId == 0x4A || classId == 0x5A) && *reinterpret_cast<const int*>(At(particle, 0x158)) == type &&
              (!keep || self->visual == particle);
    ei::verify::Check("CSpellEffect::CreateParticle", true, ok, "type %X keep %d", type, keep);
    return particle;
}
EI_VERIFY(0x0067F260, CreateParticleVerify, 0, s_particle);

// 0067F410: with a target the visual is a lightning (0x4D).
static void* s_lightning;
static void __fastcall CreateLightningVerify(CSpellEffect* self, void*, const Vector3* from, CEngineObject* target,
                                             CEngineObject* source, float power, int flag) {
    CEngineObject* before = self->visual;
    self->CreateLightning(from, target, source, power, flag);
    bool ok = target ? (self->visual && ClassIdOf(self->visual) == 0x4D) : self->visual == before;
    ei::verify::Check("CSpellEffect::CreateLightning", true, ok, "target %p", target);
}
EI_VERIFY(0x0067F410, CreateLightningVerify, 0, s_lightning);

// 0067F580: a kept sound is the sound (0x48) with the name.
static void* s_sound;
static void __fastcall CreateSoundVerify(CSpellEffect* self, void*, int keep, const char* name, const Vector3* at,
                                         const Vector3* velocity, float volume, float radius) {
    CEngineObject* before = self->sound;
    self->CreateSound(keep, name, at, velocity, volume, radius);
    bool ok = keep ? (self->sound && ClassIdOf(self->sound) == 0x48 && std::strcmp(At(self->sound, 0x14C), name) == 0)
                   : self->sound == before;
    ei::verify::Check("CSpellEffect::CreateSound", true, ok, "%s keep %d", name, keep);
}
EI_VERIFY(0x0067F580, CreateSoundVerify, 0, s_sound);

// 0067F660: the sound is a sequence (99) holding max(count, 1) names after the first.
static void* s_sequence;
static void __fastcall CreateSoundSequenceVerify(CSpellEffect* self, void*, const Vector3* at, const Vector3* velocity,
                                                 const char* first, const char* pattern, int count, float volume,
                                                 float radius) {
    self->CreateSoundSequence(at, velocity, first, pattern, count, volume, radius);
    int names = *reinterpret_cast<const int*>(At(self->sound, 0x158));
    bool ok = ClassIdOf(self->sound) == 0x63 && names == (count < 1 ? 1 : count);
    ei::verify::Check("CSpellEffect::CreateSoundSequence", true, ok, "%s count %d names %d", pattern, count, names);
}
EI_VERIFY(0x0067F660, CreateSoundSequenceVerify, 0, s_sequence);

// 0067F7C0: the light is a point light (0x4B) with flags 0x840.
static void* s_light;
static void __fastcall CreatePointLightVerify(CSpellEffect* self, void*, const Vector3* at, const Vector3* color,
                                              float range, const Vector3* velocity) {
    self->CreatePointLight(at, color, range, velocity);
    bool ok = ClassIdOf(self->light) == 0x4B &&
              *reinterpret_cast<const std::uint32_t*>(At(self->light, 0x160)) == 0x840;
    ei::verify::Check("CSpellEffect::CreatePointLight", true, ok, "range %g", static_cast<double>(range));
}
EI_VERIFY(0x0067F7C0, CreatePointLightVerify, 0, s_light);
