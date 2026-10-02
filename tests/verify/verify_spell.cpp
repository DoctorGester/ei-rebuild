// Differential checks for the spell rules and CLight (spell.cpp).
#include "verify.h"

#include "../../src/game/spell.h"

#include <cstring>

namespace {
std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Runs `ours` on the object, snapshots bytes [from, to), restores them, runs `original`, and
// reports whether both left the same bytes and returned the same value.
template <typename Obj, typename R, typename Ours, typename Original>
bool SameWrites(Obj* self, int from, int to, Ours ours, Original original, R& expected) {
    char saved[0x80], after[0x80];
    char* bytes = reinterpret_cast<char*>(self) + from;
    std::size_t n = static_cast<std::size_t>(to - from);
    std::memcpy(saved, bytes, n);
    R actual = ours();
    std::memcpy(after, bytes, n);
    std::memcpy(bytes, saved, n);
    expected = original();
    return expected == actual && std::memcmp(after, bytes, n) == 0;
}
} // namespace

// --- CSpell ----------------------------------------------------------------------------------

// 0068B320 writes +0x30..+0x5C. XOR EAX,EAX; XOR EDX,EDX; MOV [ECX+0x30],EAX (7 bytes).
static BOOL(__fastcall* s_create)(CSpell*);
static BOOL __fastcall CreateVerify(CSpell* self) {
    BOOL expected;
    bool same = SameWrites(self, 0x30, 0x5C, [&] { return self->Clear(); }, [&] { return s_create(self); }, expected);
    ei::verify::Check("CSpell::Clear", true, same, "spell %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x0068B320, CreateVerify, 7, s_create);

// 0068B430 writes +0x1C..+0x5C. MOV EAX,[ESP+4]; MOV EDX,[EAX+0x20] (7 bytes).
static BOOL(__fastcall* s_initFromPrototype)(CSpell*, void*, const char*);
static BOOL __fastcall InitFromPrototypeVerify(CSpell* self, void*, const char* prototype) {
    BOOL expected;
    bool same = SameWrites(self, 0x1C, 0x5C, [&] { return self->InitFromPrototype(prototype); },
                           [&] { return s_initFromPrototype(self, nullptr, prototype); }, expected);
    ei::verify::Check("CSpell::InitFromPrototype", true, same, "spell %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x0068B430, InitFromPrototypeVerify, 7, s_initFromPrototype);

// 0068BA20: MOV EDX,[0x007C8514] (6 bytes).
static char*(__fastcall* s_spellPrototype)(CSpell*);
static char* __fastcall SpellPrototypeVerify(CSpell* self) {
    char* expected = s_spellPrototype(self);
    ei::verify::Check("CSpell::GetPrototype", expected, self->GetPrototype(), "spell %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x0068BA20, SpellPrototypeVerify, 6, s_spellPrototype);

// 0068BA60: XOR EAX,EAX; MOV DL,[ECX+EAX+0x54] (6 bytes).
static int(__fastcall* s_modifierCount)(CSpell*);
static int __fastcall ModifierCountVerify(CSpell* self) {
    int expected = s_modifierCount(self);
    ei::verify::Check("CSpell::GetModifierCount", expected, self->GetModifierCount(), "spell %08X",
                      self->unknown_18);
    return expected;
}
EI_VERIFY(0x0068BA60, ModifierCountVerify, 6, s_modifierCount);

// 0068B7C0: PUSH EBX; MOV EBX,[0x007C8534] (7 bytes). Returns AL only.
static bool(__fastcall* s_hasCastFlag)(CSpell*);
static bool __fastcall HasCastFlagVerify(CSpell* self) {
    bool expected = s_hasCastFlag(self);
    ei::verify::Check("CSpell::HasCastFlagModifier", expected, self->HasCastFlagModifier(), "spell %08X",
                      self->unknown_18);
    return expected;
}
EI_VERIFY(0x0068B7C0, HasCastFlagVerify, 7, s_hasCastFlag);

// 0068B700 and 0068B770: MOV EAX,[ECX+0x18]; SHR EAX,0x10 (6 bytes).
static int(__fastcall* s_damageType)(CSpell*);
static int __fastcall DamageTypeVerify(CSpell* self) {
    int expected = s_damageType(self);
    ei::verify::Check("CSpell::GetDamageType", expected, self->GetDamageType(), "spell %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x0068B700, DamageTypeVerify, 6, s_damageType);

static bool(__fastcall* s_isOffensive)(CSpell*);
static bool __fastcall IsOffensiveVerify(CSpell* self) {
    bool expected = s_isOffensive(self);
    ei::verify::Check("CSpell::IsOffensive", expected, self->IsOffensive(), "spell %08X", self->unknown_18);
    return expected;
}
EI_VERIFY(0x0068B770, IsOffensiveVerify, 6, s_isOffensive);

// 0068B4C0: PUSH EBX; MOV EBX,[ECX+0x3C]; PUSH ESI (5 bytes).
static bool(__fastcall* s_canAffect)(CSpell*, void*, const CEngineObject*, CEngineObject*);
static bool __fastcall CanAffectVerify(CSpell* self, void*, const CEngineObject* caster, CEngineObject* target) {
    bool expected = s_canAffect(self, nullptr, caster, target);
    ei::verify::Check("CSpell::CanAffect", expected, self->CanAffect(caster, target), "flags %08X caster %p target %p",
                      self->targetFlags_, caster, target);
    return expected;
}
EI_VERIFY(0x0068B4C0, CanAffectVerify, 5, s_canAffect);

// --- CSpellEffect ----------------------------------------------------------------------------

// 00683950 only writes `out`: the original into a scratch struct, ours into `out`.
// MOV EDX,[ESP+4]; PUSH EBX; PUSH EBP (6 bytes).
static BOOL(__fastcall* s_getDamage)(CSpellEffect*, void*, DamageInfo*);
static BOOL __fastcall GetDamageVerify(CSpellEffect* self, void*, DamageInfo* out) {
    DamageInfo expected;
    BOOL expectedResult = s_getDamage(self, nullptr, &expected);
    BOOL actualResult = self->GetDamage(out);
    bool same = expectedResult == actualResult && std::memcmp(&expected, out, sizeof(expected)) == 0;
    ei::verify::Check("CSpellEffect::GetDamage", true, same, "spell %08X targets %d damage %g: amount %g vs %g",
                      self->spellId, self->targetCount, self->damage, expected.amount, out->amount);
    return expectedResult;
}
EI_VERIFY(0x00683950, GetDamageVerify, 6, s_getDamage);

// --- CLight ----------------------------------------------------------------------------------

// 0068D730 starts with a 4-byte CMP followed by a JNZ, too short to steal. Both of its branches
// are entry points of their own (thiscall, RET 4), so call the original's branch directly.
static void* s_unused;
static float __fastcall IntensityVerify(CLight* self, void*, const Vector3* point) {
    using Branch = float(__fastcall*)(CLight*, void*, const Vector3*);
    Branch original = reinterpret_cast<Branch>(self->type_ == 0x31 ? 0x0068D736 : 0x0068D7B4);
    float expected = original(self, nullptr, point);
    float actual = self->GetIntensityAt(point);
    ei::verify::Check("CLight::GetIntensityAt", Bits(expected), Bits(actual), "type %X point (%g, %g, %g): %g vs %g",
                      self->type_, point->x, point->y, point->z, expected, actual);
    return expected;
}
EI_VERIFY(0x0068D730, IntensityVerify, 0, s_unused);

// 0068D280 writes litColors_ (+0x114..+0x134). MOV EAX,[ESP+4]; FLD [ESP+8] (8 bytes).
static void(__fastcall* s_setLitColors)(CLight*, void*, const float*, float);
static void __fastcall SetLitColorsVerify(CLight* self, void*, const float* source, float scale) {
    int unusedResult;
    bool same = SameWrites(self, 0x114, 0x134, [&] { self->SetLitColors(source, scale); return 0; },
                           [&] { s_setLitColors(self, nullptr, source, scale); return 0; }, unusedResult);
    ei::verify::Check("CLight::SetLitColors", true, same, "scale %g", scale);
}
EI_VERIFY(0x0068D280, SetLitColorsVerify, 8, s_setLitColors);

// --- Database lookups by name ----------------------------------------------------------------

// 006893F0 / 006894B0: MOV ECX,[0x007C8514 / 0x007C8534] (6 bytes). __stdcall.
static int(__stdcall* s_spellCode)(const char*);
static int __stdcall SpellCodeVerify(const char* name) {
    int expected = s_spellCode(name);
    ei::verify::Check("SpellCodeByName", expected, SpellCodeByName(name), "'%s'", name);
    return expected;
}
EI_VERIFY(0x006893F0, SpellCodeVerify, 6, s_spellCode);

static char*(__stdcall* s_modifierCode)(const char*);
static char* __stdcall ModifierCodeVerify(const char* name) {
    char* expected = s_modifierCode(name);
    ei::verify::Check("SpellModifierCodeByName", expected, SpellModifierCodeByName(name), "'%s'", name);
    return expected;
}
EI_VERIFY(0x006894B0, ModifierCodeVerify, 6, s_modifierCode);
